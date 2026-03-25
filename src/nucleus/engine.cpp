#include "engine.h"
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>
#include <stdexcept>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "forma/mesh.h"
#include "vista/camera.h"
#include "lumen/pipeline.h"
#include "mundus/scene.h"
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
  glm::mat4 viewProj;
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
  m_camera = std::make_unique<Vista::Camera>();
  m_camera->setPerspective(45.0f, 800.0f / 600.0f, 0.1f, 100.0f);

  m_renderCtx->initDepthBuffer(m_allocator->getVma());

  m_perfFreq = SDL_GetPerformanceFrequency();
  m_startCount = SDL_GetPerformanceCounter();

  initCommands();
  initDescriptors();
  initPipeline();
  initFramebuffers();
  initSync();
  initMesh();
}

Engine::~Engine() {
  VkDevice device = m_renderCtx->getDevice();
  if (device) {
    vkDeviceWaitIdle(device);

    m_renderCtx->cleanupDepthBuffer(m_allocator->getVma());

    for (auto &ent : m_scene.getEntities()) {
      m_allocator->destroyBuffer(ent.vertexBuffer, ent.vertexAllocation);
      if (ent.indexBuffer) {
        m_allocator->destroyBuffer(ent.indexBuffer, ent.indexAllocation);
      }
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      vkDestroyFence(device, m_inFlight[i], nullptr);
      vkDestroySemaphore(device, m_renderDone[i], nullptr);
      vkDestroySemaphore(device, m_imageAvail[i], nullptr);
    }
    vkDestroyCommandPool(device, m_cmdPool, nullptr);
    if (m_pipeline) {
      m_pipeline->destroy(device);
    }
    
    m_texture.destroy(*m_allocator, device);
    m_sampler.destroy(device);
    if (m_descriptorPool) vkDestroyDescriptorPool(device, m_descriptorPool, nullptr);
    if (m_descriptorSetLayout) vkDestroyDescriptorSetLayout(device, m_descriptorSetLayout, nullptr);

    cleanupSwapchainResources();
  }
}

void Engine::initPipeline() {
  Lumen::PipelineConfig config{};
  config.vertexShaderPath = std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/vert.spv";
  config.fragmentShaderPath = std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/frag.spv";
  config.pushConstantSize = sizeof(PushConstants);
  config.bindingDescription = Forma::Vertex::getBindingDescription();
  config.attributeDescriptions = Forma::Vertex::getAttributeDescriptions();
  config.descriptorSetLayouts = { m_descriptorSetLayout };
  config.depthTest = true;

  m_pipeline = std::make_unique<Lumen::Pipeline>();
  m_pipeline->init(*m_renderCtx, config);
}

void Engine::initDescriptors() {
  VkDevice device = m_renderCtx->getDevice();

  // Layout
  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = 1;
  layoutInfo.pBindings = &binding;
  VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_descriptorSetLayout));
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Descriptor Set Layout created.");

  // Pool
  VkDescriptorPoolSize poolSize{};
  poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSize.descriptorCount = 1;

  VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  poolInfo.maxSets = 1;
  VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_descriptorPool));
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Descriptor Pool created.");

  // Allocate Set
  m_descriptorSets.resize(1);
  VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = m_descriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &m_descriptorSetLayout;
  VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, m_descriptorSets.data()));
  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Descriptor Set allocated.");

  // Texture and Sampler
  std::string texturePath = std::string(CONCORDIA_ASSETS_DIR) + "/images/CubeTexture.png";
  m_texture.load(texturePath, *m_allocator, m_renderCtx->getGraphicsQueue(), m_cmdPool, device);
  m_sampler.init(device);

  // Update Set
  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView = m_texture.getView();
  imageInfo.sampler = m_sampler.getSampler();

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = m_descriptorSets[0];
  write.dstBinding = 0;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;
  vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
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
    VkImageView attachments[] = {swapViews[i], m_renderCtx->getDepthImageView()};
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
    m_camera->setPerspective(45.0f, static_cast<float>(extent.width) / static_cast<float>(extent.height), 0.1f, 100.0f);
    initFramebuffers();
}

void Engine::drawFrame() {
  if (m_needsResize) {
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
  float time = (float)(SDL_GetPerformanceCounter() - m_startCount) / (float)m_perfFreq;
  static uint64_t lastCount = SDL_GetPerformanceCounter();
  uint64_t currentCount = SDL_GetPerformanceCounter();
  float frameTimeMs = (float)(currentCount - lastCount) * 1000.0f / (float)m_perfFreq;
  lastCount = currentCount;

  auto& sceneEntities = m_scene.getEntities();
  Vigil::DebugStats stats{};
  stats.fps = 1000.0f / (frameTimeMs > 1e-6f ? frameTimeMs : 1.0f);
  stats.frameTime = frameTimeMs;
  stats.drawCalls = static_cast<uint32_t>(sceneEntities.size());
  for (const auto& ent : sceneEntities) stats.vertexCount += ent.vertexCount;
  stats.cameraPos = m_camera->getPosition();
  stats.cameraFront = m_camera->getFront();

  // UI overlay
  m_overlay->beginFrame();
  m_overlay->drawUI(*m_renderCtx, stats);

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
  vp.maxDepth = 1.0f;
  vkCmdSetViewport(cb, 0, 1, &vp);

  VkRect2D scissor{{0, 0}, swapExtent};
  vkCmdSetScissor(cb, 0, 1, &scissor);

  m_pipeline->bind(cb);
  vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->getLayout(), 0, 1, &m_descriptorSets[0], 0, nullptr);

  glm::mat4 viewProj = m_camera->getProj() * m_camera->getView();

  VkDeviceSize offset = 0;
  for (auto &ent : sceneEntities) {
    // Dynamic update for testing
    if (ent.name == "SpinningCube") {
        ent.transform.rotation.y = time * glm::radians(90.0f);
        ent.transform.rotation.x = time * glm::radians(45.0f);
    }

    PushConstants pc{};
    pc.model = ent.transform.getMatrix();
    pc.viewProj = viewProj;
    
    vkCmdPushConstants(cb, m_pipeline->getLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);
    vkCmdBindVertexBuffers(cb, 0, 1, &ent.vertexBuffer, &offset);
    if (ent.indexBuffer) {
      vkCmdBindIndexBuffer(cb, ent.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(cb, ent.indexCount, 1, 0, 0, 0);
    } else {
      vkCmdDraw(cb, ent.vertexCount, 1, 0, 0);
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
  std::vector<Forma::Vertex> vertices;
  std::vector<uint32_t> indices;
  Forma::Mesh::loadFromOBJ("assets/models/cube.obj", vertices, indices);

  size_t vSize = vertices.size() * sizeof(Forma::Vertex);
  size_t iSize = indices.size() * sizeof(uint32_t);

  // Helper to create entity
  auto createEntity = [&](const std::string& name, glm::vec3 pos) {
      Mundus::Entity ent{};
      ent.name = name;
      ent.transform.position = pos;
      ent.vertexCount = static_cast<uint32_t>(vertices.size());
      ent.indexCount = static_cast<uint32_t>(indices.size());

      // Vertex Buffer
      {
        VkBuffer stagingBuffer;
        VmaAllocation stagingAlloc;
        m_allocator->createBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer,
                                  stagingAlloc);

        void *data;
        vmaMapMemory(m_allocator->getVma(), stagingAlloc, &data);
        memcpy(data, vertices.data(), vSize);
        vmaUnmapMemory(m_allocator->getVma(), stagingAlloc);

        m_allocator->createBuffer(vSize,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                  VMA_MEMORY_USAGE_GPU_ONLY, ent.vertexBuffer,
                                  ent.vertexAllocation);

        m_allocator->copyBuffer(stagingBuffer, ent.vertexBuffer, vSize,
                                m_renderCtx->getGraphicsQueue(), m_cmdPool,
                                m_renderCtx->getDevice());

        m_allocator->destroyBuffer(stagingBuffer, stagingAlloc);
      }

      // Index Buffer
      {
        VkBuffer stagingBuffer;
        VmaAllocation stagingAlloc;
        m_allocator->createBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer,
                                  stagingAlloc);

        void *data;
        vmaMapMemory(m_allocator->getVma(), stagingAlloc, &data);
        memcpy(data, indices.data(), iSize);
        vmaUnmapMemory(m_allocator->getVma(), stagingAlloc);

        m_allocator->createBuffer(iSize,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                  VMA_MEMORY_USAGE_GPU_ONLY, ent.indexBuffer,
                                  ent.indexAllocation);

        m_allocator->copyBuffer(stagingBuffer, ent.indexBuffer, iSize,
                                m_renderCtx->getGraphicsQueue(), m_cmdPool,
                                m_renderCtx->getDevice());

        m_allocator->destroyBuffer(stagingBuffer, stagingAlloc);
      }
      return ent;
  };

  m_scene.addEntity(createEntity("SpinningCube", {0.0f, 0.0f, 0.0f}));
  m_scene.addEntity(createEntity("CubeLeft", {-1.5f, 0.5f, -1.0f}));
  m_scene.addEntity(createEntity("CubeRight", {1.5f, -0.5f, -1.0f}));
}

void Engine::run() {
  SDL_Event ev;
  while (m_isRunning) {
    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL3_ProcessEvent(&ev);
      if (ev.type == SDL_EVENT_QUIT)
        m_isRunning = false;
      if (ev.type == SDL_EVENT_WINDOW_RESIZED ||
          ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        m_needsResize = true;
      }
    }
    drawFrame();
  }
}

} // namespace Nucleus
