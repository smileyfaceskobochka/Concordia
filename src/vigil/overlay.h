#pragma once

#include "mundus/scene.h"
#include "mundus/components.h"
#include "petra/window.h"
#include "render/context.h"
#include <flecs.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Memoria {
class AssetManager;
class Sampler;
struct TextureAsset;
}

namespace Vigil {

struct DebugStats {
  float fps = 0.0f;
  float frameTime = 0.0f;
  uint32_t drawCalls = 0;
  uint32_t vertexCount = 0;
  glm::vec3 cameraPos{0.0f};
  glm::vec3 cameraFront{0.0f};

  float *cameraSpeed = nullptr;
  float *cameraSens = nullptr;
  bool *captureMouse = nullptr;

  glm::vec3 focusTarget{0.0f};
  bool hasFocusTarget = false;
};

class Overlay {
public:
  Overlay(const Petra::Window &window, const Render::Context &renderCtx);
  ~Overlay();

  Overlay(const Overlay &) = delete;
  Overlay &operator=(const Overlay &) = delete;

  void beginFrame();

  void drawUI(const Render::Context &renderCtx, DebugStats &stats,
              flecs::world &ecs,
              Memoria::AssetManager &assetManager,
              VkSampler sampler, uint32_t *debugMode,
              uint32_t *selectedSkybox, uint32_t skyboxCount,
              const char *const *skyboxNames,
              uint32_t shaderCount, const char *const *shaderNames);

  void endFrameAndRecord(VkCommandBuffer cmdBuf);

  void setSelectedEntity(flecs::entity e) { m_selectedEntity = e; }
  flecs::entity getSelectedEntity() const { return m_selectedEntity; }

private:
  struct FileEntry {
    std::string name;
    std::string path;
    bool isDirectory = false;
    std::vector<FileEntry> children;
  };

  void loadUIConfig(const std::string &path);
  void scanDirTree(const std::string &rootPath,
                   std::vector<FileEntry> &entries);
  void drawDirTree(const std::vector<FileEntry> &entries);

  VkDevice m_device = VK_NULL_HANDLE;
  VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
  flecs::entity m_selectedEntity;

  std::map<uint32_t, VkDescriptorSet> m_textureCache;

  // Config-loaded values
  float m_statsPadding = 10.0f;
  float m_inspectorWidth = 350.0f;
  glm::vec2 m_assetWindowSize = {420.0f, 320.0f};
  int m_renameBufSize = 256;
  float m_sliderSpeedMin = 0.1f;
  float m_sliderSpeedMax = 50.0f;
  float m_sliderSensMin = 0.01f;
  float m_sliderSensMax = 1.0f;
  std::vector<std::string> m_debugModes;

  // File browser
  std::vector<FileEntry> m_dirTree;
  bool m_treeScanned = false;
  std::string m_selectedDir;
  int m_selectedFileIndex = -1;
  std::string m_previewPath;
};

} // namespace Vigil
