#ifndef _APP_HPP_
#define _APP_HPP_

#include <vector> // resizable container
#include <array> // for c++-like syntax of user-type arrays
#include <filesystem> // for platform-agnostic paths

// Windows has different calling conventions, vk_platform defines alternatives
#include <vulkan/vk_platform.h>

// Meta-loader for vulkan functions
// volk loads function pointers at runtime directly from the driver
// rather than linking all of that information in the executable
#include <volk/volk.h>

// C++ bindings and RAII definitions for vulkan
#include <vulkan/vulkan_raii.hpp>

//#include <vulkan/vulkan_profiles.hpp> // provided profiles not compatible with some target hardware

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h> // for callback declarations, pWindow member

// OpenGL Mathematics: for linear algebra functions
#include <glm/glm.hpp>
#include <glm/ext/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

// for declaring fastgltf members
#include "fastgltf/types.hpp"

// for ktxTexture2 type
#include "ktx.h"

// for camera member (stores universal uniform buffer)
#include "camera.hpp"

// constexpr allows for explicit typing (vs const)
constexpr uint32_t WIDTH = 800;
constexpr uint32_t HEIGHT = 600;

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

// path to gltf, can be defined through compile-line preprocessor
#ifndef MODEL_PATH
#define MODEL_PATH ""
#endif

// path to spv, can be defined through compile-line preprocessor
#ifndef SHADER_PATH
#define SHADER_PATH "../assets/shaders/shader.spv"
#endif

// not array, implicit typing and contents are immutable
const std::vector validationLayers = {
  "VK_LAYER_KHRONOS_validation"
};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

// stores measurements data
struct EngineStats {
  long int frametime = 0L;
  uint32_t tris = 0U;
  uint32_t drawcalls = 0U;
  long int sceneUpdateTime = 0L;
  long int meshDrawTime = 0L;
};

struct Vertex {
  // Attributes
  glm::vec3 pos;
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
      vk::VertexInputAttributeDescription(0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)),
      vk::VertexInputAttributeDescription(1, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, colour)),
      vk::VertexInputAttributeDescription(2, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, texCoord))
    };
  }

  // equal_to function, needed for use of Vertex as Key in unordered containers e.g. unordered_map(Key, T, hash(Key), equal_to(Key))
  bool operator==(const Vertex& other) const
  {
    return pos == other.pos && colour == other.colour && texCoord == other.texCoord;
  }
};

// Hash function, needed for use of Vertex as Key in unordered containers e.g. unordered_map(Key, T, hash(Key), equal_to(Key))
template<> struct std::hash<Vertex> {
  size_t operator()(Vertex const& vertex) const noexcept
  {
    return ((hash<glm::vec3>()(vertex.pos) ^
            (hash<glm::vec3>()(vertex.colour) << 1 )) >> 1) ^
            (hash<glm::vec2>()(vertex.texCoord) << 1);
  }
};

// need to keep byte alignment in mind when defining probe and ray data structures
// Model, View, Projection uniform buffer object struct
struct MVP {
  alignas(16) glm::mat4 model;
  alignas(16) glm::mat4 view;
  alignas(16) glm::mat4 proj;
};

// stores the unique data of each primitive in a gltf
struct PrimData {
  fastgltf::Mesh* parent;

  std::vector<uint32_t> indices;
  
  vk::raii::Buffer indexBuffer = nullptr;
  vk::raii::DeviceMemory indexBufferMemory = nullptr;
  
  size_t imageViewIndex;

  std::vector<vk::raii::DescriptorSet> descriptorSets;
};

struct MeshData {
  glm::vec3 translation = glm::vec3(0.0f);
  glm::vec3 rotation = glm::vec3(0.0f);
  glm::vec3 scale = glm::vec3(1.0f);

  glm::mat4 getModelMatrix() const 
  {
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, translation);
    model = glm::rotate(model, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::scale(model, scale);
    return model;
  }
};

static Camera camera = {};
static bool frameBufferResized = false;

class App
{
  public:
  void run();
  private:
  // Class Variables
  GLFWwindow* pWindow = nullptr;
  

  static int xpos, ypos;
  
  EngineStats stats;
  
  std::vector<Vertex> vertices;

  fastgltf::Asset asset;

  std::vector<MeshData> meshes;
  std::vector<PrimData> prims;

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
  vk::Format swapChainSurfaceFormat;
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
  std::vector<vk::raii::Image> textureImages;
  std::vector<vk::raii::DeviceMemory> textureImagesMemory;
  std::vector<vk::raii::ImageView> textureImageViews;
  vk::raii::Sampler textureSampler = nullptr;

  vk::raii::Image depthImage = nullptr;
  vk::raii::DeviceMemory depthImageMemory = nullptr;
  vk::raii::ImageView depthImageView = nullptr;

  vk::raii::Buffer vertexBuffer = nullptr;
  vk::raii::DeviceMemory vertexBufferMemory = nullptr;

  std::vector<vk::raii::Buffer> uniformBuffers;
  std::vector<vk::raii::DeviceMemory> uniformBuffersMemory;
  std::vector<void*> uniformBuffersMapped;

  vk::raii::DescriptorPool descriptorPool = nullptr;
  vk::raii::DescriptorPool imguiDescriptorPool = nullptr;

  std::vector<vk::raii::Semaphore> presentCompleteSemaphores;
  std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
  std::vector<vk::raii::Fence> inFlightFences;
  uint32_t currentFrame = 0;
  uint32_t semaphoreIndex = 0;

  struct SwapChainSupportDetails {
    vk::SurfaceCapabilitiesKHR capabilities;
    std::vector<vk::SurfaceFormatKHR> formats;
    std::vector<vk::PresentModeKHR> presentModes;
  };

  // Class Functions
  void initWindow();
  
  void initVulkan();
  void createInstance();
  void setupDebugMessenger();
  void createSurface();
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createSwapChain();
  void createSwapChainImageViews();
  void createDescriptorSetLayout();
  void createGraphicsPipeline();
  [[nodiscard]] vk::raii::ShaderModule createShaderModule(const std::vector<char>& code) const;
  [[nodiscard]] vk::Format findDepthFormat() const;
  vk::Format findSupportedFormat(
    const std::vector<vk::Format>& candidates, 
    vk::ImageTiling, 
    vk::FormatFeatureFlags features
  ) const;
  void createCommandPool();
  void createDepthResources();
  void createImage(
    uint32_t width, uint32_t height, uint32_t mipLevels, 
    vk::Format format,
    vk::ImageTiling tiling,
    vk::ImageUsageFlags usage,
    vk::MemoryPropertyFlags properties,
    vk::raii::Image& image,
    vk::raii::DeviceMemory& imageMemory
  );
  uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);
  [[nodiscard]] vk::raii::ImageView createImageView(
    const vk::raii::Image& image,
    vk::Format format,
    vk::ImageAspectFlags aspectFlags,
    uint32_t mipLevels
  ) const;
  void loadAsset(std::filesystem::path path);
  void loadTextures(std::filesystem::path path);
  void createTextureImage(const char* texturePath);
  void createBuffer(
    vk::DeviceSize size,
    vk::BufferUsageFlags usage,
    vk::MemoryPropertyFlags properties,
    vk::raii::Buffer& buffer,
    vk::raii::DeviceMemory& bufferMemory
  );
  void transitionImageLayout(
    const vk::raii::Image& image,
    vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
    uint32_t mipLevels
  );
  std::unique_ptr<vk::raii::CommandBuffer> beginSingleTimeCommands();
  void endSingleTimeCommands(const vk::raii::CommandBuffer& commandBuffer) const;
  void copyBufferToImage(
    const vk::raii::Buffer& buffer,
    const vk::raii::Image& image,
    uint32_t width, uint32_t height, uint32_t mipLevels,
    ktxTexture2* kTexture
  );
  void createTextureImageView(
    const vk::raii::Image& image, 
    vk::Format format, 
    uint32_t mipLevels
  );
  void createTextureSampler();
  void loadGeometry();
  void copyBuffer(
    vk::raii::Buffer& srcBuffer, vk::raii::Buffer& dstBuffer,
    vk::DeviceSize size
  );
  void createVertexBuffer();
  void createIndexBuffers();
  void createUniformBuffers();
  void createDescriptorPools();
  void createDescriptorSets();
  void createCommandBuffers();
  void createSyncObjects();

  void initImGui();
  
  void mainLoop();
  void drawFrame();
  void recreateSwapChain();
  void cleanupSwapChain();
  void updateModelViewProjection(uint32_t imageIndex);
  void transitionImageLayout(
    uint32_t imageIndex,
    vk::ImageLayout oldLayout,
    vk::ImageLayout newLayout,
    vk::AccessFlags2 srcAccessMask,
    vk::AccessFlags2 dstAccessMask,
    vk::PipelineStageFlags2 srcStageMask,
    vk::PipelineStageFlags2 dstStageMask
  );
  void recordCommandBuffer(uint32_t imageIndex);
  
  void cleanup();
  
  static void key_callback(GLFWwindow* _pWindow, int key, int scancode, int action, int mods)
  {
    camera.key_callback(_pWindow, key, scancode, action, mods);
  }
  
  static void cursor_pos_callback(GLFWwindow* _pWindow, double xpos, double ypos)
  {
    if (glfwGetInputMode(_pWindow, GLFW_CURSOR) == GLFW_CURSOR_DISABLED)
    {
      camera.cursor_pos_callback(xpos, ypos);
    }
  }
  
  static void framebufferResizeCallback(GLFWwindow* _pWindow, int width, int height)
  {
    frameBufferResized = true;
  }
};


#endif