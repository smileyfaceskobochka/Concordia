#pragma once

#include "forma/mesh.h"
#include "lumen/pipeline.h"
#include "lumen/shader_registry.h"
#include "memoria/allocator.h"
#include "memoria/asset_manager.h"
#include "memoria/sampler.h"
#include "memoria/texture.h"
#include "mundus/scene.h"
#include "petra/window.h"
#include "render/context.h"
#include "sensus/input.h"
#include "vigil/overlay.h"
#include "vista/camera.h"
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace Nucleus {

class Engine {
public:
  Engine();
  ~Engine();

  Engine(const Engine &) = delete;
  Engine &operator=(const Engine &) = delete;

  void run();

private:
  void initPipeline();
  void initFramebuffers();
  void initCommands();
  void initSync();
  void initMesh();
  void initDescriptors();

  void drawFrame();
  void recreateSwapchain();
  void cleanupSwapchainResources();

  std::unique_ptr<Memoria::Allocator> m_allocator;
  std::unique_ptr<Render::Context> m_renderCtx;
  std::unique_ptr<Memoria::AssetManager> m_assetManager;
  std::unique_ptr<Lumen::ShaderRegistry> m_shaderRegistry;
  std::unique_ptr<Vigil::Overlay> m_overlay;

  Mundus::Scene m_scene;
  std::unique_ptr<Vista::Camera> m_camera;
  std::unique_ptr<Sensus::Input> m_input;
  std::unique_ptr<Petra::Window> m_window;

  VkDescriptorSetLayout m_globalDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_materialDescriptorLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet m_globalDescriptorSet = VK_NULL_HANDLE;

  struct GlobalUBO {
    glm::vec4 lightDir;
    glm::vec4 viewPos;
    glm::vec4 lightColor;
    glm::mat4 view;
    glm::mat4 proj;
    float exposure = 1.0f;
    float gamma = 2.2f;
    float padding[2];
  };

  std::shared_ptr<Memoria::TextureAsset> m_skyboxTexture;

  // Memoria::Sampler is still used for the default sampler
  Memoria::Sampler m_sampler;

  VkBuffer m_globalUBO = VK_NULL_HANDLE;
  VmaAllocation m_globalUBOAlloc = VK_NULL_HANDLE;
  void *m_globalUBOMapped = nullptr;

  VkCommandPool m_cmdPool = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> m_framebuffers;
  std::vector<VkCommandBuffer> m_cmdBufs;

  std::vector<VkSemaphore> m_imageAvail;
  std::vector<VkSemaphore> m_renderDone;
  std::vector<VkFence> m_inFlight;

  uint32_t m_frameIndex = 0;
  bool m_needsResize = false;
  bool m_isRunning = true;

  uint64_t m_startCount = 0;
  uint64_t m_perfFreq = 0;
};

} // namespace Nucleus
