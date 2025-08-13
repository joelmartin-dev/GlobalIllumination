#ifndef _APP_HPP_
#define _APP_HPP_

#include <vector>
#include <iostream>
#include <fstream>
#include <array>

#include <volk/volk.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(__CLANGD__) || defined(__INTELLISENSE__)
#include <vulkan/vulkan_raii.hpp>
#else
import vulkan_hpp;
#endif

//#include <vulkan/vk_platform.h>

constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

const std::vector validationLayers = {
  "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

#include <glm/glm.hpp>

#include "camera.hpp"

struct Vertex {
  // Attributes
  glm::vec2 pos;
  glm::vec3 colour;
  glm::vec2 texCoord;

  // How the struct is passed
  static vk::VertexInputBindingDescription getBindingDescription()
  {
    return {0, sizeof(Vertex), vk::VertexInputRate::eVertex};
  }

  // How the struct's data is laid out
  static std::array<vk::VertexInputAttributeDescription, 3> getAttributeDescriptions()
  {
    return {
      // location, binding, format, offset
      // Binding is 0, as we decided in getBindingDescription
      // Formats are aliases for in-shader data types, e.g. R32Sfloat is float, R64Sfloat is double
      vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, pos)),
      vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, colour)),
      vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord))
    };
  }
};

struct UniformBufferObject {
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
};

const std::vector<Vertex> vertices = {
  {{-0.5f, -0.5f}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},  
  {{0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
  {{0.5f, 0.5f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
  {{-0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
};

const std::vector<uint16_t> indices = {
  0, 1, 2, 2, 3, 0
};

#ifndef MODEL_PATH
#define MODEL_PATH "../assets/models/Sponza.gltf"
#endif

#ifndef TEXTURE_PATH
#define TEXTURE_PATH "../assets/textures/texture.jpg"
#endif

static Camera camera;
static bool changeInputMode = true;

class App
{
  public:
  void run();
  private:
  // Class Variables
  GLFWwindow* pWindow = nullptr;
  
  static int xpos, ypos;

  vk::raii::Context context;
  vk::raii::Instance instance = nullptr;
  vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
  
  std::vector<const char*> requiredDeviceExtensions = {
    vk::KHRSwapchainExtensionName,
    vk::KHRDynamicRenderingExtensionName,
    vk::KHRSpirv14ExtensionName,
    vk::KHRSynchronization2ExtensionName,
    vk::KHRCreateRenderpass2ExtensionName
  };
  
  vk::raii::PhysicalDevice physicalDevice = nullptr;
  vk::raii::Device device = nullptr;
  
  uint32_t graphicsIndex = ~0;
  uint32_t computeIndex = ~0;
  vk::raii::Queue queue = nullptr;
  
  vk::raii::SurfaceKHR surface = nullptr;
  vk::SurfaceFormatKHR swapChainSurfaceFormat;
  vk::Extent2D swapChainExtent;
  vk::raii::SwapchainKHR swapChain = nullptr;
  std::vector<vk::Image> swapChainImages;
  
  std::vector<vk::raii::ImageView> swapChainImageViews;
  
  vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;

  vk::raii::PipelineLayout pipelineLayout = nullptr;
  vk::raii::Pipeline graphicsPipeline = nullptr;
  vk::SampleCountFlagBits msaaSamples = vk::SampleCountFlagBits::e1;
  
  vk::raii::CommandPool commandPool = nullptr;
  std::vector<vk::raii::CommandBuffer> commandBuffers;
  vk::raii::Image textureImage = nullptr;
  vk::raii::DeviceMemory textureImageMemory = nullptr;
  vk::raii::ImageView textureImageView = nullptr;
  vk::raii::Sampler textureSampler = nullptr;

  vk::raii::Buffer vertexBuffer = nullptr;
  vk::raii::DeviceMemory vertexBufferMemory = nullptr;
  vk::raii::Buffer indexBuffer = nullptr;
  vk::raii::DeviceMemory indexBufferMemory = nullptr;

  std::vector<vk::raii::Buffer> uniformBuffers;
  std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
  std::vector<void*> uniformBuffersMapped;

  vk::raii::DescriptorPool descriptorPool = nullptr;
  vk::raii::DescriptorPool imguiDescriptorPool = nullptr;
  std::vector<vk::raii::DescriptorSet> descriptorSets;
  

  std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
  std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
  std::vector<vk::raii::Fence> inFlightFences;
  bool frameBufferResized = false;
  uint32_t currentFrame = 0;
  uint32_t semaphoreIndex = 0;
  
  
  // Static Variables
  
  // Class Functions
  void initWindow();
  
  void initVulkan();
  void createInstance();
  void setupDebugMessenger();
  void createSurface();
  vk::SampleCountFlagBits getMaxUsableSampleCount();
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createSwapChain();
  void createImageViews();
  void recreateSwapChain();
  void createDescriptorSetLayout();
  void createGraphicsPipeline();
  [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char>& code) const {
    vk::ShaderModuleCreateInfo createInfo {
      .codeSize = code.size() * sizeof(char),
      .pCode = reinterpret_cast<const uint32_t*>(code.data())
    };
    
    vk::raii::ShaderModule shaderModule{device, createInfo};
    
    return shaderModule;
  };
  void createCommandPool();
  uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
  void createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Buffer& buffer, vk::raii::DeviceMemory& bufferMemory);
  void copyBuffer(vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer, vk::DeviceSize size);
  void copyBufferToImage(const vk::raii::Buffer& buffer, vk::raii::Image& image, uint32_t width, uint32_t height);
  void createImage(uint32_t width, uint32_t height, vk::Format format, vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags properties, vk::raii::Image& image, vk::raii::DeviceMemory& imageMemory);
  void transitionTextureImageLayout(const vk::raii::Image& _textureImage, vk::ImageLayout oldLayout, vk::ImageLayout newLayout);
  void createTextureImage();
  vk::raii::ImageView createImageView(vk::raii::Image& image, vk::Format format);
  void createTextureImageView();
  void createTextureSampler();
  void createVertexBuffer();
  void createIndexBuffer();
  void createUniformBuffers();
  void createDescriptorPools();
  void createDescriptorSets();
  void createCommandBuffers();
  void transitionImageLayout(
    uint32_t imageIndex,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    vk::AccessFlags2 srcAccessMask,
    vk::AccessFlags2 dstAccessMask,
    vk::PipelineStageFlags2 srcStageMask,
    vk::PipelineStageFlags2 dstStageMask
  );
  vk::raii::CommandBuffer beginSingleTimeCommands();
  void endSingleTimeCommands(vk::raii::CommandBuffer& commandBuffer);
  void recordCommandBuffer(uint32_t imageIndex);
  void createSyncObjects();
  
  void initImGui();
  
  void initSponza();

  void mainLoop();
  void updateUniformBuffer(uint32_t imageIndex);
  void drawFrame();
  
  void cleanupSwapChain();
  void cleanup();
  
  // Static Functions
  static void key_callback(GLFWwindow* _pWindow, int key, int scancode, int action, int mods)
  {
    // unused
    (void)scancode; (void) mods;
    
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
      glfwSetWindowShouldClose(_pWindow, GLFW_TRUE);
    }

    if (action == GLFW_REPEAT || action == GLFW_PRESS)
    {
      //std::clog << "PRESSING" << std::endl;
      switch (key)
      {
        case GLFW_KEY_W:
          camera.velocity.z = -1.0f;
          break;
        case GLFW_KEY_A:
          camera.velocity.x = -1.0f;
          break;
        case GLFW_KEY_S:
          camera.velocity.z = 1.0f;
          break;
        case GLFW_KEY_D:
          camera.velocity.x = 1.0f;
          break;
        default:
          break;
      }
    }

    if (action == GLFW_RELEASE)
    {
      //std::clog << "RELEASING" << std::endl;
      switch (key)
      {
        case GLFW_KEY_W:
          camera.velocity.z = 0.0f;
          break;
        case GLFW_KEY_A:
          camera.velocity.x = 0.0f;
          break;
        case GLFW_KEY_S:
          camera.velocity.z = 0.0f;
          break;
        case GLFW_KEY_D:
          camera.velocity.x = 0.0f;
          break;
        default:
          break;
      }
    }

  };
  
  static void mouse_button_callback(GLFWwindow* _pWindow, int button, int action, int mods)
  {
    (void) button; (void) _pWindow; (void) action; (void) mods;
    // if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && changeInputMode)
    // {
    //   changeInputMode = false;
    //   if (glfwGetInputMode(_pWindow, GLFW_CURSOR) == GLFW_TRUE)
    //   {
    //     glfwSetInputMode(_pWindow, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
    //     glfwSetInputMode(_pWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    //   }
    //   else
    //   {
    //     glfwSetInputMode(_pWindow, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    //     glfwSetInputMode(_pWindow, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    //   }
    // }

    // if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) changeInputMode = true;
  }

  static void scroll_callback(GLFWwindow* _pWindow, double xoffset, double yoffset)
  {
    (void) _pWindow; (void) xoffset; (void) yoffset;
  }

  static void framebufferResizeCallback(GLFWwindow* _pWindow, int width, int height)
  {
    (void) width; (void) height;
    auto app = reinterpret_cast<App*>(glfwGetWindowUserPointer(_pWindow));
    app->frameBufferResized = true;
  }

  static VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT severity, vk::DebugUtilsMessageTypeFlagsEXT type, const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData, void*)
  {
    if (severity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
    {
      std::cerr << "validation layer: type " << to_string(type) << " msg: " << pCallbackData->pMessage << std::endl;
    }

    return vk::False;
  }

  static std::vector<char> readFile(const std::string& fileName)
  {
    std::ifstream file(fileName, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
      throw std::runtime_error("failed to open file!");
    }

    std::vector<char> buffer(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();
    return buffer;
  }
};

#endif