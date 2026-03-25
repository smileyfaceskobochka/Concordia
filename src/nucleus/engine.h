#pragma once

#include "petra/window.h"
#include "render/context.h"
#include "vigil/overlay.h"
#include "memoria/allocator.h"
#include "forma/mesh.h"
#include "mundus/scene.h"
#include "vista/camera.h"
#include "lumen/pipeline.h"
#include "memoria/texture.h"
#include "memoria/sampler.h"
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace Nucleus {

class Engine {
public:
    Engine();
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

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

    std::unique_ptr<Petra::Window> m_window;
    std::unique_ptr<Render::Context> m_renderCtx;
    std::unique_ptr<Memoria::Allocator> m_allocator;
    std::unique_ptr<Vigil::Overlay> m_overlay;

    Mundus::Scene m_scene;
    std::unique_ptr<Vista::Camera> m_camera;
    
    std::unique_ptr<Lumen::Pipeline> m_pipeline;

    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_descriptorSets;

    Memoria::Texture m_texture;
    Memoria::Sampler m_sampler;
    
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
