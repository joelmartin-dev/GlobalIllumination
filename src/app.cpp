#include "app.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifndef IMGUI_IMPL_VULKAN_USE_VOLK
#define IMGUI_IMPL_VULKAN_USE_VOLK
#endif
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "fastgltf/core.hpp"
#include "fastgltf/types.hpp"
#include "fastgltf/tools.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

std::vector<const char*> getRequiredExtensions()
{
  uint32_t glfwExtensionCount = 0;
  auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
  if (enableValidationLayers)
  {
    extensions.push_back(vk::EXTDebugUtilsExtensionName);
  }

  return extensions;
}

void App::createInstance()
{
  if (volkInitialize() != VK_SUCCESS)
  {
    throw std::runtime_error("failed to initialise volk!");
  }

  constexpr vk::ApplicationInfo appInfo {
    .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
    .pEngineName        = "Backend Engine",
    .engineVersion      = VK_MAKE_VERSION(1, 0, 0),
    .apiVersion         = vk::ApiVersion14
  };

  std::vector<char const*> requiredLayers;
  if (enableValidationLayers)
  {
    requiredLayers.assign(validationLayers.begin(), validationLayers.end());
  }

  auto layerProperties = context.enumerateInstanceLayerProperties();
  for (auto const& requiredLayer : requiredLayers)
  {
      if (std::ranges::none_of(layerProperties,
                                [requiredLayer](auto const& layerProperty)
                                { return strcmp(layerProperty.layerName, requiredLayer) == 0; }))
      {
          throw std::runtime_error("Required layer not supported: " + std::string(requiredLayer));
      }
  }

  auto requiredExtensions = getRequiredExtensions();

  auto extensionProperties = context.enumerateInstanceExtensionProperties();
  for (auto const& requiredExtension : requiredExtensions)
  {
    if (std::ranges::none_of(extensionProperties, [requiredExtension](auto const& extensionProperty)
      { return strcmp(extensionProperty.extensionName, requiredExtension) == 0; }
    ))
    {
      throw std::runtime_error("required extension not supported: " + std::string(requiredExtension));
    }
  }

  vk::InstanceCreateInfo createInfo {
    .pApplicationInfo = &appInfo,
    .enabledLayerCount = static_cast<uint32_t>(requiredLayers.size()),
    .ppEnabledLayerNames = requiredLayers.data(),
    .enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size()),
    .ppEnabledExtensionNames = requiredExtensions.data()
  };

  instance = vk::raii::Instance(context, createInfo);
  volkLoadInstance(static_cast<VkInstance>(*instance));
}

void App::setupDebugMessenger()
{
  if (!enableValidationLayers) return;

  vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
  vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);

  vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT {
    .messageSeverity = severityFlags,
    .messageType = messageTypeFlags,
    .pfnUserCallback = &debugCallback
  };

  debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
}

void App::createSurface()
{
  VkSurfaceKHR _surface; // glfwCreateWindowSurface requires the struct defined in the C API
  if (glfwCreateWindowSurface(*instance, pWindow, nullptr, &_surface) != VK_SUCCESS)
  {
    throw std::runtime_error("failed to create window surface!");
  }
  surface = vk::raii::SurfaceKHR(instance, _surface);
}

vk::SampleCountFlagBits App::getMaxUsableSampleCount()
{
  vk::PhysicalDeviceProperties physicalDeviceProperties = physicalDevice.getProperties();

  vk::SampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
  if (counts & vk::SampleCountFlagBits::e64) { return vk::SampleCountFlagBits::e64; }
  if (counts & vk::SampleCountFlagBits::e32) { return vk::SampleCountFlagBits::e32; }
  if (counts & vk::SampleCountFlagBits::e16) { return vk::SampleCountFlagBits::e16; }
  if (counts & vk::SampleCountFlagBits::e8) { return vk::SampleCountFlagBits::e8; }
  if (counts & vk::SampleCountFlagBits::e4) { return vk::SampleCountFlagBits::e4; }
  if (counts & vk::SampleCountFlagBits::e2) { return vk::SampleCountFlagBits::e2; }

  return vk::SampleCountFlagBits::e1;
}

void App::pickPhysicalDevice()
{
  std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
  if (physicalDevices.empty())
  {
    throw std::runtime_error("failed to find any physical devices");
  }
  const auto devIter = std::ranges::find_if(physicalDevices, [&]( auto const& _physicalDevice)
    {
      // Check if the device supports the Vulkan 1.3 API version
      bool supportsVulkan1_3 = _physicalDevice.getProperties().apiVersion >= VK_API_VERSION_1_3;
      
      // Check if any of the queue families support graphics operations
      auto queueFamilies = _physicalDevice.getQueueFamilyProperties();
      bool supportsGraphics = std::ranges::any_of(queueFamilies, [](auto const& qfp) { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); } );
      
      // Check if all required device extensions are available
      auto availableDeviceExtensions = _physicalDevice.enumerateDeviceExtensionProperties();
      bool supportsAllRequiredExtensions = std::ranges::all_of(requiredDeviceExtensions, [&availableDeviceExtensions](auto const& requiredDeviceExtension)
        {
          return std::ranges::any_of(availableDeviceExtensions, [requiredDeviceExtension](auto const& availableDeviceExtension)
            { return strcmp(availableDeviceExtension.extensionName, requiredDeviceExtension) == 0; }
          );
        }
      );

      auto features = _physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
      bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
                                      features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState;            

      return supportsVulkan1_3 && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
    }
  );

  if (devIter != physicalDevices.end())
  {
    physicalDevice = *devIter;
    //getMaxUsableSampleCount();
  }
  else
  {
    throw std::runtime_error( "failed to find a suitable GPU!" );
  }
}

// Set up as single queue for all needs
uint32_t findQueueFamilies(const vk::raii::PhysicalDevice& _physicalDevice, const vk::SurfaceKHR& _surface)
{  
  std::vector<vk::QueueFamilyProperties> queueFamilyProperties = _physicalDevice.getQueueFamilyProperties();

  /* Example of how to get a potentially separate queue. For specific queues change the QueueFlagBits and variable names
  auto graphicsQueueFamilyProperties = std::find_if(queueFamilyProperties.begin(), queueFamilyProperties.end(), [](const auto& qfp)
    {
      return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
    }
  );
  auto graphicsIndex = static_cast<uint32_t>(std::distance(queueFamilyProperties.begin(), graphicsQueueFamilyProperties));
  */

  uint32_t queueFamilyIndex = ~0U; // like UINT_MAX

  for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
  {
    if ((queueFamilyProperties[qfpIndex].queueFlags & (vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute)) &&
      _physicalDevice.getSurfaceSupportKHR(qfpIndex, _surface))
    {
      queueFamilyIndex = qfpIndex;
      break;
    }
  }

  if (queueFamilyIndex == ~0U)
  {
    throw std::runtime_error("could not find a queue for graphics AND compute AND present!");
  }
  
  // return the index of the queue with a graphics queue family
  return queueFamilyIndex;
}

void App::createLogicalDevice()
{
  auto queueFamilyIndex = findQueueFamilies(physicalDevice, surface);
  
  vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain = {
    {},
    {.synchronization2 = true, .dynamicRendering = true},
    {.extendedDynamicState = true}
  };
  
  float queuePriority = 0.0f;

  vk::DeviceQueueCreateInfo deviceQueueCreateInfo {
    .queueFamilyIndex = queueFamilyIndex,
    .queueCount = 1,
    .pQueuePriorities = &queuePriority
  };
  vk::DeviceCreateInfo deviceCreateInfo {
    .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &deviceQueueCreateInfo,
    .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
    .ppEnabledExtensionNames = requiredDeviceExtensions.data()
  };

  device = vk::raii::Device(physicalDevice, deviceCreateInfo);
  queue = vk::raii::Queue(device, queueFamilyIndex, 0);
  graphicsIndex = queueFamilyIndex;
  (void) computeIndex;

  volkLoadDevice(static_cast<VkDevice>(*device));
}

vk::SurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<vk::SurfaceFormatKHR>& availableFormats)
{
  const auto formIter = std::find_if(availableFormats.begin(), availableFormats.end(), [](const auto& availableFormat)
    {
      return (availableFormat.format == vk::Format::eB8G8R8A8Srgb && availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear);
    }
  );

  return formIter != availableFormats.end() ? *formIter : availableFormats[0];
}

vk::PresentModeKHR chooseSwapPresentMode(const std::vector<vk::PresentModeKHR>& availablePresentModes)
{
  const auto presIter = std::find_if(availablePresentModes.begin(), availablePresentModes.end(), [](const auto& availablePresentMode)
    {
      return availablePresentMode == vk::PresentModeKHR::eMailbox;
    }
  );
  
  return presIter != availablePresentModes.end() ? vk::PresentModeKHR::eMailbox : vk::PresentModeKHR::eFifo;
}

vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR& capabilities, GLFWwindow* const _pWindow)
{
  if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    return capabilities.currentExtent;

  int width, height;
  glfwGetFramebufferSize(_pWindow, &width, &height);

  return {
    std::clamp<uint32_t>(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
    std::clamp<uint32_t>(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
  };
}

void App::createSwapChain()
{
  auto surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
  swapChainSurfaceFormat = chooseSwapSurfaceFormat(physicalDevice.getSurfaceFormatsKHR(surface));
  auto swapChainPresentMode = chooseSwapPresentMode(physicalDevice.getSurfacePresentModesKHR(surface));
  swapChainExtent = chooseSwapExtent(surfaceCapabilities, pWindow);
  uint32_t minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
  // clamp to the maxImageCount so long as maxImageCount has a maximum and is < than minImageCount
  minImageCount = (surfaceCapabilities.maxImageCount > 0 && minImageCount > surfaceCapabilities.maxImageCount) ? surfaceCapabilities.maxImageCount : minImageCount;
  uint32_t imageCount = surfaceCapabilities.minImageCount + 1;
  if (surfaceCapabilities.maxImageCount > 0 && imageCount > surfaceCapabilities.maxImageCount)
    imageCount = surfaceCapabilities.maxImageCount;

  vk::SwapchainCreateInfoKHR swapChainCreateInfo {
    .flags = vk::SwapchainCreateFlagsKHR(),
    .surface = surface,
    .minImageCount = minImageCount,
    .imageFormat = swapChainSurfaceFormat.format,
    .imageColorSpace = swapChainSurfaceFormat.colorSpace,
    .imageExtent = swapChainExtent,
    .imageArrayLayers = 1,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
    .imageSharingMode = vk::SharingMode::eExclusive,
    .preTransform = surfaceCapabilities.currentTransform,
    .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
    .presentMode = swapChainPresentMode,
    .clipped = vk::True,
    .oldSwapchain = VK_NULL_HANDLE
  };

  swapChain = vk::raii::SwapchainKHR(device, swapChainCreateInfo, nullptr);
  swapChainImages = swapChain.getImages();
}

void App::createImageViews()
{
  swapChainImageViews.clear();

  vk::ImageViewCreateInfo imageViewCreateInfo {
    .viewType = vk::ImageViewType::e2D,
    .format = swapChainSurfaceFormat.format,
    .subresourceRange = { 
      .aspectMask = vk::ImageAspectFlagBits::eColor, 
      .baseMipLevel = 0, 
      .levelCount = 1, 
      .baseArrayLayer = 0, 
      .layerCount = 1 
    }
  };

  for (auto image : swapChainImages)
  {
    imageViewCreateInfo.image = image;
    swapChainImageViews.emplace_back(device, imageViewCreateInfo);
  }
}

void App::recreateSwapChain()
{
  int width, height;
  glfwGetFramebufferSize(pWindow, &width, &height);
  while (width == 0 || height == 0)
  {
    glfwGetFramebufferSize(pWindow, &width, &height);
    glfwWaitEvents();
  }

  device.waitIdle();

  cleanupSwapChain();

  createSwapChain();
  createImageViews();
}

void App::createDescriptorSetLayout()
{
  std::array bindings = {
    vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eVertex, nullptr),
    vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eCombinedImageSampler, 1, vk::ShaderStageFlagBits::eFragment, nullptr),
  };

  vk::DescriptorSetLayoutCreateInfo layoutInfo {
    .bindingCount = static_cast<uint32_t>(bindings.size()),
    .pBindings = bindings.data()
  };

  descriptorSetLayout = vk::raii::DescriptorSetLayout(device, layoutInfo);
}

void App::createGraphicsPipeline()
{
  auto shaderModule = createShaderModule(readFile("shaders/shader.spv"));
  
  vk::PipelineShaderStageCreateInfo vertShaderModuleCreateInfo {
    .stage = vk::ShaderStageFlagBits::eVertex,
    .module = shaderModule,
    .pName = "vertMain"
  };
  vk::PipelineShaderStageCreateInfo fragShaderModuleCreateInfo {
    .stage = vk::ShaderStageFlagBits::eFragment,
    .module = shaderModule,
    .pName = "fragMain"
  };
  vk::PipelineShaderStageCreateInfo shaderStages[] = {vertShaderModuleCreateInfo, fragShaderModuleCreateInfo};
  
  auto bindingDescription = Vertex::getBindingDescription();
  auto attributesDescriptions = Vertex::getAttributeDescriptions();
  vk::PipelineVertexInputStateCreateInfo vertexInputInfo {
    .vertexBindingDescriptionCount = 1,
    .pVertexBindingDescriptions = &bindingDescription,
    .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributesDescriptions.size()),
    .pVertexAttributeDescriptions = attributesDescriptions.data()
  };
  vk::PipelineInputAssemblyStateCreateInfo inputAssemblyInfo {
    .topology = vk::PrimitiveTopology::eTriangleList
  };

  std::vector<vk::DynamicState> dynamicStates = {
    vk::DynamicState::eViewport,
    vk::DynamicState::eScissor
  };

  vk::PipelineDynamicStateCreateInfo dynamicInfo {
    .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
    .pDynamicStates = dynamicStates.data()
  };

  vk::PipelineViewportStateCreateInfo viewportInfo {
    .viewportCount = 1,
    .scissorCount = 1
  };

  vk::PipelineRasterizationStateCreateInfo rasterizerInfo {
    .depthClampEnable = vk::False,
    .rasterizerDiscardEnable = vk::False,
    .polygonMode = vk::PolygonMode::eFill,
    .cullMode = vk::CullModeFlagBits::eBack,
    .frontFace = vk::FrontFace::eCounterClockwise,
    .depthBiasEnable = vk::False,
    .depthBiasConstantFactor = 0.0f,
    .depthBiasClamp = 0.0f,
    .depthBiasSlopeFactor = 1.0f,
    .lineWidth = 1.0f
  };

  vk::PipelineMultisampleStateCreateInfo multisamplingInfo {
    .sampleShadingEnable = vk::False
  };

  if (msaaSamples == vk::SampleCountFlagBits::e1)
  {
    multisamplingInfo.rasterizationSamples = vk::SampleCountFlagBits::e1;
  }
  else if (msaaSamples == vk::SampleCountFlagBits::e2)
  {
    multisamplingInfo.rasterizationSamples = vk::SampleCountFlagBits::e2;
  }
  else if (msaaSamples == vk::SampleCountFlagBits::e4)
  {
    multisamplingInfo.rasterizationSamples = vk::SampleCountFlagBits::e4;
  }
  else
  {
    multisamplingInfo.rasterizationSamples = vk::SampleCountFlagBits::e8;
  }

  vk::PipelineColorBlendAttachmentState colorBlendAttachment {
    .blendEnable = vk::False,
    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
  };

  vk::PipelineColorBlendStateCreateInfo colorBlendInfo {
    .logicOpEnable = vk::False,
    .logicOp = vk::LogicOp::eCopy,
    .attachmentCount = 1,
    .pAttachments = &colorBlendAttachment
  };

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo {
    .setLayoutCount = 1,
    .pSetLayouts = &*descriptorSetLayout,
    .pushConstantRangeCount = 0
  };
  pipelineLayout = vk::raii::PipelineLayout(device, pipelineLayoutInfo);

  vk::PipelineRenderingCreateInfo pipelineRenderingInfo = {
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &swapChainSurfaceFormat.format,
  };

  vk::GraphicsPipelineCreateInfo graphicsPipelineInfo {
    .pNext = &pipelineRenderingInfo,
    // .flags = vk::PipelineCreateFlagBits::eDerivative,
    .stageCount = 2,
    .pStages = shaderStages,
    .pVertexInputState = &vertexInputInfo,
    .pInputAssemblyState = &inputAssemblyInfo,
    .pViewportState = &viewportInfo,
    .pRasterizationState = &rasterizerInfo,
    .pMultisampleState = &multisamplingInfo,
    .pColorBlendState = &colorBlendInfo,
    .pDynamicState = &dynamicInfo,
    .layout = pipelineLayout,
    .renderPass = nullptr,
    // .basePipelineHandle = VK_NULL_HANDLE,
    // .basePipelineIndex = -1,
  };

  graphicsPipeline = vk::raii::Pipeline(device, nullptr, graphicsPipelineInfo);
}

void App::createCommandPool()
{
  vk::CommandPoolCreateInfo commandPoolInfo {
    .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
    .queueFamilyIndex = graphicsIndex,
  };
  commandPool = vk::raii::CommandPool(device, commandPoolInfo);
}

uint32_t App::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties)
{
  // typeFilter is a bitmask, and we iterate over it by shifting 1 by i
  // then we check if it has the same properties as properties
  vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
  {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
    {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}

void App::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& bufferMemory)
{
    vk::BufferCreateInfo bufferInfo{
      .size = size,
      .usage = usage,
      .sharingMode = vk::SharingMode::eExclusive
    };
    buffer = vk::raii::Buffer(device, bufferInfo);
    vk::MemoryRequirements memRequirements = buffer.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{
      .allocationSize = memRequirements.size,
      .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)
    };
    bufferMemory = vk::raii::DeviceMemory(device, allocInfo);
    buffer.bindMemory(*bufferMemory, 0);
}

void App::copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size)
{
  auto commandCopyBuffer = beginSingleTimeCommands();
  commandCopyBuffer.copyBuffer(srcBuffer, dstBuffer, vk::BufferCopy(0, 0, size));
  endSingleTimeCommands(commandCopyBuffer);
}

void App::copyBufferToImage(const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height)
{
  vk::raii::CommandBuffer commandBuffer = beginSingleTimeCommands();
  
  vk::BufferImageCopy region {
    .bufferOffset = 0,
    .bufferRowLength = 0,
    .bufferImageHeight = 0,
    .imageSubresource = {
      .aspectMask = vk::ImageAspectFlagBits::eColor,
      .mipLevel = 0,
      .baseArrayLayer = 0,
      .layerCount = 1
    },
    .imageOffset = { 0, 0, 0 },
    .imageExtent = {
      .width = width,
      .height = height,
      .depth = 1
    }
  };

  commandBuffer.copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, {region});

  endSingleTimeCommands(commandBuffer);
}

void App::createImage(uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Image& image, vk::raii::DeviceMemory& imageMemory) {
    vk::ImageCreateInfo imageInfo {
      .imageType = vk::ImageType::e2D,
      .format = format,
      .extent = {width, height, 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = vk::SampleCountFlagBits::e1,
      .tiling = tiling,
      .usage = usage,
      .sharingMode = vk::SharingMode::eExclusive,
      .queueFamilyIndexCount = 0
    };

    image = vk::raii::Image( device, imageInfo );

    vk::MemoryRequirements memRequirements = image.getMemoryRequirements();
    vk::MemoryAllocateInfo allocInfo{
      .allocationSize = memRequirements.size, 
      .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)
    };
    imageMemory = vk::raii::DeviceMemory(device, allocInfo);
    image.bindMemory(imageMemory, 0);
}

void App::transitionTextureImageLayout(const vk::raii::Image& _textureImage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout)
{
  auto commandBuffer = beginSingleTimeCommands();

  vk::ImageMemoryBarrier barrier {
    .oldLayout = oldLayout,
    .newLayout = newLayout,
    .image = _textureImage,
    .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}
  };

  vk::PipelineStageFlags srcStage;
  vk::PipelineStageFlags dstStage;

  if (oldLayout == vk::ImageLayout::eUndefined && newLayout == vk::ImageLayout::eTransferDstOptimal)
  {
    barrier.srcAccessMask = {};
    barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
    
    srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
    dstStage = vk::PipelineStageFlagBits::eTransfer;
  }
  else if (oldLayout == vk::ImageLayout::eTransferDstOptimal && newLayout == vk::ImageLayout::eShaderReadOnlyOptimal)
  {
    barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    
    srcStage = vk::PipelineStageFlagBits::eTransfer;
    dstStage = vk::PipelineStageFlagBits::eFragmentShader;
  }
  else
  {
    throw std::invalid_argument("unsupported layout transition!");
  }

  commandBuffer.pipelineBarrier(srcStage, dstStage, {}, {}, nullptr, barrier);
  endSingleTimeCommands(commandBuffer);
}

void App::createTextureImage()
{
  int texWidth, texHeight, texChannels;
  stbi_uc* pixels = stbi_load(TEXTURE_PATH, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
  vk::DeviceSize imageSize = texWidth * texHeight * 4; // width * height * channels used

  if (pixels == nullptr)
  {
    throw std::runtime_error("failed to load texture image!");
  }

  vk::raii::Buffer stagingBuffer({});
  vk::raii::DeviceMemory stagingBufferMemory({});

  createBuffer(
    imageSize,
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
    stagingBuffer,
    stagingBufferMemory
  );

  void* data = stagingBufferMemory.mapMemory(0, imageSize);
  memcpy(data, pixels, imageSize);
  stagingBufferMemory.unmapMemory();

  stbi_image_free(pixels);

  createImage(static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), vk::Format::eR8G8B8A8Srgb, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, textureImage, textureImageMemory);

  transitionTextureImageLayout(textureImage, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
  copyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight));
  transitionTextureImageLayout(textureImage, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
}

vk::raii::ImageView App::createImageView(vk::raii::Image& image, vk::Format format)
{
  vk::ImageViewCreateInfo viewInfo {
    .image = image,
    .viewType = vk::ImageViewType::e2D,
    .format = format,
    .subresourceRange = {
      .aspectMask = vk::ImageAspectFlagBits::eColor,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1
    }
  };

  return vk::raii::ImageView(device, viewInfo);
}

void App::createTextureImageView()
{
  textureImageView = createImageView(textureImage, vk::Format::eR8G8B8A8Srgb);
}

void App::createTextureSampler()
{
  vk::PhysicalDeviceProperties properties = physicalDevice.getProperties();
  vk::SamplerCreateInfo samplerInfo {
    .magFilter = vk::Filter::eLinear,
    .minFilter = vk::Filter::eLinear,
    .mipmapMode = vk::SamplerMipmapMode::eLinear,
    .addressModeU = vk::SamplerAddressMode::eRepeat,
    .addressModeV = vk::SamplerAddressMode::eRepeat,
    .addressModeW = vk::SamplerAddressMode::eRepeat,
    .mipLodBias = 0.0f,
    .anisotropyEnable = vk::True,
    .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
    .compareEnable = vk::False,
    .compareOp = vk::CompareOp::eAlways,
    .minLod = 0.0f,
    .maxLod = 0.0f,
    .borderColor = vk::BorderColor::eIntOpaqueBlack,
    .unnormalizedCoordinates = vk::False
  };

  textureSampler = vk::raii::Sampler(device, samplerInfo);
}

void App::createVertexBuffer()
{
  vk::DeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
  vk::raii::Buffer stagingBuffer({});
  vk::raii::DeviceMemory stagingBufferMemory({});

  createBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
    stagingBuffer,
    stagingBufferMemory
  );

  void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
  memcpy(dataStaging, vertices.data(), (size_t )bufferSize);
  stagingBufferMemory.unmapMemory();

  createBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    vertexBuffer,
    vertexBufferMemory
  );

  copyBuffer(stagingBuffer, vertexBuffer, bufferSize);
}

void App::createIndexBuffer()
{
  vk::DeviceSize bufferSize = sizeof(indices[0]) * indices.size();
  vk::raii::Buffer stagingBuffer({});
  vk::raii::DeviceMemory stagingBufferMemory({});

  createBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eTransferSrc,
    vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent,
    stagingBuffer,
    stagingBufferMemory
  );

  void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
  memcpy(dataStaging, indices.data(), (size_t) bufferSize);
  stagingBufferMemory.unmapMemory();

  createBuffer(
    bufferSize,
    vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    indexBuffer,
    indexBufferMemory
  );

  copyBuffer(stagingBuffer, indexBuffer, bufferSize);
}

void App::createUniformBuffers()
{
    uniformBuffers.clear();
    uniformBuffersMemory.clear();
    uniformBuffersMapped.clear();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
      vk::DeviceSize bufferSize = sizeof(UniformBufferObject);
      vk::raii::Buffer buffer({});
      vk::raii::DeviceMemory bufferMemory({});
      createBuffer(bufferSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent, buffer, bufferMemory);
      uniformBuffers.emplace_back(std::move(buffer));
      uniformBuffersMemory.emplace_back(std::move(bufferMemory));
      uniformBuffersMapped.emplace_back(uniformBuffersMemory[i].mapMemory(0, bufferSize));
    }
}


void App::createDescriptorPools()
{
  std::array poolSizes = {
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT),
    vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT)
  };

  vk::DescriptorPoolCreateInfo poolInfo {
    .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
    .maxSets = MAX_FRAMES_IN_FLIGHT,
    .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
    .pPoolSizes = poolSizes.data()
  };

  descriptorPool = vk::raii::DescriptorPool(device, poolInfo);

  // Create descriptor pool for ImGui
  std::array imguiPoolSizes = {
    vk::DescriptorPoolSize(vk::DescriptorType::eSampler, 1000),
    vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 1000),
    vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, 1000),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 1000),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformTexelBuffer, 1000),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageTexelBuffer, 1000),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1000),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 1000),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, 1000),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageBufferDynamic, 1000),
    vk::DescriptorPoolSize(vk::DescriptorType::eInputAttachment, 1000)
  };

  vk::DescriptorPoolCreateInfo imguiPoolInfo {
    .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
    .maxSets = 1000 * static_cast<uint32_t>(imguiPoolSizes.size()),
    .poolSizeCount = static_cast<uint32_t>(imguiPoolSizes.size()),
    .pPoolSizes = imguiPoolSizes.data()
  };

  imguiDescriptorPool = vk::raii::DescriptorPool(device, imguiPoolInfo);
}

void App::createDescriptorSets()
{  
  std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
  vk::DescriptorSetAllocateInfo allocInfo {
    .descriptorPool = descriptorPool,
    .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
    .pSetLayouts = layouts.data(),
  };
  
  descriptorSets.clear();
  descriptorSets = device.allocateDescriptorSets(allocInfo);

  if (descriptorSets.empty())
  {
    throw std::runtime_error("failed to allocate descriptor sets!");
  }

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
  {
    vk::DescriptorBufferInfo bufferInfo {
      .buffer = uniformBuffers[i],
      .offset = 0,
      .range = sizeof(UniformBufferObject)
    };

    vk::DescriptorImageInfo imageInfo {
      .sampler = textureSampler,
      .imageView = textureImageView,
      .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
    };

    std::array descriptorWrites = {
      vk::WriteDescriptorSet{
        .dstSet = static_cast<vk::DescriptorSet>(*descriptorSets[i]),
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eUniformBuffer,
        .pBufferInfo = &bufferInfo
      },
      vk::WriteDescriptorSet{
        .dstSet = static_cast<vk::DescriptorSet>(*descriptorSets[i]),
        .dstBinding = 1,
        .dstArrayElement = 0,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .pImageInfo = &imageInfo
      },
    };

    device.updateDescriptorSets(descriptorWrites, {});
  }
}

void App::createCommandBuffers()
{
  commandBuffers.clear();

  vk::CommandBufferAllocateInfo allocInfo {
    .commandPool = commandPool,
    .level = vk::CommandBufferLevel::ePrimary,
    .commandBufferCount = MAX_FRAMES_IN_FLIGHT
  };

  commandBuffers = vk::raii::CommandBuffers(device, allocInfo);
}

vk::raii::CommandBuffer App::beginSingleTimeCommands()
{
  vk::CommandBufferAllocateInfo allocInfo {
    .commandPool = commandPool,
    .level = vk::CommandBufferLevel::ePrimary,
    .commandBufferCount = 1
  };
  vk::raii::CommandBuffer commandBuffer = std::move(device.allocateCommandBuffers(allocInfo).front());

  vk::CommandBufferBeginInfo beginInfo {
    .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
  };
  commandBuffer.begin(beginInfo);

  return commandBuffer;
}

void App::endSingleTimeCommands(vk::raii::CommandBuffer& commandBuffer)
{
  commandBuffer.end();

  vk::SubmitInfo submitInfo {
    .commandBufferCount = 1,
    .pCommandBuffers = &*commandBuffer
  };
  queue.submit(submitInfo, nullptr);
  queue.waitIdle();
}

void App::recordCommandBuffer(uint32_t imageIndex)
{
  commandBuffers[currentFrame].begin({});

  transitionImageLayout(
    imageIndex,
    vk::ImageLayout::eUndefined,
    vk::ImageLayout::eColorAttachmentOptimal,
    {},
    vk::AccessFlagBits2::eColorAttachmentWrite,
    vk::PipelineStageFlagBits2::eTopOfPipe,
    vk::PipelineStageFlagBits2::eColorAttachmentOutput
  );

  vk::ClearValue clearColour = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
  vk::RenderingAttachmentInfo attachmentInfo {
    .imageView = swapChainImageViews[imageIndex],
    .imageLayout = vk::ImageLayout::eAttachmentOptimal,
    .loadOp = vk::AttachmentLoadOp::eClear,
    .storeOp = vk::AttachmentStoreOp::eStore,
    .clearValue = clearColour
  };
  
  vk::RenderingInfo renderingInfo {
    .renderArea = {
      .offset = {0, 0},
      .extent = swapChainExtent
    },
    .layerCount = 1,
    .colorAttachmentCount = 1,
    .pColorAttachments = &attachmentInfo
  };
  
  commandBuffers[currentFrame].beginRendering(renderingInfo);
  
  commandBuffers[currentFrame].bindPipeline(
    vk::PipelineBindPoint::eGraphics,
    *graphicsPipeline
  );

  commandBuffers[currentFrame].setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f));
  commandBuffers[currentFrame].setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));
  
  commandBuffers[currentFrame].bindVertexBuffers(0, *vertexBuffer, {0});
  commandBuffers[currentFrame].bindIndexBuffer(*indexBuffer, 0, vk::IndexTypeValue<decltype(indices)::value_type>::value);
  
  commandBuffers[currentFrame].bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipelineLayout, 0, *descriptorSets[currentFrame], nullptr);

  commandBuffers[currentFrame].drawIndexed(indices.size(), 1, 0, 0, 0);
  
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(*commandBuffers[currentFrame]));

  commandBuffers[currentFrame].endRendering();
  
  transitionImageLayout(
    imageIndex,
    vk::ImageLayout::eColorAttachmentOptimal,
    vk::ImageLayout::ePresentSrcKHR,
    vk::AccessFlagBits2::eColorAttachmentWrite,
    {},
    vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    vk::PipelineStageFlagBits2::eBottomOfPipe
  );

  commandBuffers[currentFrame].end();
}

void App::transitionImageLayout(
  uint32_t imageIndex,
  vk::ImageLayout oldLayout,
  vk::ImageLayout newLayout,
  vk::AccessFlags2 srcAccessMask,
  vk::AccessFlags2 dstAccessMask,
  vk::PipelineStageFlags2 srcStageMask,
  vk::PipelineStageFlags2 dstStageMask
)
{
  vk::ImageMemoryBarrier2 barrier = {
    .srcStageMask = srcStageMask,
    .srcAccessMask = srcAccessMask,
    .dstStageMask = dstStageMask,
    .dstAccessMask = dstAccessMask,
    .oldLayout = oldLayout,
    .newLayout = newLayout,
    .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
    .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
    .image = swapChainImages[imageIndex],
    .subresourceRange = {
      .aspectMask = vk::ImageAspectFlagBits::eColor,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1
    }
  };

  vk::DependencyInfo dependencyInfo {
    .dependencyFlags = {},
    .imageMemoryBarrierCount = 1,
    .pImageMemoryBarriers = &barrier
  };

  commandBuffers[currentFrame].pipelineBarrier2(dependencyInfo);
}

void App::createSyncObjects()
{
  presentCompleteSemaphores.clear();
  renderFinishedSemaphores.clear();
  inFlightFences.clear();

  for (size_t i = 0; i < swapChainImages.size(); i++)
  { 
    presentCompleteSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
    renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo());
  }

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
  {    
    inFlightFences.emplace_back(device, vk::FenceCreateInfo {.flags = vk::FenceCreateFlagBits::eSignaled});
  }
}

void App::updateUniformBuffer(uint32_t imageIndex)
{
  static auto startTime = std::chrono::high_resolution_clock::now();

  auto currentTime = std::chrono::high_resolution_clock::now();
  float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

  UniformBufferObject ubo{};
  ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
  ubo.proj = glm::perspective(glm::radians(45.0f), static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height), 0.1f, 10.0f);
  ubo.proj[1][1] *= -1;

  memcpy(uniformBuffersMapped[imageIndex], &ubo, sizeof(ubo));
}

void App::drawFrame()
{
  while (vk::Result::eTimeout == device.waitForFences(*inFlightFences[currentFrame], vk::True, UINT64_MAX))
  { }
  
  auto [result, imageIndex] = swapChain.acquireNextImage(UINT64_MAX, *presentCompleteSemaphores[semaphoreIndex], nullptr);

  if (result == vk::Result::eErrorOutOfDateKHR)
  {
    recreateSwapChain();
    return;
  } 
  else if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR)
  {
    throw std::runtime_error("failed to acquire swap chain image!");
  }

  updateUniformBuffer(currentFrame);

  device.resetFences(*inFlightFences[currentFrame]);
  commandBuffers[currentFrame].reset();

  recordCommandBuffer(imageIndex);

  vk::PipelineStageFlags waitDestinationStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput);
  
  const vk::SubmitInfo submitInfo {
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &*presentCompleteSemaphores[semaphoreIndex],
    .pWaitDstStageMask = &waitDestinationStageMask,
    .commandBufferCount = 1,
    .pCommandBuffers = &*commandBuffers[currentFrame],
    .signalSemaphoreCount = 1,
    .pSignalSemaphores = &*renderFinishedSemaphores[imageIndex]
  };

  queue.submit(submitInfo, *inFlightFences[currentFrame]);

  const vk::PresentInfoKHR presentInfo {
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &*renderFinishedSemaphores[imageIndex],
    .swapchainCount = 1,
    .pSwapchains = &*swapChain,
    .pImageIndices = &imageIndex,
  };

  result = queue.presentKHR(presentInfo);
  if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR || frameBufferResized)
  {
    frameBufferResized = false;
    recreateSwapChain();
  }
  else if (result != vk::Result::eSuccess)
  {
    throw std::runtime_error("failed to present swap chain image!");
  }

  semaphoreIndex = (semaphoreIndex + 1) % presentCompleteSemaphores.size();
  currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void App::initImGui()
{
  if (!static_cast<VkDevice>(*device))
  {
    throw std::runtime_error("ImGui not working with device!");
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  if (!ImGui_ImplGlfw_InitForVulkan(pWindow, true))
  {
    throw std::runtime_error("failed to initialise ImGui GLFW backend!");
  }


  ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.ApiVersion = vk::ApiVersion14;
    initInfo.Instance = static_cast<VkInstance>(*instance);
    initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(*physicalDevice);
    initInfo.Device = static_cast<VkDevice>(*device);
    initInfo.QueueFamily = graphicsIndex;
    initInfo.Queue = static_cast<VkQueue>(*queue);
    initInfo.DescriptorPool = static_cast<VkDescriptorPool>(*imguiDescriptorPool);
    initInfo.MinImageCount = static_cast<uint32_t>(swapChainImages.size());
    initInfo.ImageCount = static_cast<uint32_t>(swapChainImages.size());
    initInfo.MSAASamples = static_cast<VkSampleCountFlagBits>(msaaSamples);
    initInfo.UseDynamicRendering = true;

    vk::PipelineRenderingCreateInfo imguiPipelineInfo {
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = reinterpret_cast<const vk::Format *>(&swapChainSurfaceFormat.format)
    };

    initInfo.PipelineRenderingCreateInfo = imguiPipelineInfo;

  if (ImGui_ImplVulkan_Init(&initInfo) != true)
  {
    throw std::runtime_error("failed to initialise ImGuiImplVulkan!");
  }
}

void App::initSponza()
{

}

void App::initWindow()
{
  if (glfwInit() != GLFW_TRUE)
  {
    throw std::runtime_error("failed to initialise GLFW!");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

  pWindow = glfwCreateWindow(WIDTH, HEIGHT, "App", nullptr, nullptr);

  if (pWindow == nullptr)
  {
    throw std::runtime_error("failed to create GLFWwindow!");
  }

  glfwSetFramebufferSizeCallback(pWindow, framebufferResizeCallback);
  glfwSetKeyCallback(pWindow, key_callback);
  glfwSetMouseButtonCallback(pWindow, mouse_button_callback);
  glfwSetScrollCallback(pWindow, scroll_callback);
  
  glfwSetWindowUserPointer(pWindow, this);
  glfwSetInputMode(pWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
}

void App::initVulkan()
{
  createInstance();
  setupDebugMessenger();
  createSurface();
  pickPhysicalDevice();
  createLogicalDevice();
  createSwapChain();
  createImageViews();
  createDescriptorSetLayout();
  createGraphicsPipeline();
  createCommandPool();
  createTextureImage();
  createTextureImageView();
  createTextureSampler();
  createVertexBuffer();
  createIndexBuffer();
  createUniformBuffers();
  createDescriptorPools();
  createDescriptorSets();
  createCommandBuffers();
  createSyncObjects();
}

void App::cleanupSwapChain()
{
  swapChainImageViews.clear();
  swapChain = nullptr;

}

void App::cleanup()
{
  cleanupSwapChain();

  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(pWindow);
  glfwTerminate();
}

void App::mainLoop()
{
  static bool showWindow = true;
  while (glfwWindowShouldClose(pWindow) != GLFW_TRUE)
  {
    glfwPollEvents();

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    {
      ImGui::Begin("Frametime", &showWindow, 0);
      ImGui::Text("%fms", ImGui::GetIO().Framerate);
      ImGui::End();
    }

    ImGui::Render();

    drawFrame();
  }
  device.waitIdle();
}

void App::run()
{
  initWindow();
  initVulkan();
  initImGui();
  initSponza();
  mainLoop();
  cleanup();
}