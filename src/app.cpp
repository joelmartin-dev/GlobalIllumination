#include "app.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <chrono>
#include <memory>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#ifndef IMGUI_IMPL_VULKAN_USE_VOLK
#define IMGUI_IMPL_VULKAN_USE_VOLK
#endif
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "fastgltf/core.hpp"
#include "fastgltf/tools.hpp"
#include "fastgltf/glm_element_traits.hpp"

#include "ktxvulkan.h"

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

// vk::SampleCountFlagBits App::getMaxUsableSampleCount()
// {
//   vk::PhysicalDeviceProperties physicalDeviceProperties = physicalDevice.getProperties();

//   vk::SampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
//   if (counts & vk::SampleCountFlagBits::e64) { return vk::SampleCountFlagBits::e64; }
//   if (counts & vk::SampleCountFlagBits::e32) { return vk::SampleCountFlagBits::e32; }
//   if (counts & vk::SampleCountFlagBits::e16) { return vk::SampleCountFlagBits::e16; }
//   if (counts & vk::SampleCountFlagBits::e8) { return vk::SampleCountFlagBits::e8; }
//   if (counts & vk::SampleCountFlagBits::e4) { return vk::SampleCountFlagBits::e4; }
//   if (counts & vk::SampleCountFlagBits::e2) { return vk::SampleCountFlagBits::e2; }

//   return vk::SampleCountFlagBits::e1;
// }

void App::checkFeatureSupport()
{
  engineInfo.profile = {
    VP_KHR_ROADMAP_2022_NAME,
    VP_KHR_ROADMAP_2022_SPEC_VERSION
  };

  VkBool32 supported = VK_FALSE;
  VkResult result = vpGetPhysicalDeviceProfileSupport(
    *instance,
    *physicalDevice,
    &engineInfo.profile,
    &supported
  );

  if (result == VK_SUCCESS && supported == VK_TRUE)
  {
    engineInfo.profileSupported = true;
    std::clog << "Using KHR roadmap 2022 profile" << std::endl;
  }
  else
  {
    engineInfo.profileSupported = false;
    throw std::runtime_error("KHR roadmap 2022 profile not supported!");
  }
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
      vk::PhysicalDeviceProperties properties = _physicalDevice.getProperties();
      // Check if the device supports the Vulkan 1.3 API version
      bool supportsVulkan1_3 = properties.apiVersion >= VK_API_VERSION_1_3;
      bool supportsSamplerAnisotropy = properties.limits.maxSamplerAnisotropy >= 1.0f;
      
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

      return supportsVulkan1_3 && supportsSamplerAnisotropy && supportsGraphics && supportsAllRequiredExtensions && supportsRequiredFeatures;
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
    { .features = {.samplerAnisotropy = vk::True}},
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
  createDepthResources();
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

[[nodiscard]] vk::raii::ShaderModule App::createShaderModule(const std::vector<char>& code) const {
    vk::ShaderModuleCreateInfo createInfo {
      .codeSize = code.size() * sizeof(char),
      .pCode = reinterpret_cast<const uint32_t*>(code.data())
    };
    
    vk::raii::ShaderModule shaderModule{device, createInfo};
    
    return shaderModule;
  };

void App::createGraphicsPipeline()
{
  auto shaderModule = createShaderModule(readFile(SHADER_PATH));
  
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
    .rasterizationSamples = vk::SampleCountFlagBits::e1,
    .sampleShadingEnable = vk::False,
  };

  vk::PipelineDepthStencilStateCreateInfo depthStencil
  {
    .depthTestEnable = vk::True,
    .depthWriteEnable = vk::True,
    .depthCompareOp = vk::CompareOp::eLess,
    .depthBoundsTestEnable = vk::False,
    .stencilTestEnable = vk::False
  };

  vk::PipelineColorBlendAttachmentState colorBlendAttachment {
    .blendEnable = vk::False,
    .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
    .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .colorBlendOp = vk::BlendOp::eAdd,
    .srcAlphaBlendFactor = vk::BlendFactor::eSrcAlpha,
    .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
    .alphaBlendOp = vk::BlendOp::eAdd,
    .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    
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

  vk::Format depthFormat = findDepthFormat();
  vk::PipelineRenderingCreateInfo pipelineRenderingInfo = {
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &swapChainSurfaceFormat.format,
    .depthAttachmentFormat = depthFormat
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
    .pDepthStencilState = &depthStencil,
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
  vk::CommandBufferAllocateInfo allocInfo {
    .commandPool = commandPool,
    .level = vk::CommandBufferLevel::ePrimary, 
    .commandBufferCount = 1
  };
  vk::raii::CommandBuffer commandCopyBuffer = std::move(device.allocateCommandBuffers(allocInfo).front());
  commandCopyBuffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
  commandCopyBuffer.copyBuffer(*srcBuffer, *dstBuffer, vk::BufferCopy{.size = size});
  commandCopyBuffer.end();
  queue.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &*commandCopyBuffer}, nullptr);
  queue.waitIdle();
}

void App::copyBufferToImage(const vk::raii::Buffer& buffer, const vk::raii::Image& image, uint32_t width, uint32_t height, uint32_t mipLevels, ktxTexture2* kTexture)
{
  std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = beginSingleTimeCommands();
  
  std::vector<vk::BufferImageCopy> regions;

  for (uint32_t level = 0; level < mipLevels; level++)
  {
    ktx_size_t offset;
    ktxTexture2_GetImageOffset(kTexture, level, 0, 0, &offset);

    uint32_t mipWidth = std::max(1u, width >> level);
    uint32_t mipHeight = std::max(1u, height >> level);

    vk::BufferImageCopy region {
      .bufferOffset = offset,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = level,
        .baseArrayLayer = 0,
        .layerCount = 1
      },
      .imageOffset = { 0, 0, 0 },
      .imageExtent = {
        .width = mipWidth,
        .height = mipHeight,
        .depth = 1
      }
    };

    regions.push_back(region);
  }
  commandBuffer->copyBufferToImage(buffer, image, vk::ImageLayout::eTransferDstOptimal, regions);

  endSingleTimeCommands(*commandBuffer);
}

void App::createImage(uint32_t width, uint32_t height, vk::Format format, uint32_t mipLevels, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Image& image, vk::raii::DeviceMemory& imageMemory) {
    vk::ImageCreateInfo imageInfo {
      .imageType = vk::ImageType::e2D,
      .format = format,
      .extent = {width, height, 1},
      .mipLevels = mipLevels,
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

void App::transitionImageLayout(const vk::raii::Image& image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, uint32_t mipLevels)
{
  const auto commandBuffer = beginSingleTimeCommands();

  vk::ImageMemoryBarrier barrier {
    .oldLayout = oldLayout,
    .newLayout = newLayout,
    .image = image,
    .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels, 0, 1}
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

  commandBuffer->pipelineBarrier(srcStage, dstStage, {}, {}, nullptr, barrier);
  endSingleTimeCommands(*commandBuffer);
}

vk::Format App::findSupportedFormat(const std::vector<vk::Format>& candidates, vk::ImageTiling tiling, vk::FormatFeatureFlags features) const
{
  auto formatIter = std::ranges::find_if(candidates, [&](auto const format)
    {
      vk::FormatProperties props = physicalDevice.getFormatProperties(format);
      return (((tiling == vk::ImageTiling::eLinear ) && ((props.linearTilingFeatures  & features) == features)) ||
              ((tiling == vk::ImageTiling::eOptimal) && ((props.optimalTilingFeatures & features) == features))
      );
    }
  );
  if (formatIter == candidates.end())
  {
    throw std::runtime_error("failed to find supported format!");
  }
  return *formatIter;
}

[[nodiscard]] vk::Format App::findDepthFormat() const
{
  return findSupportedFormat(
    {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint, vk::Format::eD24UnormS8Uint},
    vk::ImageTiling::eOptimal,
    vk::FormatFeatureFlagBits::eDepthStencilAttachment
  );
}

void App::createDepthResources()
{
  vk::Format depthFormat = findDepthFormat();
  createImage(
    swapChainExtent.width,
    swapChainExtent.height,
    depthFormat,
    1,
    vk::ImageTiling::eOptimal,
    vk::ImageUsageFlagBits::eDepthStencilAttachment,
    vk::MemoryPropertyFlagBits::eDeviceLocal,
    depthImage,
    depthImageMemory
  );
  depthImageView = createImageView(depthImage, depthFormat, vk::ImageAspectFlagBits::eDepth, 1);
}

void App::createTextureImage(const char* texturePath)
{
  //std::clog << texturePath << std::endl;

  ktxTexture2* kTexture;
  auto result = ktxTexture2_CreateFromNamedFile(texturePath, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture);

  if (result != KTX_SUCCESS)
  {
    throw std::runtime_error("failed to load ktx texture image!");
  }

  auto texWidth = kTexture->baseWidth;
  auto texHeight = kTexture->baseHeight;
  auto mipLevels = kTexture->numLevels;
  auto imageSize = ktxTexture_GetDataSize(ktxTexture(kTexture));
  auto ktxTextureData = ktxTexture_GetData(ktxTexture(kTexture));

  vk::Format textureFormat = static_cast<vk::Format>(ktxTexture2_GetVkFormat(kTexture));

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
  memcpy(data, ktxTextureData, imageSize);
  stagingBufferMemory.unmapMemory();
  
  vk::raii::Image tempImage = nullptr;
  vk::raii::DeviceMemory tempMemory = nullptr;
  
  createImage(texWidth, texHeight, textureFormat, mipLevels, vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled, vk::MemoryPropertyFlagBits::eDeviceLocal, tempImage, tempMemory);
  
  textureImages.emplace_back(std::move(tempImage));
  textureImagesMemory.emplace_back(std::move(tempMemory));
  
  transitionImageLayout(textureImages.back(), vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal, mipLevels);
  copyBufferToImage(stagingBuffer, textureImages.back(), texWidth, texHeight, mipLevels, kTexture);
  transitionImageLayout(textureImages.back(), vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal, mipLevels);
  
  ktxTexture2_Destroy(kTexture);
  
  createTextureImageView(textureImages.back(), textureFormat, mipLevels);
}

[[nodiscard]] vk::raii::ImageView App::createImageView(const vk::raii::Image& image, vk::Format format, vk::ImageAspectFlags aspectFlags, uint32_t mipLevels) const
{
  vk::ImageViewCreateInfo viewInfo {
    .image = image,
    .viewType = vk::ImageViewType::e2D,
    .format = format,
    .subresourceRange = {
      .aspectMask = aspectFlags,
      .baseMipLevel = 0,
      .levelCount = mipLevels,
      .baseArrayLayer = 0,
      .layerCount = 1
    }
  };

  return vk::raii::ImageView(device, viewInfo);
}

void App::createTextureImageView(const vk::raii::Image& image, vk::Format format, uint32_t mipLevels)
{
  textureImageViews.emplace_back(createImageView(image, format, vk::ImageAspectFlagBits::eColor, mipLevels));
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
    .maxLod = vk::LodClampNone
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
  memcpy(dataStaging, vertices.data(), bufferSize);
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

void App::createIndexBuffers()
{
  for (auto& go : gameObjects)
  {
    vk::DeviceSize bufferSize = sizeof(go.indices[0]) * go.indices.size();
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
    memcpy(dataStaging, go.indices.data(), (size_t) bufferSize);
    stagingBufferMemory.unmapMemory();

    createBuffer(
      bufferSize,
      vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      go.indexBuffer,
      go.indexBufferMemory
    );

    copyBuffer(stagingBuffer, go.indexBuffer, bufferSize);
  }
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
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, MAX_FRAMES_IN_FLIGHT * gameObjects.size()),
    vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, MAX_FRAMES_IN_FLIGHT * gameObjects.size())
  };

  vk::DescriptorPoolCreateInfo poolInfo {
    .flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
    .maxSets = MAX_FRAMES_IN_FLIGHT * static_cast<uint32_t>(gameObjects.size()),
    .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
    .pPoolSizes = poolSizes.data()
  };

  descriptorPool = vk::raii::DescriptorPool(device, poolInfo);

  // Create descriptor pool for ImGui
  std::array imguiPoolSizes = {
    vk::DescriptorPoolSize(vk::DescriptorType::eSampler, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformTexelBuffer, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageTexelBuffer, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eStorageBufferDynamic, 10),
    vk::DescriptorPoolSize(vk::DescriptorType::eInputAttachment, 10)
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
  for (auto& go : gameObjects)
  {
    std::vector<vk::DescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, *descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo {
      .descriptorPool = static_cast<vk::DescriptorPool>(descriptorPool),
      .descriptorSetCount = static_cast<uint32_t>(layouts.size()),
      .pSetLayouts = layouts.data(),
    };
    go.descriptorSets.clear();
    go.descriptorSets = device.allocateDescriptorSets(allocInfo);

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
      vk::DescriptorBufferInfo bufferInfo {
        .buffer = static_cast<vk::Buffer>(uniformBuffers[i]),
        .offset = 0,
        .range = sizeof(UniformBufferObject)
      };

      vk::DescriptorImageInfo imageInfo {
        .sampler = static_cast<vk::Sampler>(textureSampler),
        .imageView = static_cast<vk::ImageView>(textureImageViews[go.prim.materialIndex.value()]),
        .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal
      };

      std::array descriptorWrites = {
        vk::WriteDescriptorSet{
          .dstSet = static_cast<vk::DescriptorSet>(go.descriptorSets[i]),
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = vk::DescriptorType::eUniformBuffer,
          .pBufferInfo = &bufferInfo
        },
        vk::WriteDescriptorSet{
          .dstSet = static_cast<vk::DescriptorSet>(go.descriptorSets[i]),
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

std::unique_ptr<vk::raii::CommandBuffer> App::beginSingleTimeCommands()
{
  vk::CommandBufferAllocateInfo allocInfo {
    .commandPool = commandPool,
    .level = vk::CommandBufferLevel::ePrimary,
    .commandBufferCount = 1
  };
  std::unique_ptr<vk::raii::CommandBuffer> commandBuffer = std::make_unique<vk::raii::CommandBuffer>(std::move(device.allocateCommandBuffers(allocInfo).front()));

  vk::CommandBufferBeginInfo beginInfo {
    .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
  };
  commandBuffer->begin(beginInfo);

  return commandBuffer;
}

void App::endSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const
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

  vk::ImageMemoryBarrier2 depthBarrier {
    .srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe,
    .srcAccessMask = {},
    .dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests,
    .dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead | vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
    .oldLayout = vk::ImageLayout::eUndefined,
    .newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
    .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
    .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
    .image = depthImage,
    .subresourceRange = {
      .aspectMask = vk::ImageAspectFlagBits::eDepth,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1
    }
  };

  vk::DependencyInfo depthDependencyInfo {
    .dependencyFlags = {},
    .imageMemoryBarrierCount = 1,
    .pImageMemoryBarriers = &depthBarrier
  };
  commandBuffers[currentFrame].pipelineBarrier2(depthDependencyInfo);

  vk::ClearValue clearColour = vk::ClearColorValue(0.0f, 0.0f, 0.0f, 1.0f);
  vk::ClearDepthStencilValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);
  vk::RenderingAttachmentInfo colourAttachmentInfo {
    .imageView = swapChainImageViews[imageIndex],
    .imageLayout = vk::ImageLayout::eAttachmentOptimal,
    .loadOp = vk::AttachmentLoadOp::eClear,
    .storeOp = vk::AttachmentStoreOp::eStore,
    .clearValue = clearColour,
  };

  vk::RenderingAttachmentInfo depthAttachmentInfo {
    .imageView = depthImageView,
    .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
    .loadOp = vk::AttachmentLoadOp::eClear,
    .storeOp = vk::AttachmentStoreOp::eDontCare,
    .clearValue = clearDepth,
  };
  
  vk::RenderingInfo renderingInfo {
    .renderArea = {
      .offset = {0, 0},
      .extent = swapChainExtent
    },
    .layerCount = 1,
    .colorAttachmentCount = 1,
    .pColorAttachments = &colourAttachmentInfo,
    .pDepthAttachment = &depthAttachmentInfo
  };
  
  commandBuffers[currentFrame].beginRendering(renderingInfo);
  
  commandBuffers[currentFrame].bindPipeline(
    vk::PipelineBindPoint::eGraphics,
    *graphicsPipeline
  );

  commandBuffers[currentFrame].setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f));
  commandBuffers[currentFrame].setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));
  
  commandBuffers[currentFrame].bindVertexBuffers(0, *vertexBuffer, {0});
  
  for (auto& go : gameObjects)
  {
    commandBuffers[currentFrame].bindIndexBuffer(*go.indexBuffer, 0, vk::IndexType::eUint32);
    commandBuffers[currentFrame].bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics,
      pipelineLayout,
      0,
      *go.descriptorSets[currentFrame],
      nullptr
    );
    commandBuffers[currentFrame].drawIndexed(go.indices.size(), 1, 0, 0, 0);
  }

  
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
  UniformBufferObject ubo{};
  ubo.view = camera.getViewMatrix();
  ubo.proj = glm::perspective(glm::radians(45.0f), static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height), 0.1f, 1000.0f);
  ubo.proj[1][1] *= -1;
  ubo.model = glm::rotate(camera.getRotationMatrix(), 0.0f, glm::vec3(0.0f, 0.0f, 1.0f));

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
      .pColorAttachmentFormats = reinterpret_cast<const vk::Format *>(&swapChainSurfaceFormat.format),
      .depthAttachmentFormat = findDepthFormat()
    };

    initInfo.PipelineRenderingCreateInfo = imguiPipelineInfo;

  if (ImGui_ImplVulkan_Init(&initInfo) != true)
  {
    throw std::runtime_error("failed to initialise ImGuiImplVulkan!");
  }
}

void App::loadAsset(std::filesystem::path path)
{
  fastgltf::Parser parser;
  constexpr auto options = fastgltf::Options::LoadExternalBuffers;
  auto data = fastgltf::GltfDataBuffer::FromPath(path);
  if (data.error() != fastgltf::Error::None)
  {
    throw std::runtime_error(std::string("failed to load ").append(path.string()));
  }
  else
  {
    std::clog << "loaded " << path << std::endl;
  }

  auto parsed = parser.loadGltf(data.get(), path.parent_path(), options);
  if (parsed.error() != fastgltf::Error::None)
  {
    throw std::runtime_error(std::string("failed to parse ").append(path.string()));
  }
  else
  {
    std::clog << "parsed " << path << std::endl;
  }

  if (fastgltf::validate(parsed.get()) != fastgltf::Error::None)
  {
    throw std::runtime_error(std::string("failed to validate ").append(path.string()));
  }
  else
  {
    std::clog << "validated " << path << std::endl;
  }

  asset = std::move(parsed.get());
}

void App::loadGeometry()
{
  for (auto& mesh : asset.meshes)
  {
    for (auto& p : mesh.primitives)
    {
      gameObjects.emplace_back(GameObject{});

      auto v_offset = vertices.size();

      if (p.indicesAccessor.has_value())
      {
        auto& accessor = asset.accessors[p.indicesAccessor.value()];
        gameObjects.back().indices.resize(accessor.count);
        fastgltf::iterateAccessorWithIndex<uint32_t>(
          asset, accessor, [&](uint32_t index, size_t idx)
            {
              gameObjects.back().indices[idx] = index + v_offset;
            }
        );
      }
      
      auto pos = p.findAttribute("POSITION");
      auto uv = p.findAttribute("TEXCOORD_0");
      // auto normal = p.findAttribute("NORMAL");
      
      if (pos != p.attributes.end())
      {
        //std::clog << pos->accessorIndex << " ";
        auto& accessor = asset.accessors[pos->accessorIndex];
        vertices.resize(v_offset + accessor.count);
        //std::clog << accessor.count << " ";
        
        fastgltf::iterateAccessorWithIndex<glm::vec3>(
          asset, accessor, [&](glm::vec3 position, uint32_t idx)
          {
            vertices[idx + v_offset].pos = position / 500.0f;
          }
        );
      }
      
      if (uv != p.attributes.end())
      {  
        auto& accessor = asset.accessors[uv->accessorIndex];
        // std::clog << uv->accessorIndex << " ";
        //std::clog << accessor.count << " ";
        fastgltf::iterateAccessorWithIndex<glm::vec2>(
          asset, accessor, [&](glm::vec2 uv, uint32_t idx)
          {
            vertices[idx + v_offset].texCoord = uv;
          }
        );
      }

      gameObjects.back().prim = std::move(p);
    }
  }
}

void App::loadTextures(std::filesystem::path path)
{
  textureImages.clear();
  textureImagesMemory.clear();
  textureImageViews.clear();
  for (auto& material : asset.materials)
  {
    fastgltf::Image image = asset.images[asset.textures[material.pbrData.baseColorTexture->textureIndex].imageIndex.value()];
    std::visit(fastgltf::visitor {
      [](auto& args) { (void)args; },
      [&](fastgltf::sources::URI& filePath)
      {
        const std::string texturePath = path.parent_path().append(filePath.uri.path().begin(), filePath.uri.path().end()).string();
        createTextureImage(texturePath.c_str());
      }
    },
    image.data);
  }
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
  glfwSetCursorPosCallback(pWindow, cursor_pos_callback);
  glfwSetMouseButtonCallback(pWindow, mouse_button_callback);
  
  glfwSetWindowUserPointer(pWindow, this);
  glfwSetInputMode(pWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwSetInputMode(pWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
}

void App::initVulkan()
{
  createInstance();
  setupDebugMessenger();
  createSurface();
  pickPhysicalDevice();
  checkFeatureSupport();
  createLogicalDevice();
  createSwapChain();
  createImageViews();
  createDescriptorSetLayout();
  createGraphicsPipeline();
  createCommandPool();
  createDepthResources();
  loadAsset(static_cast<std::filesystem::path>(MODEL_PATH));
  loadTextures(static_cast<std::filesystem::path>(MODEL_PATH));
  createTextureSampler();
  loadGeometry();
  createVertexBuffer();
  createIndexBuffers();
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
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  queue = nullptr;

  surface = nullptr;
  swapChain = nullptr;
  swapChainImages.clear();

  swapChainImageViews.clear();

  descriptorSetLayout = nullptr;

  pipelineLayout = nullptr;
  graphicsPipeline = nullptr;
  
  commandPool = nullptr;
  commandBuffers.clear();
  textureImageViews.clear();
  textureImagesMemory.clear();
  textureImages.clear();
  textureSampler = nullptr;

  depthImage = nullptr;
  depthImageMemory = nullptr;
  depthImageView = nullptr;

  vertexBuffer = nullptr;
  vertexBufferMemory = nullptr;

  uniformBuffers;
  uniformBuffersMemory;

  descriptorPool = nullptr;
  imguiDescriptorPool = nullptr;

  presentCompleteSemaphores.clear();
  renderFinishedSemaphores.clear();
  inFlightFences.clear();

  device = nullptr;
  physicalDevice = nullptr;
  debugMessenger = nullptr;
  instance = nullptr;

  glfwDestroyWindow(pWindow);
  glfwTerminate();
}

void App::mainLoop()
{
  static bool showWindow = true;
  float deltaMultiplier = 1000000.0f;
  for (auto& go : gameObjects)
  {
    stats.tris += go.indices.size();
  }
  stats.tris /= 3;
  camera.update(1.0f);
  double xpos, ypos;
  while (glfwWindowShouldClose(pWindow) != GLFW_TRUE)
  {
    glfwPollEvents();
    glfwGetCursorPos(pWindow, &xpos, &ypos);
    
    camera.update((float)stats.frametime / deltaMultiplier);
    if (xpos == camera.oldXpos)
      camera.deltaYaw = 0.0f;
    if (ypos == camera.oldYpos)
      camera.deltaPitch = 0.0f;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    {
      ImGui::Begin("Delta Frametime", &showWindow, ImGuiWindowFlags_AlwaysAutoResize);
      ImGui::Text("%lius", stats.frametime);
      ImGui::Text("%i tris", stats.tris);
      ImGui::Spacing();
      ImGui::SliderFloat("Cam X", &camera.position.x, -3.0f, 3.0f);
      ImGui::SliderFloat("Cam Y", &camera.position.y, -3.0f, 3.0f);
      ImGui::SliderFloat("Cam Z", &camera.position.z, -3.0f, 3.0f);
      ImGui::SliderFloat("Move Speed", &camera.moveSpeed, 0.01f, 5.0f);
      ImGui::Spacing();
      ImGui::SliderFloat("Rot Pitch", &camera.pitch, -glm::pi<float>(), glm::pi<float>());
      ImGui::SliderFloat("Rot Yaw", &camera.yaw, -glm::pi<float>(), glm::pi<float>());
      ImGui::SliderFloat("Rot Speed", &camera.rotSpeed, 0.01f, 5.0f);
      ImGui::Spacing();
      ImGui::SliderFloat("Shift Speed", &camera.shiftSpeed, 0.01f, 4.0f);
      ImGui::InputFloat("Delta Mult", &deltaMultiplier);
      ImGui::End();
    }

    ImGui::Render();

    auto start = std::chrono::system_clock::now();
    drawFrame();
    auto end = std::chrono::system_clock::now();
    stats.frametime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  }
  device.waitIdle();
}

void App::run()
{
  initWindow();
  initVulkan();
  initImGui();
  mainLoop();
  cleanup();
}