#include "overlay.h"
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <SDL3/SDL.h>
#include <stdexcept>

#define VK_CHECK(call)                                                         \
  do {                                                                         \
    VkResult _r = (call);                                                      \
    if (_r != VK_SUCCESS) {                                                    \
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,                               \
                   "Vulkan error %d at " __FILE__ ":%d", _r, __LINE__);       \
      throw std::runtime_error("Vulkan call failed in Vigil Overlay");         \
    }                                                                          \
  } while (0)

namespace Vigil {

Overlay::Overlay(const Petra::Window& window, const Render::Context& renderCtx) {
    m_device = renderCtx.getDevice();

    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };
    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000 * 11;
    pool_info.poolSizeCount = static_cast<uint32_t>(sizeof(pool_sizes)/sizeof(pool_sizes[0]));
    pool_info.pPoolSizes = pool_sizes;

    VK_CHECK(vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_descriptorPool));

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    const char* defaultFontPath = "assets/fonts/JetBrainsMonoNF/JetBrainsMonoNerdFont-Regular.ttf";
    io.Fonts->AddFontFromFileTTF(defaultFontPath, 16.0f);

    ImGui_ImplSDL3_InitForVulkan(window.getHandle());

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = renderCtx.getInstance();
    init_info.PhysicalDevice = renderCtx.getPhysicalDevice();
    init_info.Device = m_device;
    init_info.QueueFamily = renderCtx.getGraphicsQueueFamily();
    init_info.Queue = renderCtx.getGraphicsQueue();
    init_info.PipelineCache = VK_NULL_HANDLE;
    init_info.DescriptorPool = m_descriptorPool;
    init_info.PipelineInfoMain.RenderPass = renderCtx.getRenderPass();
    init_info.PipelineInfoMain.Subpass = 0;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.MinImageCount = renderCtx.getImageCount();
    init_info.ImageCount = renderCtx.getImageCount();
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = nullptr;
    ImGui_ImplVulkan_Init(&init_info);
}

Overlay::~Overlay() {
    if (m_device) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    }
}

void Overlay::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void Overlay::drawUI(const Render::Context& renderCtx, const DebugStats& stats) {
    const float PAD = 10.0f;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 work_pos = viewport->WorkPos;
    
    ImGui::SetNextWindowPos(ImVec2(work_pos.x + PAD, work_pos.y + PAD), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.65f);
    
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | 
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | 
                                    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

    if (ImGui::Begin("Concordia Engine Stats", nullptr, window_flags)) {
        ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f), "CONCORDIA ENGINE");
        ImGui::Separator();
        
        ImGui::Text("Performance:");
        ImGui::Text("  FPS: %.1f", stats.fps);
        ImGui::Text("  FT:  %.3f ms", stats.frameTime);
        ImGui::Text("  DC:  %u", stats.drawCalls);
        ImGui::Text("  Vtx: %u", stats.vertexCount);
        
        ImGui::Separator();
        ImGui::Text("Camera:");
        ImGui::Text("  Pos:   (%.1f, %.1f, %.1f)", stats.cameraPos.x, stats.cameraPos.y, stats.cameraPos.z);
        ImGui::Text("  Front: (%.2f, %.2f, %.2f)", stats.cameraFront.x, stats.cameraFront.y, stats.cameraFront.z);

        ImGui::Separator();
        ImGui::Text("Graphics:");
        ImGui::Text("  GPU: %s", renderCtx.getGPUName().c_str());
        
        ImGui::End();
    }
}

void Overlay::endFrameAndRecord(VkCommandBuffer cmdBuf) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
}

} // namespace Vigil
