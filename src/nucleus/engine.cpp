#define GLM_ENABLE_EXPERIMENTAL
#include "engine.h"
#include "forma/material.h"
#include "forma/mesh.h"
#include "lumen/pipeline.h"
#include "lumen/shader_registry.h"
#include "mundus/scene_pick.h"
#include "vista/camera.h"
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <iostream>
#include <render/vk_check.h>
#include <stdexcept>
#include <string>
#include <vk_mem_alloc.h>
#include <algorithm>
#include "vigil/editor_keys.h"

struct PushConstants {
  glm::mat4 model;
  glm::vec4 baseColor;
  float roughness;
  float metallic;
  uint32_t albedoIdx;
  uint32_t normalIdx;
  uint32_t mrIdx;
  uint32_t aoIdx;
  uint32_t emissiveIdx;
  uint32_t debugMode;
};

namespace Nucleus {

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

Engine::Engine() {
  m_config.load_file((std::string(CONCORDIA_ASSETS_DIR) + "/config/engine.toon").c_str());

  const char *title =
      m_config.get_string("window.title", "Concordia \xe2\x80\x93 Scene");
  int win_w = (int)m_config.get_number("window.width", 800.0);
  int win_h = (int)m_config.get_number("window.height", 600.0);

  m_window = std::make_unique<Petra::Window>(title, win_w, win_h);
  m_renderCtx = std::make_unique<Render::Context>(*m_window);
  m_allocator = std::make_unique<Memoria::Allocator>(
      m_renderCtx->getInstance(), m_renderCtx->getPhysicalDevice(),
      m_renderCtx->getDevice());
  m_overlay = std::make_unique<Vigil::Overlay>(*m_window, *m_renderCtx);
  m_input = std::make_unique<Sensus::Input>();
  float fov = (float)m_config.get_number("camera.fov", 45.0);
  float nearP = (float)m_config.get_number("camera.near", 0.1);
  float farP = (float)m_config.get_number("camera.far", 100.0);

  m_camera = std::make_unique<Vista::Camera>();
  m_camera->setPerspective(fov, (float)win_w / (float)win_h, nearP, farP);
  m_camera->mouseSensitivity =
      (float)m_config.get_number("camera.sensitivity", 0.1);
  m_camera->moveSpeed = (float)m_config.get_number("camera.speed", 5.0);

  m_debugMode = m_config.get_bool("renderer.debug_mode", false) ? 1 : 0;

  SDL_Log("Engine: Initializing depth buffer...");
  m_renderCtx->initDepthBuffer(m_allocator->getVma());

  m_perfFreq = SDL_GetPerformanceFrequency();
  m_startCount = SDL_GetPerformanceCounter();

  SDL_Log("Engine: Initializing commands...");
  initCommands();

  SDL_Log("Engine: Initializing asset manager...");
  m_assetManager = std::make_unique<Memoria::AssetManager>(
      *m_allocator, m_renderCtx->getDevice(), m_renderCtx->getGraphicsQueue(),
      m_cmdPool);

  m_descriptors = std::make_unique<Lumen::DescriptorManager>(
      m_renderCtx->getDevice(), *m_allocator);

  SDL_Log("Engine: Initializing descriptors...");
  initDescriptors();

  SDL_Log("Engine: Generating IBL maps...");
  m_iblMaps = Lumen::IBLGenerator::generate(
      *m_allocator, m_renderCtx->getDevice(),
      m_renderCtx->getGraphicsQueue(), m_cmdPool,
      std::string(CONCORDIA_ASSETS_DIR) +
          "/images/skybox/cubemap/Cubemap_Sky_01-512x512.png");
  if (m_iblMaps.irradianceMap) {
    m_descriptors->updateIBL(m_iblMaps.irradianceMap->view,
                              m_iblMaps.prefilterMap->view,
                              m_iblMaps.brdfLUT->view);
  }

  SDL_Log("Engine: Initializing shader registry...");
  initPipeline();
  SDL_Log("Engine: Initializing framebuffers...");
  initFramebuffers();
  SDL_Log("Engine: Initializing sync...");
  initSync();
  SDL_Log("Engine: Initializing mesh...");
  initMesh();

  // Final bindless update after ALL initial assets are loaded
  updateBindlessDescriptorSet();

  // Load saved scene if available, otherwise keep test scene from initMesh
  std::string scenePath =
      std::string(CONCORDIA_ASSETS_DIR) + "/scenes/default.toon";
  if (FILE *f = fopen(scenePath.c_str(), "r")) {
    fclose(f);
    m_scene.load(scenePath.c_str());
    SDL_Log("Engine: Loaded scene from %s", scenePath.c_str());
    resolveSceneMeshes();
  } else {
    SDL_Log("Engine: No saved scene found, using test scene");
  }

  SDL_Log("Engine: Constructor finished.");
}

Engine::~Engine() {
  std::string scenePath =
      std::string(CONCORDIA_ASSETS_DIR) + "/scenes/default.toon";
  m_scene.save(scenePath.c_str());

  VkDevice device = m_renderCtx->getDevice();
  if (device) {
    vkDeviceWaitIdle(device);

    // Explicitly release scene and assets before destroying allocator
    m_skyboxTexture.reset();
    m_scene.getEntities().clear();

    if (m_allocator) {
      m_descriptors.reset(); // destroys descriptor pool, layouts, UBO
      m_renderCtx->cleanupDepthBuffer(m_allocator->getVma());
    }

    if (m_assetManager) {
      m_assetManager.reset();
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      vkDestroyFence(device, m_inFlight[i], nullptr);
      vkDestroySemaphore(device, m_renderDone[i], nullptr);
      vkDestroySemaphore(device, m_imageAvail[i], nullptr);
    }

    vkDestroyCommandPool(device, m_cmdPool, nullptr);

    if (m_shaderRegistry) {
      m_shaderRegistry->destroy(device);
    }

    m_sampler.destroy(device);

    cleanupSwapchainResources();
  }
}

std::vector<std::string> Engine::getShaderNames() const {
  return m_shaderRegistry ? m_shaderRegistry->getPipelineNames()
                          : std::vector<std::string>();
}

void Engine::initPipeline() {
  m_shaderRegistry = std::make_unique<Lumen::ShaderRegistry>();

  { // PBR Pipeline
    Lumen::PipelineConfig config{};
    config.vertexShaderPath =
        std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/vert.spv";
    config.fragmentShaderPath =
        std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/pbr_frag.spv";
    config.pushConstantSize = sizeof(PushConstants);
    config.bindingDescription = Forma::Vertex::getBindingDescription();
    config.attributeDescriptions = Forma::Vertex::getAttributeDescriptions();
    config.descriptorSetLayouts = {m_descriptors->getGlobalLayout(),
                                   m_descriptors->getMaterialLayout()};
    config.depthTest = true;

    auto pipeline = std::make_shared<Lumen::Pipeline>();
    pipeline->init(*m_renderCtx, config);
    m_shaderRegistry->registerPipeline("pbr", pipeline);
  }

  // Skybox Pipeline (Cubemap)
  {
    Lumen::PipelineConfig config{};
    config.vertexShaderPath =
        std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/skybox_vert.spv";
    config.fragmentShaderPath =
        std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/skybox_frag.spv";
    config.pushConstantSize = sizeof(PushConstants);
    config.bindingDescription = Forma::Vertex::getBindingDescription();
    config.attributeDescriptions = Forma::Vertex::getAttributeDescriptions();
    config.descriptorSetLayouts = {m_descriptors->getGlobalLayout(),
                                   m_descriptors->getMaterialLayout()};
    config.depthTest = true;
    config.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    config.depthTest = true;
    config.depthWriteEnable = false; // Don't write to depth
    config.cullMode = VK_CULL_MODE_NONE;

    auto pipeline = std::make_shared<Lumen::Pipeline>();
    pipeline->init(*m_renderCtx, config);
    m_shaderRegistry->registerPipeline("skybox", pipeline);
  }

  // Skybox Pipeline (HDRI Equirectangular)
  {
    Lumen::PipelineConfig config{};
    config.vertexShaderPath =
        std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/skybox_vert.spv";
    config.fragmentShaderPath =
        std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/skybox_hdri_frag.spv";
    config.pushConstantSize = sizeof(PushConstants);
    config.bindingDescription = Forma::Vertex::getBindingDescription();
    config.attributeDescriptions = Forma::Vertex::getAttributeDescriptions();
    config.descriptorSetLayouts = {m_descriptors->getGlobalLayout(),
                                   m_descriptors->getMaterialLayout()};
    config.depthTest = true;
    config.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    config.depthTest = true;
    config.depthWriteEnable = false;
    config.cullMode = VK_CULL_MODE_NONE;

    auto pipeline = std::make_shared<Lumen::Pipeline>();
    pipeline->init(*m_renderCtx, config);
    m_shaderRegistry->registerPipeline("skybox_hdri", pipeline);
  }
}
void Engine::initDescriptors() {
  VkDevice device = m_renderCtx->getDevice();
  m_sampler.init(device);

  SDL_Log("Engine: Descriptors: Loading skybox textures...");
  auto skyboxAssets = m_assetManager->scanSkyboxes();
  for (auto &sa : skyboxAssets) {
    std::shared_ptr<Memoria::TextureAsset> tex;
    if (sa.isHDR) {
      tex = m_assetManager->loadHDR(sa.path);
    } else {
      tex = m_assetManager->loadCubemapFromCross(sa.path);
    }
    std::string pipelineName = sa.isHDR ? "skybox_hdri" : "skybox";
    m_skyboxOptions.push_back({sa.name, pipelineName, tex, sa.path});
  }

  m_skyboxTexture = m_skyboxOptions[0].texture;
  m_selectedSkybox = 0;

  // Collect initial texture views for bindless
  auto &textures = m_assetManager->getLoadedTextures();
  std::vector<VkImageView> initialViews;
  for (auto &t : textures)
    initialViews.push_back(t->view);

  m_descriptors->init(m_sampler.getSampler(), 1024,
                      initialViews);

  // Write initial skybox descriptor
  m_descriptors->updateSkybox(m_skyboxTexture->view);

  SDL_Log("Engine: Descriptors: Done.");
}

void Engine::updateBindlessDescriptorSet() {
  auto &textures = m_assetManager->getLoadedTextures();
  std::vector<VkImageView> views;
  for (auto &t : textures)
    views.push_back(t->view);
  m_descriptors->updateBindless(views);
}

void Engine::updateIBLDescriptors() {
  if (!m_iblMaps.irradianceMap || !m_iblMaps.prefilterMap ||
      !m_iblMaps.brdfLUT) {
    SDL_Log("Engine: updateIBLDescriptors: IBL maps not ready, skipping.");
    return;
  }
  m_descriptors->updateIBL(m_iblMaps.irradianceMap->view,
                            m_iblMaps.prefilterMap->view,
                            m_iblMaps.brdfLUT->view);
  SDL_Log("Engine: IBL descriptors updated.");
}

void Engine::updateSkyboxDescriptor() {
  m_descriptors->updateSkybox(m_skyboxTexture->view);
  SDL_Log("Engine: Skybox descriptor updated.");
}

void Engine::cleanupSwapchainResources() {
  VkDevice device = m_renderCtx->getDevice();
  for (auto fb : m_framebuffers)
    vkDestroyFramebuffer(device, fb, nullptr);
  m_framebuffers.clear();
}

void Engine::initFramebuffers() {
  const auto &swapViews = m_renderCtx->getSwapchainImageViews();
  auto swapExtent = m_renderCtx->getSwapchainExtent();
  VkDevice device = m_renderCtx->getDevice();
  VkRenderPass renderPass = m_renderCtx->getRenderPass();

  m_framebuffers.resize(swapViews.size());
  for (size_t i = 0; i < swapViews.size(); ++i) {
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = renderPass;
    fci.attachmentCount = 2;
    VkImageView attachments[] = {swapViews[i],
                                 m_renderCtx->getDepthImageView()};
    fci.pAttachments = attachments;
    fci.width = swapExtent.width;
    fci.height = swapExtent.height;
    fci.layers = 1;
    VK_CHECK(vkCreateFramebuffer(device, &fci, nullptr, &m_framebuffers[i]));
  }
}

void Engine::initCommands() {
  VkDevice device = m_renderCtx->getDevice();
  VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpci.queueFamilyIndex = m_renderCtx->getGraphicsQueueFamily();
  cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  VK_CHECK(vkCreateCommandPool(device, &cpci, nullptr, &m_cmdPool));

  m_cmdBufs.resize(MAX_FRAMES_IN_FLIGHT);
  VkCommandBufferAllocateInfo ai{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = m_cmdPool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
  VK_CHECK(vkAllocateCommandBuffers(device, &ai, m_cmdBufs.data()));
}

void Engine::initSync() {
  VkDevice device = m_renderCtx->getDevice();
  m_imageAvail.resize(MAX_FRAMES_IN_FLIGHT);
  m_renderDone.resize(MAX_FRAMES_IN_FLIGHT);
  m_inFlight.resize(MAX_FRAMES_IN_FLIGHT);

  VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    VK_CHECK(vkCreateSemaphore(device, &sci, nullptr, &m_imageAvail[i]));
    VK_CHECK(vkCreateSemaphore(device, &sci, nullptr, &m_renderDone[i]));
    VK_CHECK(vkCreateFence(device, &fci, nullptr, &m_inFlight[i]));
  }
}

void Engine::recreateSwapchain() {
  int w = 0, h = 0;
  m_window->getPixelSize(w, h);
  while (w == 0 || h == 0) {
    m_window->getPixelSize(w, h);
    SDL_WaitEvent(nullptr);
  }

  vkDeviceWaitIdle(m_renderCtx->getDevice());

  cleanupSwapchainResources();
  m_renderCtx->cleanupDepthBuffer(m_allocator->getVma());
  m_renderCtx->recreateSwapchain(*m_window);
  m_renderCtx->initDepthBuffer(m_allocator->getVma());

  auto extent = m_renderCtx->getSwapchainExtent();
  m_camera->setPerspective(
      (float)m_config.get_number("camera.fov", 45.0),
      static_cast<float>(extent.width) / static_cast<float>(extent.height),
      (float)m_config.get_number("camera.near", 0.1),
      (float)m_config.get_number("camera.far", 100.0));
  initFramebuffers();
}

void Engine::drawFrame() {
  if (m_needsResize) {
    SDL_Log("Engine: drawFrame: needsResize");
    recreateSwapchain();
    m_needsResize = false;
    return;
  }

  VkDevice device = m_renderCtx->getDevice();
  VkSwapchainKHR swapchain = m_renderCtx->getSwapchain();
  VkQueue graphicsQueue = m_renderCtx->getGraphicsQueue();

  VkFence fence = m_inFlight[m_frameIndex];
  VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));

  uint32_t imageIdx;
  VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                     m_imageAvail[m_frameIndex], VK_NULL_HANDLE,
                                     &imageIdx);
  if (r == VK_ERROR_OUT_OF_DATE_KHR) {
    m_needsResize = true;
    return;
  }
  if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
    throw std::runtime_error("vkAcquireNextImageKHR failed");

  VK_CHECK(vkResetFences(device, 1, &fence));

  VkCommandBuffer cb = m_cmdBufs[m_frameIndex];
  VK_CHECK(vkResetCommandBuffer(cb, 0));

  // Calculate stats
  static uint64_t lastCount = SDL_GetPerformanceCounter();
  uint64_t currentCount = SDL_GetPerformanceCounter();
  float frameTimeMs =
      (float)(currentCount - lastCount) * 1000.0f / (float)m_perfFreq;
  lastCount = currentCount;

  // Camera update
  if (m_input->isCaptured()) {
    bool fw = m_input->isKeyPressed(SDLK_W);
    bool bw = m_input->isKeyPressed(SDLK_S);
    bool lf = m_input->isKeyPressed(SDLK_A);
    bool rt = m_input->isKeyPressed(SDLK_D);
    bool up = m_input->isKeyPressed(SDLK_SPACE);
    bool dn = m_input->isKeyPressed(SDLK_LSHIFT) ||
              m_input->isKeyPressed(SDLK_RSHIFT);

    m_camera->processKeyboard(fw, bw, lf, rt, up, dn, frameTimeMs / 1000.0f);
    m_camera->processMouse(m_input->getMouseDelta());
  }

  m_scene.update(frameTimeMs / 1000.0f);

  auto &sceneEntities = m_scene.getEntities();
  Vigil::DebugStats stats{};
  stats.fps = 1000.0f / (frameTimeMs > 1e-6f ? frameTimeMs : 1.0f);
  stats.frameTime = frameTimeMs;
  stats.drawCalls = static_cast<uint32_t>(sceneEntities.size());
  for (const auto &ent : sceneEntities) {
    if (ent.mesh)
      stats.vertexCount += ent.mesh->vertexCount;
  }
  stats.cameraPos = m_camera->getPosition();
  stats.cameraFront = m_camera->getFront();
  stats.cameraSpeed = &m_camera->moveSpeed;
  stats.cameraSens = &m_camera->mouseSensitivity;
  bool wantCapture = m_input->isCaptured();
  stats.captureMouse = &wantCapture;

  // UI overlay
  m_overlay->beginFrame();
  std::vector<const char *> skyboxNamePtrs;
  skyboxNamePtrs.reserve(m_skyboxOptions.size());
  for (auto &opt : m_skyboxOptions)
    skyboxNamePtrs.push_back(opt.name.c_str());
  auto shaderNames = getShaderNames();
  std::vector<const char *> shaderNamePtrs;
  shaderNamePtrs.reserve(shaderNames.size());
  for (auto &sn : shaderNames)
    shaderNamePtrs.push_back(sn.c_str());
  m_overlay->drawUI(*m_renderCtx, stats, m_scene, *m_assetManager,
                    m_sampler.getSampler(), &m_debugMode,
                    &m_selectedSkybox, static_cast<uint32_t>(m_skyboxOptions.size()),
                    skyboxNamePtrs.data(),
                    static_cast<uint32_t>(shaderNamePtrs.size()),
                    shaderNamePtrs.data());

  if (wantCapture != m_input->isCaptured()) {
    m_input->setCapture(wantCapture, m_window->getHandle());
  }

  // Viewport click selection (only when NOT hovering any ImGui window)
  if (!ImGui::GetIO().WantCaptureMouse &&
      m_input->isMousePressed(Sensus::MouseButton::Left)) {
    pickEntityAtMouse();
  }

  // Handle focus camera request from inspector context menu
  if (stats.hasFocusTarget) {
    glm::vec3 target = stats.focusTarget;
    glm::vec3 eye = target + glm::vec3(2.0f, 1.0f, 2.0f);
    m_camera->lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    stats.hasFocusTarget = false;
  }

  // Update skybox if selection changed
  if (m_selectedSkybox < m_skyboxOptions.size()) {
    auto &opt = m_skyboxOptions[m_selectedSkybox];
    if (m_skyboxTexture != opt.texture) {
      m_skyboxTexture = opt.texture;
      auto &skyboxEnt = m_scene.getEntities()[m_skyboxEntityIndex];
      skyboxEnt.material->shaderName = opt.pipelineName;
      updateSkyboxDescriptor();

      // Regenerate IBL maps for the new skybox
      VkQueue gfxQueue = m_renderCtx->getGraphicsQueue();
      if (m_iblMaps.irradianceMap)
        m_iblMaps.irradianceMap->destroy(*m_allocator, m_renderCtx->getDevice());
      if (m_iblMaps.prefilterMap)
        m_iblMaps.prefilterMap->destroy(*m_allocator, m_renderCtx->getDevice());
      if (m_iblMaps.brdfLUT)
        m_iblMaps.brdfLUT->destroy(*m_allocator, m_renderCtx->getDevice());

      if (opt.pipelineName == "skybox") {
        m_iblMaps = Lumen::IBLGenerator::generate(
            *m_allocator, m_renderCtx->getDevice(), gfxQueue, m_cmdPool,
            opt.sourcePath);
      } else {
        m_iblMaps = Lumen::IBLGenerator::generateFromHDR(
            *m_allocator, m_renderCtx->getDevice(), gfxQueue, m_cmdPool,
            opt.sourcePath);
      }
      updateIBLDescriptors();
    }
  }

  // Update Global UBO
  GlobalUBO ubo{};
  ubo.lightDir = glm::vec4(m_scene.globalLightDir, 0.0f);
  ubo.viewPos = glm::vec4(m_camera->getPosition(), 1.0f);
  ubo.lightColor = glm::vec4(m_scene.globalLightColor, 1.0f);
  ubo.view = m_camera->getView();
  ubo.proj = m_camera->getProj();
  ubo.exposure = 1.0f;
   ubo.gamma = 2.2f;
   memcpy(m_descriptors->getUBOMapped(), &ubo, sizeof(GlobalUBO));
 
   VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VK_CHECK(vkBeginCommandBuffer(cb, &bi));

  auto swapExtent = m_renderCtx->getSwapchainExtent();

  VkClearValue clearValues[2];
  clearValues[0].color = {{0.01f, 0.01f, 0.01f, 1.0f}};
  clearValues[1].depthStencil = {1.0f, 0};

  VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rpbi.renderPass = m_renderCtx->getRenderPass();
  rpbi.framebuffer = m_framebuffers[imageIdx];
  rpbi.renderArea.extent = swapExtent;
  rpbi.clearValueCount = 2;
   rpbi.pClearValues = clearValues;
 
   vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport vp{};
  vp.width = static_cast<float>(swapExtent.width);
  vp.height = static_cast<float>(swapExtent.height);
  vp.minDepth = 0.0f;
  vp.maxDepth = 1.0f;
  vkCmdSetViewport(cb, 0, 1, &vp);

  VkRect2D scissor{{0, 0}, swapExtent};
  vkCmdSetScissor(cb, 0, 1, &scissor);

  Lumen::Pipeline *lastPipeline = nullptr;
  VkDeviceSize offset = 0;

  for (auto &ent : sceneEntities) {
    if (!ent.mesh || !ent.material || !ent.effectiveVisible) {
      if (!ent.effectiveVisible) {
        continue;
      }
      SDL_Log("Engine: drawFrame: skipping entity %s (no mesh/material)",
              ent.name.c_str());
      continue;
    }

    auto pipeline = m_shaderRegistry->getPipeline(ent.material->shaderName);
    if (!pipeline) {
      SDL_Log("Engine: drawFrame: skipping entity %s (no pipeline for %s)",
              ent.name.c_str(), ent.material->shaderName.c_str());
      continue;
    }

    if (pipeline.get() != lastPipeline) {
      pipeline->bind(cb);
      // Bind Global Set (Set 0)
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->getLayout(), 0, 1,
                              &m_descriptors->getGlobalSet(), 0, nullptr);
      // Bind Bindless Set (Set 1)
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->getLayout(), 1, 1,
                              &m_descriptors->getBindlessSet(), 0, nullptr);
      lastPipeline = pipeline.get();
    }

    // Bindless indices are passed via PushConstants now.
    // Set 1 is bound above once per pipeline.

    PushConstants pc{};
    // Skybox specifically needs camera translation removed for the infinite
    // effect
    if (ent.material->shaderName == "skybox" ||
        ent.material->shaderName == "skybox_hdri") {
      pc.model = glm::translate(glm::mat4(1.0f), m_camera->getPosition());
    } else {
      pc.model = ent.globalTransform;
    }
    pc.baseColor = ent.material->baseColor;
    pc.roughness = ent.material->roughness;
    pc.metallic = ent.material->metallic;
    pc.albedoIdx = ent.material->albedoIdx;
    pc.normalIdx = ent.material->normalIdx;
    pc.mrIdx = ent.material->metallicRoughnessIdx;
     pc.aoIdx = ent.material->aoIdx;
     pc.emissiveIdx = ent.material->emissiveIdx;
     pc.debugMode = m_debugMode;
 
     vkCmdPushConstants(cb, pipeline->getLayout(),
                       VK_SHADER_STAGE_VERTEX_BIT |
                           VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pc);
    vkCmdBindVertexBuffers(cb, 0, 1, &ent.mesh->vertexBuffer, &offset);
    if (ent.mesh->indexBuffer) {
      vkCmdBindIndexBuffer(cb, ent.mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(cb, ent.mesh->indexCount, 1, 0, 0, 0);
    } else {
      vkCmdDraw(cb, ent.mesh->vertexCount, 1, 0, 0);
    }
  }

  // Overlay rendering
  m_overlay->endFrameAndRecord(cb);

  vkCmdEndRenderPass(cb);
  VK_CHECK(vkEndCommandBuffer(cb));

  VkPipelineStageFlags waitStage =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &m_imageAvail[m_frameIndex];
  si.pWaitDstStageMask = &waitStage;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cb;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &m_renderDone[m_frameIndex];
  VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &si, fence));

  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &m_renderDone[m_frameIndex];
  pi.swapchainCount = 1;
  pi.pSwapchains = &swapchain;
  pi.pImageIndices = &imageIdx;
  r = vkQueuePresentKHR(graphicsQueue, &pi);
  if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
    m_needsResize = true;
  else if (r != VK_SUCCESS)
    throw std::runtime_error("vkQueuePresentKHR failed");

  m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Engine::initMesh() {
  m_scene.clear();

  auto textureCube = m_assetManager->loadTexture(
      std::string(CONCORDIA_ASSETS_DIR) + "/images/CubeTexture.png");

  // Create Manual Materials for OBJ testing
  auto matLit = std::make_shared<Forma::Material>();
  matLit->shaderName = "pbr";
  matLit->albedo = textureCube;
  matLit->baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
  matLit->roughness = 0.5f;
  matLit->metallic = 0.0f;

  auto matUnlit = std::make_shared<Forma::Material>();
  matUnlit->shaderName = "unlit";
  matUnlit->albedo = textureCube;
  matUnlit->baseColor = {1.0f, 0.5f, 0.5f, 1.0f};

  auto matGold = std::make_shared<Forma::Material>();
  matGold->shaderName = "pbr";
  matGold->albedo = textureCube;
  matGold->baseColor = {1.0f, 0.78f, 0.15f, 1.0f}; // gold tint
  matGold->metallic = 0.95f;
  matGold->roughness = 0.35f;

  syncMaterialIndices(matLit);
  syncMaterialIndices(matUnlit);
  syncMaterialIndices(matGold);

  updateBindlessDescriptorSet();

  SDL_Log("Engine: initMesh: setting up initial materials...");
  // Bindless does not need per-material descriptor sets.
  SDL_Log("Engine: initMesh: initial materials setup done.");

  // Add Skybox Entity
  m_skyboxEntityIndex = m_scene.addEntity("Skybox");
  auto &skybox = m_scene.getEntities()[m_skyboxEntityIndex];
  skybox.mesh = m_assetManager->createCubeMesh();
  skybox.meshSource = "@primitive(cube)";
  skybox.material = std::make_shared<Forma::Material>();
  skybox.material->shaderName = m_skyboxOptions[m_selectedSkybox].pipelineName;
  skybox.material->albedo = m_skyboxTexture;
  skybox.transform.scale = {10.0f, 10.0f, 10.0f};
  syncMaterialIndices(skybox.material);
 
  // Load GLTF Models
  m_assetManager->loadGLTF(std::string(CONCORDIA_ASSETS_DIR) +
                              "/models/gltf/DamagedHelmet.glb",
                           m_scene);
  
  m_assetManager->loadGLTF(
      std::string(CONCORDIA_ASSETS_DIR) + "/models/gltf/cube.glb", m_scene);
  
  // Strip assets dir prefix for portable scene URIs
  {
    std::string prefix = std::string(CONCORDIA_ASSETS_DIR) + "/";
    auto strip = [&](std::string &s) {
      if (s.compare(0, prefix.size(), prefix) == 0)
        s = s.substr(prefix.size());
    };
    for (auto &ent : m_scene.getEntities()) {
      strip(ent.meshSource);
      if (ent.material) {
        strip(ent.material->albedoSource);
        strip(ent.material->normalSource);
        strip(ent.material->metallicRoughnessSource);
        strip(ent.material->aoSource);
        strip(ent.material->emissiveSource);
      }
    }
  }
  
  // Set up hierarchy: Parent GLTF Cube -> Child GLTF Cube
  int parentIdx = m_scene.findEntity("Cube");
  if (parentIdx != -1) {
    auto &parent = m_scene.getEntities()[parentIdx];
    parent.transform.position = {0, 0, 0};
 
    // Add a smaller child cube to the left
    int childIdx = m_scene.addEntity("ChildCube", parentIdx);
    auto &child = m_scene.getEntities()[childIdx];
    child.mesh = parent.mesh;
    child.material = parent.material;
    child.meshSource = parent.meshSource;
    child.transform.position = {-1.5f, 0, 0};
    child.transform.scale = {0.5f, 0.5f, 0.5f};
    if (child.material) syncMaterialIndices(child.material);
  }
 
  // Ensure all entities loaded from GLTF have their material descriptors setup
  for (auto &ent : m_scene.getEntities()) {
    if (ent.material && ent.material->shaderName == "pbr") {
      syncMaterialIndices(ent.material);
    }
  }
}

void Engine::syncMaterialIndices(std::shared_ptr<Forma::Material> mat) {
  if (!mat)
    return;
  auto resolve = [&](std::shared_ptr<Memoria::TextureAsset> &tex,
                     const std::string &source, bool srgb,
                     std::shared_ptr<Memoria::TextureAsset> fallback) {
    if (!tex && !source.empty())
      tex = m_assetManager->resolveTexture(source, srgb);
    if (!tex)
      tex = fallback;
  };
  resolve(mat->albedo, mat->albedoSource, true,
          m_assetManager->getDefaultWhite());
  resolve(mat->normal, mat->normalSource, false,
          m_assetManager->getDefaultNormal());
  resolve(mat->metallicRoughness, mat->metallicRoughnessSource, false,
          m_assetManager->getDefaultWhiteLinear());
  resolve(mat->ao, mat->aoSource, false,
          m_assetManager->getDefaultWhiteLinear());
  resolve(mat->emissive, mat->emissiveSource, true,
          m_assetManager->getDefaultBlack());
  mat->albedoIdx = mat->albedo->textureId;
  mat->normalIdx = mat->normal->textureId;
  mat->metallicRoughnessIdx = mat->metallicRoughness->textureId;
  mat->aoIdx = mat->ao->textureId;
  mat->emissiveIdx = mat->emissive->textureId;
}

void Engine::resolveSceneMeshes() {
  for (auto &ent : m_scene.getEntities()) {
    if (!ent.meshSource.empty() && !ent.mesh) {
      ent.mesh = m_assetManager->getMesh(ent.meshSource);
      if (ent.mesh) {
        SDL_Log("Engine: resolved mesh for '%s' (%s)", ent.name.c_str(),
                ent.meshSource.c_str());
      } else {
        SDL_Log("Engine: unresolved mesh source '%s' for entity '%s'",
                ent.meshSource.c_str(), ent.name.c_str());
      }
    }
    if (ent.material) {
      syncMaterialIndices(ent.material);
    }
  }
  updateBindlessDescriptorSet();
  SDL_Log("Engine: scene resolve complete (%zu entities processed)",
          m_scene.getEntities().size());
}

void Engine::pickEntityAtMouse() {
  float mx, my;
  SDL_GetMouseState(&mx, &my);
  int vpW, vpH;
  SDL_GetWindowSize(m_window->getHandle(), &vpW, &vpH);
  int idx = Mundus::pickEntity(m_scene, *m_camera, mx, my, vpW, vpH);
  m_overlay->setSelectedEntity(idx);
}

void Engine::run() {
  SDL_Log("Engine: run() started.");
  SDL_Event ev;
  while (m_isRunning) {
    m_input->newFrame();

    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL3_ProcessEvent(&ev);

      bool imguiCapturesKeyboard = ImGui::GetIO().WantCaptureKeyboard;
      bool imguiCapturesMouse = ImGui::GetIO().WantCaptureMouse;

      if (ev.type == SDL_EVENT_QUIT)
        m_isRunning = false;

      if (ev.type == SDL_EVENT_WINDOW_RESIZED ||
          ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        m_needsResize = true;
      }

      // If we are captured, or ImGui doesn't want the event, give it to Sensus
      if (m_input->isCaptured()) {
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
          m_input->setCapture(false, m_window->getHandle());
        } else {
          m_input->processEvent(ev);
        }
      } else {
        // Send to Sensus only if ImGui isn't using it
        bool isKeyboardEvent =
            (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP);
        bool isMouseEvent = (ev.type == SDL_EVENT_MOUSE_MOTION ||
                             ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                             ev.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                             ev.type == SDL_EVENT_MOUSE_WHEEL);

        if ((isKeyboardEvent && !imguiCapturesKeyboard) ||
            (isMouseEvent && !imguiCapturesMouse)) {
          m_input->processEvent(ev);
        }
      }

      // Blender-style keyboard shortcuts
      Vigil::processEditorKeys(ev, imguiCapturesKeyboard,
                               imguiCapturesMouse, m_input->isCaptured(),
                               *m_overlay, m_scene, *m_camera);
    }
    drawFrame();
  }
}

} // namespace Nucleus
