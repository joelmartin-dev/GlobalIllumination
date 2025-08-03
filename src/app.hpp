#ifndef _APP_HPP_
#define _APP_HPP_

#include <vector>
#include <iostream>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#if defined(__CLANGD__) || defined(__INTELLISENSE__)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

#include <vulkan/vk_platform.h>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

const std::vector validationLayers = {
  "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

class App
{
 public:
  void run();
 private:
  // Class Variables
  GLFWwindow* pWindow = nullptr;

  vk::raii::Context context;
  vk::raii::Instance instance = nullptr;
  vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;

  std::vector<const char*> requiredDeviceExtensions = {
    vk::KHRSwapchainExtensionName,
    vk::KHRSpirv14ExtensionName,
    vk::KHRSynchronization2ExtensionName,
    vk::KHRCreateRenderpass2ExtensionName
  };

  vk::raii::PhysicalDevice physicalDevice = nullptr;
  vk::raii::Device device = nullptr;

  vk::raii::Queue graphicsQueue = nullptr;

  // Static Variables

  // Class Functions
  void initWindow();
  void initVulkan();
  std::vector<const char*> getRequiredExtensions();
  void createInstance();
  void setupDebugMessenger();
  void pickPhysicalDevice();
  uint32_t findQueueFamilies(const vk::PhysicalDevice& device);
  void createLogicalDevice();
  void mainLoop();
  void cleanup();

  // Static Functions
  static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*)
  {
    if (severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose)
    {
      std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;
    }

    return vk::False;
  }
};

#endif