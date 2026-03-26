#include "engine.h"
#include "forma/material.h"
#include "forma/mesh.h"
#include "lumen/pipeline.h"
#include "lumen/shader_registry.h"
#include "mundus/scene.h"
#include "vista/camera.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vk_mem_alloc.h>

#define VK_CHECK(call)                                                         \
  do {                                                                         \
    VkResult _r = (call);                                                      \
    if (_r != VK_SUCCESS) {                                                    \
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,                               \
                   "Vulkan error %d at " __FILE__ ":%d", _r, __LINE__);        \
      throw std::runtime_error("Vulkan call failed in Nucleus::Engine");       \
    }                                                                          \
  } while (0)

struct PushConstants {
  glm::mat4 model;
  glm::vec4 baseColor;
  float roughness;
  float metallic;
};

namespace Nucleus {

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

Engine::Engine() {
  m_window = std::make_unique<Petra::Window>("Concordia – Scene", 800, 600);
  m_renderCtx = std::make_unique<Render::Context>(*m_window);
  m_allocator = std::make_unique<Memoria::Allocator>(
      m_renderCtx->getInstance(), m_renderCtx->getPhysicalDevice(),
      m_renderCtx->getDevice());
  m_overlay = std::make_unique<Vigil::Overlay>(*m_window, *m_renderCtx);
  m_input = std::make_unique<Sensus::Input>();
  m_camera = std::make_unique<Vista::Camera>();
  m_camera->setPerspective(45.0f, 800.0f / 600.0f, 0.1f, 100.0f);

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

  SDL_Log("Engine: Initializing descriptors...");
  initDescriptors();
  SDL_Log("Engine: Initializing shader registry...");
  initPipeline();
  SDL_Log("Engine: Initializing framebuffers...");
  initFramebuffers();
  SDL_Log("Engine: Initializing sync...");
  initSync();
  SDL_Log("Engine: Initializing mesh...");
  initMesh();
  SDL_Log("Engine: Constructor finished.");
}

Engine::~Engine() {
  VkDevice device = m_renderCtx->getDevice();
  if (device) {
    vkDeviceWaitIdle(device);

    // Explicitly release scene and assets before destroying allocator
    m_skyboxTexture.reset();
    m_scene.getEntities().clear();

    if (m_allocator) {
      m_renderCtx->cleanupDepthBuffer(m_allocator->getVma());
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

    if (m_globalUBO) {
      vmaUnmapMemory(m_allocator->getVma(), m_globalUBOAlloc);
      m_allocator->destroyBuffer(m_globalUBO, m_globalUBOAlloc);
      m_globalUBO = VK_NULL_HANDLE;
    }

    if (m_descriptorPool)
      vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    if (m_globalDescriptorLayout)
      vkDestroyDescriptorSetLayout(device, m_globalDescriptorLayout, nullptr);
    if (m_materialDescriptorLayout)
      vkDestroyDescriptorSetLayout(device, m_materialDescriptorLayout, nullptr);

    cleanupSwapchainResources();
  }
}

void Engine::initPipeline() {
  m_shaderRegistry = std::make_unique<Lumen::ShaderRegistry>();

  // Blinn-Phong Pipeline
  {
    Lumen::PipelineConfig config{};
    config.vertexShaderPath =
        std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/vert.spv";
    config.fragmentShaderPath =
        std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/frag.spv";
    config.pushConstantSize = sizeof(PushConstants);
    config.bindingDescription = Forma::Vertex::getBindingDescription();
    config.attributeDescriptions = Forma::Vertex::getAttributeDescriptions();
    config.descriptorSetLayouts = {m_globalDescriptorLayout,
                                   m_materialDescriptorLayout};
    config.depthTest = true;

    auto pipeline = std::make_shared<Lumen::Pipeline>();
    pipeline->init(*m_renderCtx, config);
    m_shaderRegistry->registerPipeline("pbr", pipeline);
  }

  // Unlit Pipeline
  {
    Lumen::PipelineConfig config{};
    config.vertexShaderPath =
        std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/vert.spv";
    config.fragmentShaderPath =
        std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/unlit_frag.spv";
    config.pushConstantSize = sizeof(PushConstants);
    config.bindingDescription = Forma::Vertex::getBindingDescription();
    config.attributeDescriptions = Forma::Vertex::getAttributeDescriptions();
    config.descriptorSetLayouts = {m_globalDescriptorLayout,
                                   m_materialDescriptorLayout};
    config.depthTest = true;

    auto pipeline = std::make_shared<Lumen::Pipeline>();
    pipeline->init(*m_renderCtx, config);
    m_shaderRegistry->registerPipeline("unlit", pipeline);
  }

  // Skybox Pipeline
  {
    Lumen::PipelineConfig config{};
    config.vertexShaderPath =
        std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/skybox_vert.spv";
    config.fragmentShaderPath =
        std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/skybox_frag.spv";
    config.pushConstantSize = sizeof(PushConstants);
    config.bindingDescription = Forma::Vertex::getBindingDescription();
    config.attributeDescriptions = Forma::Vertex::getAttributeDescriptions();
    config.descriptorSetLayouts = {m_globalDescriptorLayout,
                                   m_materialDescriptorLayout};
    config.depthTest = true;
    config.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    config.depthTest = true;
    config.depthWriteEnable = false; // Don't write to depth
    config.cullMode = VK_CULL_MODE_NONE;

    auto pipeline = std::make_shared<Lumen::Pipeline>();
    pipeline->init(*m_renderCtx, config);
    m_shaderRegistry->registerPipeline("skybox", pipeline);
  }
}
void Engine::initDescriptors() {
  VkDevice device = m_renderCtx->getDevice();
  m_sampler.init(device);

  SDL_Log("Engine: Descriptors: Creating layouts...");
  // --- SET 0: GLOBAL LAYOUT ---
  VkDescriptorSetLayoutBinding uboBinding{};
  uboBinding.binding = 0;
  uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboBinding.descriptorCount = 1;
  uboBinding.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding skyboxBinding{};
  skyboxBinding.binding = 1;
  skyboxBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  skyboxBinding.descriptorCount = 1;
  skyboxBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding irradianceBinding{};
  irradianceBinding.binding = 2;
  irradianceBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  irradianceBinding.descriptorCount = 1;
  irradianceBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding prefilterBinding{};
  prefilterBinding.binding = 3;
  prefilterBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  prefilterBinding.descriptorCount = 1;
  prefilterBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding brdfBinding{};
  brdfBinding.binding = 4;
  brdfBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  brdfBinding.descriptorCount = 1;
  brdfBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  std::array<VkDescriptorSetLayoutBinding, 5> globalBindings = {
      uboBinding, skyboxBinding, irradianceBinding, prefilterBinding,
      brdfBinding};

  VkDescriptorSetLayoutCreateInfo globalInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  globalInfo.bindingCount = static_cast<uint32_t>(globalBindings.size());
  globalInfo.pBindings = globalBindings.data();
  VK_CHECK(vkCreateDescriptorSetLayout(device, &globalInfo, nullptr,
                                       &m_globalDescriptorLayout));

  // --- SET 1: MATERIAL LAYOUT ---
  VkDescriptorSetLayoutBinding albedoBinding{};
  albedoBinding.binding = 0;
  albedoBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  albedoBinding.descriptorCount = 1;
  albedoBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding normalBinding{};
  normalBinding.binding = 1;
  normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  normalBinding.descriptorCount = 1;
  normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding mrBinding{};
  mrBinding.binding = 2;
  mrBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  mrBinding.descriptorCount = 1;
  mrBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding aoBinding{};
  aoBinding.binding = 3;
  aoBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  aoBinding.descriptorCount = 1;
  aoBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding emissiveBinding{};
  emissiveBinding.binding = 4;
  emissiveBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  emissiveBinding.descriptorCount = 1;
  emissiveBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  std::array<VkDescriptorSetLayoutBinding, 5> materialBindings = {
      albedoBinding, normalBinding, mrBinding, aoBinding, emissiveBinding};

  VkDescriptorSetLayoutCreateInfo materialInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  materialInfo.bindingCount = static_cast<uint32_t>(materialBindings.size());
  materialInfo.pBindings = materialBindings.data();
  VK_CHECK(vkCreateDescriptorSetLayout(device, &materialInfo, nullptr,
                                       &m_materialDescriptorLayout));

  SDL_Log("Engine: Descriptors: Creating pool...");
  // --- POOL ---
  std::array<VkDescriptorPoolSize, 2> poolSizes{};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[0].descriptorCount = 10;
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[1].descriptorCount = 200;

  VkDescriptorPoolCreateInfo poolInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = 50;
  VK_CHECK(
      vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool));

  SDL_Log("Engine: Descriptors: Allocating set 0...");
  // --- GLOBAL SET ALLOCATION & UPDATE ---
  VkDescriptorSetAllocateInfo allocInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = m_descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &m_globalDescriptorLayout;
  VK_CHECK(
      vkAllocateDescriptorSets(device, &allocInfo, &m_globalDescriptorSet));

  SDL_Log("Engine: Descriptors: Creating UBO...");
  // UBO
  m_allocator->createBuffer(
      sizeof(GlobalUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
      VMA_MEMORY_USAGE_CPU_ONLY, m_globalUBO, m_globalUBOAlloc);
  vmaMapMemory(m_allocator->getVma(), m_globalUBOAlloc, &m_globalUBOMapped);

  VkDescriptorBufferInfo bufferInfo{};
  bufferInfo.buffer = m_globalUBO;
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(GlobalUBO);

  SDL_Log("Engine: Descriptors: Loading skybox...");
  // Load Skybox for Set 0 Binding 1
  m_skyboxTexture = m_assetManager->loadCubemapFromCross(
      std::string(CONCORDIA_ASSETS_DIR) +
      "/images/skybox/Cubemap_Sky_01-512x512.png");

  SDL_Log("Engine: Descriptors: Updating set 0 (5 bindings)...");
  VkDescriptorImageInfo skyboxInfo{};
  skyboxInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  skyboxInfo.imageView = m_skyboxTexture->view;
  skyboxInfo.sampler = m_sampler.getSampler();

  std::array<VkWriteDescriptorSet, 5> descriptorWrites{};
  // UBO
  descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[0].dstSet = m_globalDescriptorSet;
  descriptorWrites[0].dstBinding = 0;
  descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorWrites[0].descriptorCount = 1;
  descriptorWrites[0].pBufferInfo = &bufferInfo;

  // Skybox (Binding 1)
  descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[1].dstSet = m_globalDescriptorSet;
  descriptorWrites[1].dstBinding = 1;
  descriptorWrites[1].descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrites[1].descriptorCount = 1;
  descriptorWrites[1].pImageInfo = &skyboxInfo;

  // Irradiance (Binding 2) - Placeholder
  descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[2].dstSet = m_globalDescriptorSet;
  descriptorWrites[2].dstBinding = 2;
  descriptorWrites[2].descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrites[2].descriptorCount = 1;
  descriptorWrites[2].pImageInfo = &skyboxInfo;

  // Prefilter (Binding 3) - Placeholder
  descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[3].dstSet = m_globalDescriptorSet;
  descriptorWrites[3].dstBinding = 3;
  descriptorWrites[3].descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrites[3].descriptorCount = 1;
  descriptorWrites[3].pImageInfo = &skyboxInfo;

  // BRDF (Binding 4) - Placeholder (Wait, BRDF is 2D, skybox is Cube! This will
  // fail validation but might work for a quick test) Actually, I should use the
  // White texture for 2D placeholders.
  VkDescriptorImageInfo whiteInfo{};
  whiteInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  whiteInfo.imageView = m_assetManager->getDefaultBRDF()->view;
  whiteInfo.sampler = m_sampler.getSampler();

  descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[4].dstSet = m_globalDescriptorSet;
  descriptorWrites[4].dstBinding = 4;
  descriptorWrites[4].descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrites[4].descriptorCount = 1;
  descriptorWrites[4].pImageInfo = &whiteInfo;

  vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()),
                         descriptorWrites.data(), 0, nullptr);
  SDL_Log("Engine: Descriptors: Set 0 updated.");
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
  m_camera->setPerspective(45.0f,
                           static_cast<float>(extent.width) /
                               static_cast<float>(extent.height),
                           0.1f, 100.0f);
  initFramebuffers();
}

void Engine::drawFrame() {
  SDL_Log("Engine: drawFrame() started.");
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
  float time =
      (float)(SDL_GetPerformanceCounter() - m_startCount) / (float)m_perfFreq;
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
  m_overlay->drawUI(*m_renderCtx, stats, m_scene);

  if (wantCapture != m_input->isCaptured()) {
    m_input->setCapture(wantCapture, m_window->getHandle());
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
  SDL_Log("Engine: drawFrame: updating UBO");
  memcpy(m_globalUBOMapped, &ubo, sizeof(GlobalUBO));

  SDL_Log("Engine: drawFrame: beginning command buffer");
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

  SDL_Log("Engine: drawFrame: beginning render pass");
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
    if (!ent.mesh || !ent.material) {
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
                              &m_globalDescriptorSet, 0, nullptr);
      lastPipeline = pipeline.get();
    }

    // Bind Material Set (Set 1) if available
    if (ent.material->descriptorSet != VK_NULL_HANDLE) {
      vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline->getLayout(), 1, 1,
                              &ent.material->descriptorSet, 0, nullptr);
    }

    PushConstants pc{};
    // Skybox specifically needs camera translation removed for the infinite
    // effect
    if (ent.material->shaderName == "skybox") {
      pc.model = glm::translate(glm::mat4(1.0f), m_camera->getPosition());
    } else {
      pc.model = ent.globalTransform;
    }
    pc.baseColor = ent.material->baseColor;
    pc.roughness = ent.material->roughness;
    pc.metallic = ent.material->metallic;

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
  SDL_Log("Engine: drawFrame: finished frame %u", m_frameIndex);
}

void Engine::initMesh() {
  m_scene.clear();

  auto centerCubeMesh = m_assetManager->loadMesh(
      std::string(CONCORDIA_ASSETS_DIR) + "/models/cube.obj", true);
  auto textureCube = m_assetManager->loadTexture(
      std::string(CONCORDIA_ASSETS_DIR) + "/images/CubeTexture.png");

  // Create Materials
  auto matLit = std::make_shared<Forma::Material>();
  matLit->shaderName = "pbr";
  matLit->albedo = textureCube;
  matLit->baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
  matLit->roughness = 0.2f;

  auto matUnlit = std::make_shared<Forma::Material>();
  matUnlit->shaderName = "unlit";
  matUnlit->albedo = textureCube;
  matUnlit->baseColor = {1.0f, 0.5f, 0.5f, 1.0f}; // Reddish unlit

  auto matGold = std::make_shared<Forma::Material>();
  matGold->shaderName = "pbr";
  matGold->albedo = textureCube;
  SDL_Log("Engine: initMesh: setupMaterialSet starting...");
  auto setupMaterialSet = [&](std::shared_ptr<Forma::Material> mat) {
    if (!mat) {
      SDL_Log("Engine: setupMaterialSet: mat is NULL");
      return;
    }
    if (mat->descriptorSet != VK_NULL_HANDLE)
      return;

    SDL_Log("Engine: setupMaterialSet: allocating descriptor set...");
    VkDescriptorSetAllocateInfo allocInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_materialDescriptorLayout;

    if (!m_renderCtx) {
      SDL_Log("Engine: setupMaterialSet: m_renderCtx is NULL!");
      return;
    }

    VK_CHECK(vkAllocateDescriptorSets(m_renderCtx->getDevice(), &allocInfo,
                                      &mat->descriptorSet));

    SDL_Log("Engine: setupMaterialSet: getting fallback textures...");
    auto getTex = [&](std::shared_ptr<Memoria::TextureAsset> tex,
                      std::shared_ptr<Memoria::TextureAsset> fallback) {
      return tex ? tex : fallback;
    };

    if (!m_assetManager) {
      SDL_Log("Engine: setupMaterialSet: m_assetManager is NULL!");
      return;
    }

    std::array<std::shared_ptr<Memoria::TextureAsset>, 5> targets = {
        getTex(mat->albedo, m_assetManager->getDefaultWhite()),
        getTex(mat->normal, m_assetManager->getDefaultNormal()),
        getTex(mat->metallicRoughness, m_assetManager->getDefaultWhite()),
        getTex(mat->ao, m_assetManager->getDefaultWhite()),
        getTex(mat->emissive, m_assetManager->getDefaultBlack())};

    SDL_Log("Engine: setupMaterialSet: updating descriptor sets...");
    std::array<VkDescriptorImageInfo, 5> imageInfos{};
    std::array<VkWriteDescriptorSet, 5> writes{};

    for (int i = 0; i < 5; ++i) {
      if (!targets[i]) {
        SDL_Log("Engine: setupMaterialSet: target texture %d is NULL!", i);
        continue;
      }
      imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imageInfos[i].imageView = targets[i]->view;
      imageInfos[i].sampler = m_sampler.getSampler();

      writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = mat->descriptorSet;
      writes[i].dstBinding = i;
      writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writes[i].descriptorCount = 1;
      writes[i].pImageInfo = &imageInfos[i];
    }

    vkUpdateDescriptorSets(m_renderCtx->getDevice(),
                           static_cast<uint32_t>(writes.size()), writes.data(),
                           0, nullptr);
    SDL_Log("Engine: setupMaterialSet: done.");
  };

  SDL_Log("Engine: initMesh: setting up initial materials...");
  setupMaterialSet(matLit);
  setupMaterialSet(matUnlit);
  setupMaterialSet(matGold);
  SDL_Log("Engine: initMesh: initial materials setup done.");

  // Add Skybox Entity
  int skyboxIdx = m_scene.addEntity("Skybox");
  auto &skybox = m_scene.getEntities()[skyboxIdx];
  skybox.mesh = centerCubeMesh;
  skybox.material = std::make_shared<Forma::Material>();
  skybox.material->shaderName = "skybox";
  skybox.material->albedo = m_skyboxTexture;
  skybox.transform.scale = {10.0f, 10.0f, 10.0f};
  setupMaterialSet(skybox.material);

  // Load GLTF Model
  m_assetManager->loadGLTF(std::string(CONCORDIA_ASSETS_DIR) +
                               "/models/gltf/DamagedHelmet.glb",
                           m_scene);

  // Ensure all entities loaded from GLTF have their material descriptors setup
  for (auto &ent : m_scene.getEntities()) {
    if (ent.material && ent.material->shaderName == "pbr") {
      setupMaterialSet(ent.material);
    }
  }

  // Center Cube (Parent)
  int centerIdx = m_scene.addEntity("CenterCube");
  auto &center = m_scene.getEntities()[centerIdx];
  center.mesh = centerCubeMesh;
  center.material = matGold;
  center.transform.position = {0, 2.0f, 0}; // Move it up to see helmet below?
  center.transform.angularVelocity = {0, glm::radians(45.0f), 0};
  setupMaterialSet(center.material);

  // Left Cube (Child)
  int leftIdx = m_scene.addEntity("CubeLeft", centerIdx);
  auto &left = m_scene.getEntities()[leftIdx];
  left.mesh = centerCubeMesh;
  left.material = matLit;
  left.transform.position = {-2.0f, 0, 0};
  left.transform.scale = {0.5f, 0.5f, 0.5f};

  // Right Cube (Child)
  int rightIdx = m_scene.addEntity("CubeRight", centerIdx);
  auto &right = m_scene.getEntities()[rightIdx];
  right.mesh = centerCubeMesh;
  right.material = matUnlit;
  right.transform.position = {2.0f, 0, 0};
  right.transform.scale = {0.5f, 0.5f, 0.5f};
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
    }
    drawFrame();
  }
}

} // namespace Nucleus
