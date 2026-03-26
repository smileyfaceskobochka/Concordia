#include "overlay.h"
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <stdexcept>

#define VK_CHECK(call)                                                         \
  do {                                                                         \
    VkResult _r = (call);                                                      \
    if (_r != VK_SUCCESS) {                                                    \
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,                               \
                   "Vulkan error %d at " __FILE__ ":%d", _r, __LINE__);        \
      throw std::runtime_error("Vulkan call failed in Vigil Overlay");         \
    }                                                                          \
  } while (0)

namespace Vigil {

Overlay::Overlay(const Petra::Window &window,
                 const Render::Context &renderCtx) {
  m_device = renderCtx.getDevice();

  VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};
  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1000 * 11;
  pool_info.poolSizeCount =
      static_cast<uint32_t>(sizeof(pool_sizes) / sizeof(pool_sizes[0]));
  pool_info.pPoolSizes = pool_sizes;

  VK_CHECK(
      vkCreateDescriptorPool(m_device, &pool_info, nullptr, &m_descriptorPool));

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  const char *defaultFontPath =
      "assets/fonts/JetBrainsMonoNF/JetBrainsMonoNerdFont-Regular.ttf";
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

void Overlay::drawUI(const Render::Context &renderCtx, const DebugStats &stats,
                     Mundus::Scene &scene) {
  static int frameCounter = 0;
  if (frameCounter++ % 100 == 0)
    SDL_Log("Overlay: drawUI frame %d", frameCounter);

  const float PAD = 10.0f;
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImVec2 work_pos = viewport->WorkPos;

  ImGui::SetNextWindowPos(ImVec2(work_pos.x + PAD, work_pos.y + PAD),
                          ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.65f);

  ImGuiWindowFlags window_flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
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
    ImGui::Text("  Pos:   (%.1f, %.1f, %.1f)", stats.cameraPos.x,
                stats.cameraPos.y, stats.cameraPos.z);
    ImGui::Text("  Front: (%.2f, %.2f, %.2f)", stats.cameraFront.x,
                stats.cameraFront.y, stats.cameraFront.z);

    ImGui::Text("Graphics:");
    ImGui::Text("  GPU: %s", renderCtx.getGPUName().c_str());

    ImGui::Separator();
    ImGui::Text("Controls:");
    if (stats.cameraSpeed)
      ImGui::SliderFloat("Move Speed", stats.cameraSpeed, 0.1f, 50.0f);
    if (stats.cameraSens)
      ImGui::SliderFloat("Sensitivity", stats.cameraSens, 0.01f, 1.0f);

    if (stats.captureMouse && !(*stats.captureMouse)) {
      if (ImGui::Button("Enter Viewing Mode (Press ESC to exit)",
                        ImVec2(-1.0f, 0.0f))) {
        *stats.captureMouse = true;
      }
    } else if (stats.captureMouse) {
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                         "VIEWING MODE ACTIVE (ESC TO EXIT)");
    }
  }
  ImGui::End();

  // --- RIGHT WINDOW: INSPECTOR & HIERARCHY ---
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - PAD,
             work_pos.y + PAD),
      ImGuiCond_Always, ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(350, -1),
                           ImGuiCond_FirstUseEver); // Default width
  ImGui::SetNextWindowBgAlpha(0.75f);

  if (ImGui::Begin("Scene Inspector", nullptr,
                   ImGuiWindowFlags_AlwaysAutoResize)) {
    if (ImGui::CollapsingHeader("Global Lighting",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      ImGui::DragFloat3("Direction", &scene.globalLightDir.x, 0.05f);
      ImGui::ColorEdit3("Color", &scene.globalLightColor.x);
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader("Scene Hierarchy",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      auto &entities = scene.getEntities();

      std::function<void(int)> drawNode = [&](int index) {
        auto &ent = entities[index];

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;
        if (ent.children.empty()) {
          flags |= ImGuiTreeNodeFlags_Leaf;
        }
        if (m_selectedEntity == index) {
          flags |= ImGuiTreeNodeFlags_Selected;
        }

        bool nodeOpen = ImGui::TreeNodeEx((void *)(intptr_t)index, flags, "%s",
                                          ent.name.c_str());
        if (ImGui::IsItemClicked()) {
          m_selectedEntity = index;
        }

        if (nodeOpen) {
          for (int childIdx : ent.children) {
            drawNode(childIdx);
          }
          ImGui::TreePop();
        }
      };

      for (size_t i = 0; i < entities.size(); ++i) {
        if (entities[i].parentIndex == -1) {
          drawNode(static_cast<int>(i));
        }
      }
    }

    ImGui::Separator();
    if (m_selectedEntity >= 0 &&
        m_selectedEntity < static_cast<int>(scene.getEntities().size())) {
      auto &ent = scene.getEntities()[m_selectedEntity];
      if (ImGui::CollapsingHeader("Inspector",
                                  ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Selected: %s", ent.name.c_str());
        if (ent.mesh) {
          ImGui::TextDisabled("Vtx: %u", ent.mesh->vertexCount);
        }

        ImGui::DragFloat3("Local Pos", &ent.transform.position.x, 0.1f);

        glm::vec3 rotDeg = glm::degrees(ent.transform.rotation);
        if (ImGui::DragFloat3("Local Rot", &rotDeg.x, 1.0f)) {
          ent.transform.rotation = glm::radians(rotDeg);
        }

        glm::vec3 spinDeg = glm::degrees(ent.transform.angularVelocity);
        if (ImGui::DragFloat3("Spin Speed", &spinDeg.x, 5.0f)) {
          ent.transform.angularVelocity = glm::radians(spinDeg);
        }

        ImGui::DragFloat3("Scale", &ent.transform.scale.x, 0.05f);

        if (ent.material) {
          ImGui::Spacing();
          ImGui::Separator();
          ImGui::Text("Material: %s", ent.material->shaderName.c_str());

          ImGui::ColorEdit3("Base Color", &ent.material->baseColor.x);
          ImGui::SliderFloat("Roughness", &ent.material->roughness, 0.0f, 1.0f);
          ImGui::SliderFloat("Metallic", &ent.material->metallic, 0.0f, 1.0f);

          ImGui::TextDisabled("Textures:");
          auto texName = [](auto tex) { return tex ? "Assigned" : "Default"; };
          ImGui::Text("  Albedo: %s", texName(ent.material->albedo));
          ImGui::Text("  Normal: %s", texName(ent.material->normal));
          ImGui::Text("  Met-Rou: %s",
                      texName(ent.material->metallicRoughness));
          ImGui::Text("  AO:     %s", texName(ent.material->ao));
          ImGui::Text("  Emiss:  %s", texName(ent.material->emissive));

          if (ImGui::BeginCombo("Shader", ent.material->shaderName.c_str())) {
            const char *shaders[] = {"pbr", "unlit", "skybox"};
            for (int n = 0; n < 3; n++) {
              bool is_selected = (ent.material->shaderName == shaders[n]);
              if (ImGui::Selectable(shaders[n], is_selected)) {
                ent.material->shaderName = shaders[n];
              }
              if (is_selected)
                ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
          }
        }

        ImGui::Spacing();
        ImGui::Text("Global Position:");
        ImGui::TextDisabled("(%.2f, %.2f, %.2f)", ent.globalTransform[3][0],
                            ent.globalTransform[3][1],
                            ent.globalTransform[3][2]);
      }
    } else {
      ImGui::TextDisabled("No entity selected.");
    }
  }
  ImGui::End();
}

void Overlay::endFrameAndRecord(VkCommandBuffer cmdBuf) {
  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
}

} // namespace Vigil
