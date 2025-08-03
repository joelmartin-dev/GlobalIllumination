#include "app.hpp"

#include <stdexcept>
#include <vector>
#include <iostream>
#include <algorithm>
#include <map>

#ifndef _APP_HPP_
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#if defined(__CLANGD__) || defined(__INTELLISENSE__)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include <vulkan/vk_platform.h>
#endif


void App::initWindow()
{
  if (glfwInit() != GLFW_TRUE)
  {
    throw std::runtime_error("failed to initialise GLFW!");
  }

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

  pWindow = glfwCreateWindow(WIDTH, HEIGHT, "App", nullptr, nullptr);

  if (pWindow == nullptr)
  {
    throw std::runtime_error("failed to create GLFWwindow!");
  }

};

void App::initVulkan()
{
  createInstance();
  setupDebugMessenger();
  pickPhysicalDevice();
};

std::vector<const char*> App::getRequiredExtensions()
{
  uint32_t glfwExtensionCount = 0;
  auto glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
  if (enableValidationLayers)
  {
    extensions.push_back(vk::EXTDebugUtilsExtensionName);
  }

  return extensions;
};

void App::createInstance()
{
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
  if (std::ranges::any_of(requiredLayers, [&layerProperties](auto const& requiredLayer)
    {
      return std::ranges::none_of(layerProperties, [requiredLayer](auto const& layerProperty)
        { return strcmp(layerProperty.layerName, requiredLayer) == 0; }); 
    }
  ))
  {
    throw std::runtime_error("one or more required layers are not supported!");
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
};

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
};


void App::pickPhysicalDevice()
{
  std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
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
  }
  else
  {
    throw std::runtime_error( "failed to find a suitable GPU!" );
  }
};

uint32_t App::findQueueFamilies(const vk::PhysicalDevice& _physicalDevice)
{
  std::vector<vk::QueueFamilyProperties> queueFamilyProperties = _physicalDevice.getQueueFamilyProperties();

  auto graphicsQueueFamilyProperty = std::find_if(queueFamilyProperties.begin(), queueFamilyProperties.end(), [](vk::QueueFamilyProperties const& qfp)
    {
      return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics);
    }
  );

  // return the index of the queue with a graphics queue family
  return static_cast<uint32_t>(std::distance(queueFamilyProperties.begin(), graphicsQueueFamilyProperty));
};

void App::createLogicalDevice()
{
  std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
  uint32_t graphicsIndex = findQueueFamilies(physicalDevice);
  float queuePriority = 0.0f;

  vk::DeviceQueueCreateInfo deviceQueueCreateInfo {
    .queueFamilyIndex = graphicsIndex,
    .queueCount = 1,
    .pQueuePriorities = &queuePriority
  };

  vk::StructureChain<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT> featureChain = {
    {},
    {.dynamicRendering = true},
    {.extendedDynamicState = true}
  };

  vk::DeviceCreateInfo deviceCreateInfo {
    .pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>(),
    .queueCreateInfoCount = 1,
    .pQueueCreateInfos = &deviceQueueCreateInfo,
    .enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size()),
    .ppEnabledExtensionNames = requiredDeviceExtensions.data()
  };

  device = vk::raii::Device(physicalDevice, deviceCreateInfo);
  graphicsQueue = vk::raii::Queue(device, graphicsIndex, 0);
};

void App::cleanup()
{
  glfwDestroyWindow(pWindow);
  glfwTerminate();
};

void App::mainLoop()
{
  while (glfwWindowShouldClose(pWindow) != GLFW_TRUE)
  {
    glfwPollEvents();
  }
};

void App::run()
{
  initWindow();
  initVulkan();
  mainLoop();
  cleanup();
};