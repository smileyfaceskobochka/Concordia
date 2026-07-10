#include "overlay.h"
#include "auxilia/ctoon.hpp"
#include "nerd_font_icons.h"
#include "memoria/asset_manager.h"
#include "mundus/schema.h"
#include "vigil/style.h"

static inline const char *obj_str(ctoon_value *obj, const char *key) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  return v && v->type == CTOON_STRING ? v->str_val : nullptr;
}
static inline double obj_num(ctoon_value *obj, const char *key, double def = 0.0) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  return v && v->type == CTOON_NUMBER ? v->num_val : def;
}
static inline bool obj_bool(ctoon_value *obj, const char *key, bool def = false) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  return v && v->type == CTOON_BOOL ? v->bool_val : def;
}
#include <SDL3/SDL.h>
#include <algorithm>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#include <imgui_internal.h>
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

#ifndef CONCORDIA_ASSETS_DIR
#define CONCORDIA_ASSETS_DIR "assets"
#endif

namespace Vigil {

void Overlay::loadUIConfig(const std::string &path) {
  Auxilia::ctoon_doc doc;
  if (!doc.load_file(path.c_str())) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Overlay: failed to load UI config: %s", path.c_str());
    return;
  }

  std::string errors;
  if (!Mundus::Schema::validateUI(doc.get(), errors)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "UI config schema violations:\n%s", errors.c_str());
  }

  ImGuiIO &io = ImGui::GetIO();

  // Load fonts
  ctoon_value *fonts = ctoon_obj_get(doc.get(), "fonts");
  SDL_Log("Overlay: fonts=%s", fonts ? (fonts->type == CTOON_ARRAY ? "array" : "non-array") : "null");
  if (fonts && fonts->type == CTOON_ARRAY) {
    SDL_Log("Overlay: font count=%zu", fonts->len);
    for (size_t i = 0; i < fonts->len; ++i) {
      ctoon_value *entry = &fonts->arr[i];
      const char *fp = obj_str(entry, "path");
      float sz = (float)obj_num(entry, "size", 16.0f);
      float offY = (float)obj_num(entry, "glyph_offset_y", 0.0f);

      if (!fp) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Overlay: font[%zu] has no path, skipping", i);
        continue;
      }

      ImFontConfig cfg;
      cfg.GlyphOffset.y = offY;
      static const ImWchar nfRanges[] = {0x0020, 0x00FF, ICON_NF_MIN, ICON_NF_MAX, 0};
      ImFont *loaded = io.Fonts->AddFontFromFileTTF(fp, sz, &cfg, nfRanges);
      SDL_Log("Overlay: font[%zu] path=%s size=%.1f offY=%.1f -> %s",
              i, fp, sz, offY, loaded ? "loaded" : "FAILED");
    }
  }

  m_statsPadding =
      (float)doc.get_number("stats_padding", m_statsPadding);
  m_inspectorWidth =
      (float)doc.get_number("inspector_width", m_inspectorWidth);
  {
    auto aws = doc.get_string("asset_window_size");
    if (aws) {
      glm::vec2 v;
      if (sscanf(aws, "@vec2(%f,%f)", &v.x, &v.y) == 2)
        m_assetWindowSize = v;
    }
  }
  m_renameBufSize = (int)doc.get_number("rename_buf_size", 256);
  m_sliderSpeedMin = (float)doc.get_number("slider_speed_min", 0.1f);
  m_sliderSpeedMax = (float)doc.get_number("slider_speed_max", 50.0f);
  m_sliderSensMin = (float)doc.get_number("slider_sens_min", 0.01f);
  m_sliderSensMax = (float)doc.get_number("slider_sens_max", 1.0f);

  // Debug modes
  m_debugModes.clear();
  ctoon_value *modes = ctoon_obj_get(doc.get(), "debug_modes");
  if (modes && modes->type == CTOON_ARRAY) {
    for (size_t i = 0; i < modes->len; ++i)
      if (modes->arr[i].type == CTOON_STRING && modes->arr[i].str_val)
        m_debugModes.push_back(modes->arr[i].str_val);
  }
  if (m_debugModes.empty())
    m_debugModes = {"None", "Metallic", "Roughness", "Normals", "Vertex Color"};

  // Apply ImGui style
  ImGuiStyle &style = ImGui::GetStyle();
  style.FramePadding.x =
      (float)doc.get_number("frame_padding", style.FramePadding.x);
  style.ItemSpacing.x =
      (float)doc.get_number("item_spacing", style.ItemSpacing.x);
  style.WindowRounding =
      (float)doc.get_number("window_rounding", style.WindowRounding);
  style.FrameRounding =
      (float)doc.get_number("frame_rounding", style.FrameRounding);
  style.ScrollbarSize =
      (float)doc.get_number("scrollbar_size", style.ScrollbarSize);
  style.GrabMinSize =
      (float)doc.get_number("grab_min_size", style.GrabMinSize);
}

Overlay::Overlay(const Petra::Window &window,
                 const Render::Context &renderCtx) {
  m_device = renderCtx.getDevice();

  // Load UI config first to get descriptor pool sizes
  Auxilia::ctoon_doc uiDoc;
  std::string uiPath =
      std::string(CONCORDIA_ASSETS_DIR) + "/config/ui.toon";
  float poolSets = 11000.0f;
  float poolSamplers = 1000.0f;
  float poolCombined = 1000.0f;
  if (uiDoc.load_file(uiPath.c_str())) {
    poolSets = (float)uiDoc.get_number("descriptor_pool_sets", poolSets);
    poolSamplers =
        (float)uiDoc.get_number("descriptor_pool_samplers", poolSamplers);
    poolCombined = (float)uiDoc.get_number(
        "descriptor_pool_combined_image_samplers", poolCombined);
  }

  VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, (uint32_t)poolSamplers},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (uint32_t)poolCombined},
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
  pool_info.maxSets = (uint32_t)poolSets;
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

  applyCustomStyle();

  // Load UI config (fonts, style overrides, etc.)
  loadUIConfig(uiPath);

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

void Overlay::scanDirTree(const std::string &rootPath,
                          std::vector<FileEntry> &entries) {
  if (!std::filesystem::exists(rootPath))
    return;
  std::vector<std::filesystem::path> dirs;
  for (auto &entry : std::filesystem::directory_iterator(rootPath)) {
    if (entry.is_directory()) {
      dirs.push_back(entry.path());
    }
  }
  std::sort(dirs.begin(), dirs.end());
  for (auto &dp : dirs) {
    FileEntry fe;
    fe.name = dp.filename().string();
    fe.path = dp.string();
    fe.isDirectory = true;
    scanDirTree(fe.path, fe.children);
    entries.push_back(std::move(fe));
  }
}

void Overlay::drawDirTree(const std::vector<FileEntry> &entries) {
  for (auto &entry : entries) {
    if (!entry.isDirectory)
      continue;
    bool isLeaf = entry.children.empty();
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_OpenOnDoubleClick |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (isLeaf)
      flags |= ImGuiTreeNodeFlags_Leaf;
    if (entry.path == m_selectedDir)
      flags |= ImGuiTreeNodeFlags_Selected;

    ImGuiID nodeId = ImGui::GetID(entry.name.c_str());
    bool wasOpen = ImGui::GetStateStorage()->GetBool(nodeId, false);
    const char *dirIcon = wasOpen ? ICON_NF_FOLDER_OPEN : ICON_NF_FOLDER;
    bool open = ImGui::TreeNodeEx(entry.name.c_str(), flags,
                                   "%s  %s", dirIcon, entry.name.c_str());
    if (ImGui::IsItemClicked()) {
      m_selectedDir = entry.path;
      m_selectedFileIndex = -1;
      m_previewPath.clear();
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
      m_selectedDir = entry.path;
      m_selectedFileIndex = -1;
      m_previewPath.clear();
    }
    if (open) {
      drawDirTree(entry.children);
      ImGui::TreePop();
    }
  }
}

void Overlay::drawUI(const Render::Context &renderCtx, DebugStats &stats,
                     flecs::world &ecs,
                         Memoria::AssetManager &assetManager,
                         VkSampler sampler, uint32_t *debugMode,
                         uint32_t *selectedSkybox, uint32_t skyboxCount,
                         const char *const *skyboxNames,
                         uint32_t shaderCount,
                         const char *const *shaderNames) {
  static int dbgFrame = 0;
  dbgFrame++;
  SDL_Log("DBG OV [%d] drawUI begin", dbgFrame);
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImVec2 work_pos = viewport->WorkPos;
  constexpr float PAD = 10.0f;

  // ── STATS WINDOW (collapsible categories) ──────────────────────────────
  ImGui::SetNextWindowPos(ImVec2(work_pos.x + PAD, work_pos.y + PAD),
                          ImGuiCond_Always);
  ImGui::SetNextWindowBgAlpha(0.65f);

  ImGuiWindowFlags window_flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

  if (ImGui::Begin("Concordia Engine Stats", nullptr, window_flags)) {
    ImGui::TextColored(ImVec4(0.0f, 0.8f, 1.0f, 1.0f),
                       ICON_NF_CUBOID "  CONCORDIA ENGINE");
    ImGui::Separator();

    if (ImGui::CollapsingHeader(ICON_NF_ACTIVITY "  Performance")) {
      ImGui::Text("  FPS:");
      ImGui::SameLine(120);
      ImGui::TextColored(ImVec4(0.8f, 1.0f, 0.4f, 1.0f), "%.1f", stats.fps);

      ImGui::Text("  Frame Time:");
      ImGui::SameLine(120);
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%.3f ms",
                         stats.frameTime);

      ImGui::Text("  Draw Calls:");
      ImGui::SameLine(120);
      ImGui::Text("%u", stats.drawCalls);

      ImGui::Text("  Vertices:");
      ImGui::SameLine(120);
      ImGui::Text("%u", stats.vertexCount);
    }

    if (ImGui::CollapsingHeader(ICON_NF_CAMERA "  Camera")) {
      ImGui::Text("  Position:");
      ImGui::SameLine(120);
      ImGui::Text("(%.1f, %.1f, %.1f)", stats.cameraPos.x, stats.cameraPos.y,
                  stats.cameraPos.z);
      ImGui::Text("  Front:");
      ImGui::SameLine(120);
      ImGui::Text("(%.2f, %.2f, %.2f)", stats.cameraFront.x,
                  stats.cameraFront.y, stats.cameraFront.z);
    }

    if (ImGui::CollapsingHeader(ICON_NF_MONITOR "  Graphics")) {
      ImGui::Text("  GPU:");
      ImGui::SameLine(120);
      ImGui::Text("%s", renderCtx.getGPUName().c_str());
    }

    if (ImGui::CollapsingHeader(ICON_NF_SETTINGS "  Controls")) {
      if (stats.cameraSpeed)
        ImGui::SliderFloat("Move Speed", stats.cameraSpeed, m_sliderSpeedMin,
                           m_sliderSpeedMax);
      if (stats.cameraSens)
        ImGui::SliderFloat("Sensitivity", stats.cameraSens, m_sliderSensMin,
                           m_sliderSensMax);

      if (stats.captureMouse && !(*stats.captureMouse)) {
        if (ImGui::Button("Enter Viewing Mode (Press ESC to exit)",
                          ImVec2(-1.0f, 0.0f))) {
          SDL_Log("DBG OV [%d] user clicked Enter Viewing Mode", dbgFrame);
          *stats.captureMouse = true;
        }
      } else if (stats.captureMouse) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f),
                           "Viewing Mode Active (ESC to exit)");
        SDL_Log("DBG OV [%d] viewing mode is active", dbgFrame);
      }
    }
  }
  ImGui::End();

  // ── RIGHT WINDOW: INSPECTOR & HIERARCHY ──────────────────────────────
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - PAD,
             work_pos.y + PAD),
      ImGuiCond_Always, ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(m_inspectorWidth, -1), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.75f);

  if (ImGui::Begin("Scene Inspector", nullptr,
                   ImGuiWindowFlags_AlwaysAutoResize)) {
    if (ImGui::CollapsingHeader(ICON_NF_SUN "  Global Lighting",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      auto &lightDir = ecs.get_mut<Mundus::LightDir>();
      auto &lightColor = ecs.get_mut<Mundus::LightColor>();
      ImGui::DragFloat3("Direction", &lightDir.value.x, 0.05f);
      ImGui::ColorEdit3("Color", &lightColor.value.x);
      ImGui::Spacing();
      ImGui::Separator();
      const char *preview = (*selectedSkybox < skyboxCount && skyboxNames)
                                ? skyboxNames[*selectedSkybox]
                                : "Select Skybox";
      if (ImGui::BeginCombo("Skybox", preview)) {
        for (uint32_t n = 0; n < skyboxCount; n++) {
          if (!skyboxNames[n])
            continue;
          bool is_selected = (*selectedSkybox == n);
          if (ImGui::Selectable(skyboxNames[n], is_selected)) {
            *selectedSkybox = n;
          }
          if (is_selected)
            ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
      }
    }

    ImGui::Separator();
    if (ImGui::CollapsingHeader(ICON_NF_LIST_TREE "  Scene Hierarchy",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      std::function<void(flecs::entity)> drawNode;
      drawNode = [&](flecs::entity e) {
        const Mundus::Name *n = e.try_get<Mundus::Name>();
        if (!n) return;
        const char *entName = n->value.empty() ? "unnamed" : n->value.c_str();

        ImGui::PushID(static_cast<int>(e.raw_id()));

        bool hasChildren = false;
        e.children([&](flecs::entity) { hasChildren = true; });

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;
        if (!hasChildren)
          flags |= ImGuiTreeNodeFlags_Leaf;
        if (e == m_selectedEntity)
          flags |= ImGuiTreeNodeFlags_Selected;

        bool nodeOpen = ImGui::TreeNodeEx("##node", flags, "%s", entName);
        if (ImGui::IsItemClicked())
          m_selectedEntity = e;

        if (ImGui::BeginPopupContextItem()) {
          if (ImGui::MenuItem("Focus Camera")) {
            const Mundus::GlobalTransform *gt = e.try_get<Mundus::GlobalTransform>();
            if (gt) {
              stats.hasFocusTarget = true;
              stats.focusTarget = glm::vec3(gt->value[3][0], gt->value[3][1],
                                            gt->value[3][2]);
            }
          }
          if (ImGui::MenuItem("Rename"))
            ImGui::OpenPopup("Rename Entity");
          if (ImGui::MenuItem("Delete")) {
            std::function<void(flecs::entity)> destroyTree;
            destroyTree = [&](flecs::entity ent) {
              ent.children([&](flecs::entity child) { destroyTree(child); });
              ent.destruct();
            };
            destroyTree(e);
            if (e == m_selectedEntity) m_selectedEntity = flecs::entity();
          }
          ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Rename Entity", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
          static char renameBuf[256];
          strncpy(renameBuf, entName, m_renameBufSize - 1);
          renameBuf[m_renameBufSize - 1] = '\0';
          ImGui::Text("New name:");
          ImGui::SameLine();
          if (ImGui::InputText("##rename", renameBuf, m_renameBufSize,
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
            e.set<Mundus::Name>({renameBuf});
            ImGui::CloseCurrentPopup();
          }
          if (ImGui::Button("Cancel"))
            ImGui::CloseCurrentPopup();
          ImGui::EndPopup();
        }

        if (nodeOpen) {
          e.children([&](flecs::entity child) { drawNode(child); });
          ImGui::TreePop();
        }
        ImGui::PopID();
      };

      // Draw root entities (no parent)
      SDL_Log("DBG OV [%d] before root entity query", dbgFrame);
      ecs.defer_begin();
      ecs.query_builder<>()
        .with<Mundus::Name>()
        .without(flecs::ChildOf, flecs::Wildcard)
        .build()
        .each([&](flecs::iter &it, size_t row) {
          SDL_Log("DBG OV [%d] drawNode: root entity %u", dbgFrame, it.entity(row).raw_id());
          drawNode(it.entity(row));
        });
      ecs.defer_end();
      SDL_Log("DBG OV [%d] after root entity query", dbgFrame);
    }

    // ── INSPECTOR ──────────────────────────────────────────────────────
    ImGui::Separator();
    if (m_selectedEntity.is_alive()) {
      flecs::entity e = m_selectedEntity;
      const Mundus::Name *n = e.try_get<Mundus::Name>();
      const Mundus::Visibility *v = e.try_get<Mundus::Visibility>();
      const Mundus::Transform *t = e.try_get<Mundus::Transform>();
      const Mundus::GlobalTransform *gt = e.try_get<Mundus::GlobalTransform>();
      const Mundus::MeshAssetRef *mr = e.try_get<Mundus::MeshAssetRef>();
      const Mundus::MaterialRef *matRef = e.try_get<Mundus::MaterialRef>();

      if (n) {
      ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
      ImGui::PushID(static_cast<int>(e.raw_id()));

      // ── Entity Header ──────────────────────────────────────────────
      ImGui::BeginGroup();
      ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s",
                         n->value.c_str());
      ImGui::SameLine();
      bool vis = v ? v->visible : true;
      if (ImGui::SmallButton(vis ? ICON_NF_EYE : ICON_NF_EYE_OFF)) {
        SDL_Log("DBG OV [%d] toggling visibility on entity %u", dbgFrame, e.raw_id());
        auto &mvis = e.get_mut<Mundus::Visibility>();
        mvis.visible = !mvis.visible;
      }
      if (mr && mr->value) {
        ImGui::SameLine();
        ImGui::TextDisabled("  (%u verts)", mr->value->vertexCount);
      }
      if (matRef && matRef->value) {
        ImGui::SameLine();
        ImGui::TextDisabled("  [%s]", matRef->value->shaderName.c_str());
      }
      ImGui::EndGroup();
      ImGui::Separator();

      auto measureLabelWidth = [](const std::vector<const char *> &labels) {
        float w = 0;
        for (auto *l : labels) {
          float lw = ImGui::CalcTextSize(l).x;
          if (lw > w) w = lw;
        }
        return w;
      };

      auto property_grid_row = [&](const char *label, auto widget_func) {
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::Text("%s", label);
        ImGui::PopStyleColor();
        float widgetX =
            ImGui::GetCursorPosX() + measureLabelWidth({"Location", "Rotation",
                                                          "Scale", "Spin"}) +
            ImGui::GetStyle().ItemSpacing.x * 2;
        ImGui::SameLine(widgetX);
        widget_func();
        ImGui::EndGroup();
      };

      // ── Transform ───────────────────────────────────────────────────
      if (t) {
        if (ImGui::CollapsingHeader(ICON_NF_MOVE "  Transform",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
          Mundus::Transform mutT = *t;
          property_grid_row("Location", [&]() {
            ImGui::DragFloat3("##pos", &mutT.position.x, 0.1f);
          });
          glm::vec3 rotDeg = glm::degrees(mutT.rotation);
          property_grid_row("Rotation", [&]() {
            if (ImGui::DragFloat3("##rot", &rotDeg.x, 1.0f))
              mutT.rotation = glm::radians(rotDeg);
          });
          property_grid_row("Scale", [&]() {
            ImGui::DragFloat3("##scale", &mutT.scale.x, 0.05f);
          });
          property_grid_row("Spin", [&]() {
            glm::vec3 sDeg = glm::degrees(mutT.angularVelocity);
            if (ImGui::DragFloat3("##spin", &sDeg.x, 5.0f))
              mutT.angularVelocity = glm::radians(sDeg);
          });
          e.set<Mundus::Transform>(mutT);
        }
      }

      // ── Material ────────────────────────────────────────────────────
      if (matRef && matRef->value) {
        auto mat = matRef->value;
        if (ImGui::CollapsingHeader(ICON_NF_PALETTE "  Material",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
          float matLabelWidth =
              measureLabelWidth({"Shader", "Debug", "Base Color", "Roughness",
                                 "Metallic", "Albedo", "Normal", "Met-Rou",
                                 "AO", "Emiss"});
          auto mat_grid_row = [&](const char *label, auto widget_func) {
            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::Text("%s", label);
            ImGui::PopStyleColor();
            ImGui::SameLine(ImGui::GetCursorPosX() + matLabelWidth +
                            ImGui::GetStyle().ItemSpacing.x * 2);
            widget_func();
            ImGui::EndGroup();
          };

          mat_grid_row("Shader", [&]() {
            if (ImGui::BeginCombo("##shader",
                                  mat->shaderName.c_str())) {
              for (uint32_t sn = 0; sn < shaderCount; sn++) {
                if (!shaderNames[sn]) continue;
                bool sel = (mat->shaderName == shaderNames[sn]);
                if (ImGui::Selectable(shaderNames[sn], sel))
                  mat->shaderName = shaderNames[sn];
                if (sel) ImGui::SetItemDefaultFocus();
              }
              ImGui::EndCombo();
            }
          });

          mat_grid_row("Debug", [&]() {
            const char *preview = (*debugMode < m_debugModes.size())
                                      ? m_debugModes[*debugMode].c_str()
                                      : "None";
            if (ImGui::BeginCombo("##debug", preview)) {
              for (size_t dn = 0; dn < m_debugModes.size(); ++dn) {
                if (ImGui::Selectable(m_debugModes[dn].c_str(),
                                      *debugMode == dn))
                  *debugMode = static_cast<uint32_t>(dn);
              }
              ImGui::EndCombo();
            }
          });

          ImGui::Separator();
          ImGui::BeginGroup();
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
          float textY = ImGui::GetCursorPosY();
          ImDrawList *dl = ImGui::GetWindowDrawList();
          ImVec2 swatchPos(ImGui::GetCursorScreenPos().x,
                           ImGui::GetCursorScreenPos().y +
                               (ImGui::GetTextLineHeight() - 10) * 0.5f);
          ImU32 swatchCol = ImColor(mat->baseColor.x, mat->baseColor.y,
                                    mat->baseColor.z, mat->baseColor.w);
          dl->AddRectFilled(swatchPos,
                            ImVec2(swatchPos.x + 10, swatchPos.y + 10),
                            swatchCol);
          dl->AddRect(swatchPos, ImVec2(swatchPos.x + 10, swatchPos.y + 10),
                      IM_COL32(180, 180, 180, 255));
          ImGui::SetCursorPosY(textY);
          ImGui::Text("  Base Color");
          ImGui::PopStyleColor();
          ImGui::SameLine(ImGui::GetCursorPosX() + matLabelWidth +
                          ImGui::GetStyle().ItemSpacing.x * 2);
          ImGui::ColorEdit4("##color", &mat->baseColor.x, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview);
          ImGui::EndGroup();

          mat_grid_row("Roughness", [&]() {
            ImGui::SliderFloat("##rough", &mat->roughness, 0.0f, 1.0f);
          });
          mat_grid_row("Metallic", [&]() {
            ImGui::SliderFloat("##metal", &mat->metallic, 0.0f, 1.0f);
          });

          ImGui::Separator();
          struct TexSlot {
            const char *label;
            std::shared_ptr<Memoria::TextureAsset> Forma::Material::*field;
            uint32_t Forma::Material::*idxField;
          };
          TexSlot slots[] = {
            {"Albedo", &Forma::Material::albedo, &Forma::Material::albedoIdx},
            {"Normal", &Forma::Material::normal, &Forma::Material::normalIdx},
            {"Met-Rou", &Forma::Material::metallicRoughness, &Forma::Material::metallicRoughnessIdx},
            {"AO", &Forma::Material::ao, &Forma::Material::aoIdx},
            {"Emiss", &Forma::Material::emissive, &Forma::Material::emissiveIdx},
          };
          auto &loadedTex = assetManager.getLoadedTextures();
          for (auto &slot : slots) {
            auto &tex = mat.get()->*slot.field;
            auto &idx = mat.get()->*slot.idxField;
            char preview[64];
            if (tex)
              snprintf(preview, sizeof(preview), "Tex#%u (%dx%d)",
                       tex->textureId, tex->width, tex->height);
            else
              snprintf(preview, sizeof(preview), "Default");
            mat_grid_row(slot.label, [&]() {
              if (ImGui::BeginCombo(
                      ("##" + std::string(slot.label)).c_str(), preview)) {
                if (ImGui::Selectable("Default", tex == nullptr)) {
                  tex.reset();
                  idx = 0;
                }
                for (size_t ti = 0; ti < loadedTex.size(); ++ti) {
                  if (!loadedTex[ti]) continue;
                  bool sel = (tex == loadedTex[ti]);
                  char item[64];
                  snprintf(item, sizeof(item), "Tex#%u (%dx%d)",
                           loadedTex[ti]->textureId, loadedTex[ti]->width,
                           loadedTex[ti]->height);
                  if (ImGui::Selectable(item, sel)) {
                    tex = loadedTex[ti];
                    idx = loadedTex[ti]->textureId;
                  }
                  if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
              }
            });
          }
        }
      }

      // ── World transform ──────────────────────────────────────────
      if (gt) {
        ImGui::Separator();
        float worldLabelW = ImGui::CalcTextSize("World Position").x;
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "World Position");
        ImGui::SameLine(ImGui::GetCursorPosX() + worldLabelW +
                        ImGui::GetStyle().ItemSpacing.x * 2);
        ImGui::TextDisabled("(%.2f, %.2f, %.2f)", gt->value[3][0],
                            gt->value[3][1], gt->value[3][2]);
      }

      ImGui::PopID();
      ImGui::PopStyleVar();
      } // if (n)
    } else {
      ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                         "No entity selected.");
    }
  }
  ImGui::End();

  // ── ASSET MANAGER — FILE BROWSER ─────────────────────────────────────
  ImGui::SetNextWindowPos(
      ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - PAD,
             work_pos.y + PAD + 420),
      ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(m_assetWindowSize.x, m_assetWindowSize.y),
                           ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.75f);

  if (ImGui::Begin("Asset Manager", nullptr, ImGuiWindowFlags_None)) {
    if (!m_treeScanned) {
      m_dirTree.clear();
      scanDirTree(CONCORDIA_ASSETS_DIR, m_dirTree);
      m_treeScanned = true;
      m_selectedDir = CONCORDIA_ASSETS_DIR;
    }

    // ── Left: Directory tree ──
    float treeHeight = -ImGui::GetFrameHeightWithSpacing() - 90;
    if (m_previewPath.empty())
      treeHeight = -ImGui::GetFrameHeightWithSpacing() - 40;
    ImGui::BeginChild("DirTree", ImVec2(170, treeHeight), true);
    drawDirTree(m_dirTree);
    ImGui::EndChild();

    ImGui::SameLine();

    // ── Right: File grid ──
    ImGui::BeginChild("FileGrid", ImVec2(0, treeHeight), false);
    if (!m_selectedDir.empty() && std::filesystem::exists(m_selectedDir)) {
      struct FileItem {
        std::string name;
        std::string path;
        std::string ext;
        bool isDir;
      };
      std::vector<FileItem> items;
      for (auto &entry : std::filesystem::directory_iterator(m_selectedDir)) {
        FileItem fi;
        fi.name = entry.path().filename().string();
        fi.path = entry.path().string();
        fi.ext = entry.path().extension().string();
        fi.isDir = entry.is_directory();
        items.push_back(std::move(fi));
      }
      std::sort(items.begin(), items.end(),
                [](const FileItem &a, const FileItem &b) {
                  if (a.isDir != b.isDir)
                    return a.isDir > b.isDir;
                  return a.name < b.name;
                });

      for (size_t i = 0; i < items.size(); ++i) {
        auto &fi = items[i];
        ImGui::PushID(static_cast<int>(i));

        bool isImage = (fi.ext == ".png" || fi.ext == ".jpg" ||
                        fi.ext == ".jpeg" || fi.ext == ".hdr");
        bool isModel = (fi.ext == ".glb" || fi.ext == ".gltf");
        bool isFont = (fi.ext == ".ttf" || fi.ext == ".otf" ||
                       fi.ext == ".woff" || fi.ext == ".woff2");
        bool isAudio = (fi.ext == ".wav" || fi.ext == ".mp3" ||
                        fi.ext == ".ogg" || fi.ext == ".flac");
        bool isToon = (fi.ext == ".toon");
        bool isData = (fi.ext == ".json" || fi.ext == ".xml" ||
                       fi.ext == ".yaml" || fi.ext == ".toml");

        if (fi.isDir) {
          if (ImGui::Selectable(
                  (ICON_NF_FOLDER "  " + fi.name).c_str(), false,
                  ImGuiSelectableFlags_AllowDoubleClick)) {
            m_selectedDir = fi.path;
            m_selectedFileIndex = -1;
            m_previewPath.clear();
          }
        } else if (isImage) {
          if (ImGui::Selectable(
                  (ICON_NF_IMAGE "  " + fi.name).c_str(),
                  m_selectedFileIndex == static_cast<int>(i))) {
            m_selectedFileIndex = static_cast<int>(i);
            m_previewPath = fi.path;
            try {
              auto tex = assetManager.loadTexture(fi.path);
              if (tex) {
                auto it = m_textureCache.find(tex->textureId);
                if (it == m_textureCache.end()) {
                  auto h = ImGui_ImplVulkan_AddTexture(sampler, tex->view,
                                                       tex->layout);
                  m_textureCache[tex->textureId] = h;
                }
              }
            } catch (std::exception &e) {
              SDL_Log("Overlay: Failed to load image %s: %s", fi.path.c_str(),
                      e.what());
            }
          }
        } else if (isModel) {
          if (ImGui::Selectable(
                  (ICON_NF_BOX "  " + fi.name).c_str(),
                  m_selectedFileIndex == static_cast<int>(i))) {
            m_selectedFileIndex = static_cast<int>(i);
            m_previewPath = fi.path;
            try {
              assetManager.loadGLTF(fi.path, ecs);
              SDL_Log("Overlay: Loaded model %s", fi.path.c_str());
            } catch (std::exception &e) {
              SDL_Log("Overlay: Failed to load model %s: %s", fi.path.c_str(),
                      e.what());
            }
          }
        } else if (isFont) {
          if (ImGui::Selectable(
                  (ICON_NF_FONT "  " + fi.name).c_str(),
                  m_selectedFileIndex == static_cast<int>(i))) {
            m_selectedFileIndex = static_cast<int>(i);
            m_previewPath = fi.path;
          }
        } else if (isAudio) {
          if (ImGui::Selectable(
                  (ICON_NF_MUSIC "  " + fi.name).c_str(),
                  m_selectedFileIndex == static_cast<int>(i))) {
            m_selectedFileIndex = static_cast<int>(i);
            m_previewPath = fi.path;
          }
        } else if (isToon) {
          if (ImGui::Selectable(
                  (ICON_NF_CONFIG "  " + fi.name).c_str(),
                  m_selectedFileIndex == static_cast<int>(i))) {
            m_selectedFileIndex = static_cast<int>(i);
            m_previewPath = fi.path;
          }
        } else if (isData) {
          if (ImGui::Selectable(
                  (ICON_NF_CODE "  " + fi.name).c_str(),
                  m_selectedFileIndex == static_cast<int>(i))) {
            m_selectedFileIndex = static_cast<int>(i);
            m_previewPath = fi.path;
          }
        } else {
          if (ImGui::Selectable(
                  (ICON_NF_FILE "  " + fi.name).c_str(),
                  m_selectedFileIndex == static_cast<int>(i))) {
            m_selectedFileIndex = static_cast<int>(i);
            m_previewPath = fi.path;
          }
        }

        ImGui::PopID();
      }
    }
    ImGui::EndChild();

    // ── Preview area ──
    if (!m_previewPath.empty()) {
      ImGui::Separator();
      std::filesystem::path p(m_previewPath);
      std::string ext = p.extension().string();
      bool isImage = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                      ext == ".hdr");

      if (isImage) {
        // Try to find the loaded texture and show a preview
        VkDescriptorSet handle = VK_NULL_HANDLE;
        auto &allTex = assetManager.getLoadedTextures();
        for (auto &tex : allTex) {
          if (tex && m_textureCache.count(tex->textureId)) {
            handle = m_textureCache[tex->textureId];
            break;
          }
        }
        if (handle) {
          ImVec2 avail = ImGui::GetContentRegionAvail();
          float aspect = 1.0f;
          ImVec2 previewSize(avail.x * 0.5f, avail.x * 0.5f / aspect);
          if (previewSize.y > 150)
            previewSize.y = 150;
          ImGui::Image(handle, previewSize);
        }
      }
      ImGui::Text("  %s", p.filename().string().c_str());
    }
  }
  ImGui::End();
  SDL_Log("DBG OV [%d] drawUI end", dbgFrame);
}

void Overlay::endFrameAndRecord(VkCommandBuffer cmdBuf) {
  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
}

} // namespace Vigil
