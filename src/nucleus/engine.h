#pragma once

#include "forma/mesh.h"
#include "lumen/pipeline.h"
#include "lumen/shader_registry.h"
#include "memoria/allocator.h"
#include "memoria/asset_manager.h"
#include "lumen/descriptor_manager.h"
#include "lumen/ibl_generator.h"
#include "memoria/sampler.h"
#include "memoria/texture.h"
#include "mundus/scene.h"
#include "petra/window.h"
#include "render/context.h"
#include "sensus/input.h"
#include "auxilia/toon.hpp"
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
  void resolveSceneMeshes();
  void syncMaterialIndices(std::shared_ptr<Forma::Material> mat);

  void drawFrame();
  void updateBindlessDescriptorSet();
  std::vector<std::string> getShaderNames() const;

  void updateIBLDescriptors();
  void updateSkyboxDescriptor();
  void recreateSwapchain();
  void cleanupSwapchainResources();
  void pickEntityAtMouse();

  // Nucleus: Plane of Control
  // Members are destroyed in REVERSE order of declaration.
  // Window must outlive Render Context (Vulkan Surface/Wayland connection).
  // Render Context must outlive Allocator (VmaAllocator).
  std::unique_ptr<Petra::Window> m_window;
  std::unique_ptr<Render::Context> m_renderCtx;
  std::unique_ptr<Memoria::Allocator> m_allocator;

  std::unique_ptr<Memoria::AssetManager> m_assetManager;
  std::unique_ptr<Lumen::ShaderRegistry> m_shaderRegistry;
  std::unique_ptr<Vigil::Overlay> m_overlay;

  Mundus::Scene m_scene;
  std::unique_ptr<Vista::Camera> m_camera;
  std::unique_ptr<Sensus::Input> m_input;

  std::unique_ptr<Lumen::DescriptorManager> m_descriptors;

  struct SkyboxOption {
    std::string name;
    std::string pipelineName;
    std::shared_ptr<Memoria::TextureAsset> texture;
    std::string sourcePath; // path to source file for IBL regeneration
  };
  std::vector<SkyboxOption> m_skyboxOptions;
  uint32_t m_selectedSkybox = 0;
  int m_skyboxEntityIndex = -1;
  std::shared_ptr<Memoria::TextureAsset> m_skyboxTexture;
  Lumen::IBLMaps m_iblMaps;

  // Memoria::Sampler is still used for the default sampler
  Memoria::Sampler m_sampler;

  VkCommandPool m_cmdPool = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> m_framebuffers;
  std::vector<VkCommandBuffer> m_cmdBufs;

  std::vector<VkSemaphore> m_imageAvail;
  std::vector<VkSemaphore> m_renderDone;
  std::vector<VkFence> m_inFlight;

  uint32_t m_frameIndex = 0;
  uint32_t m_debugMode = 0;
  Auxilia::toon_doc m_config;
  bool m_needsResize = false;
  bool m_isRunning = true;

  uint64_t m_startCount = 0;
  uint64_t m_perfFreq = 0;
};

} // namespace Nucleus
