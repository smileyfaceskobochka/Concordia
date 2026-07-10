#pragma once

#include "forma/material.h"
#include "memoria/types.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace Mundus {

struct UniformMember {
  std::string name;
  std::string type;
  uint32_t offset;
  uint32_t size;
};

struct ShaderAsset {
  std::string name;
  std::string vertPath;
  std::string fragPath;
  std::string lightPath;

  bool depthTest = true;
  bool depthWrite = true;
  VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
  VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
  VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  bool blendEnable = false;

  uint32_t paramSize = 0;
  std::vector<UniformMember> paramMembers;
  std::unordered_map<std::string, std::string> paramDefaults;
};

// ── Transform (moved from entity.h, also used as Flecs component) ─────────

struct Transform {
  glm::vec3 position{0.0f};
  glm::vec3 rotation{0.0f};
  glm::vec3 scale{1.0f};
  glm::vec3 angularVelocity{0.0f};

  glm::mat4 getLocalMatrix() const;
};

// ── Identity ──────────────────────────────────────────────────────────────

struct Name {
  std::string value;
};

struct Id {
  std::string value;   // stable identifier (defaults to name if empty)
};

// ── Computed at runtime by systems ────────────────────────────────────────

struct GlobalTransform {
  glm::mat4 value{1.0f};
};

struct EffectiveVisibility {
  bool visible = true;
};

// ── Source tracking (serialisation) ───────────────────────────────────────

struct MeshSource {
  std::string value;   // "@primitive(cube)" or "@asset(assets://...)"
};

// ── GPU resource handles ──────────────────────────────────────────────────

struct MeshAssetRef {
  std::shared_ptr<Memoria::MeshAsset> value;
};

struct MaterialRef {
  std::shared_ptr<Forma::Material> value;
};

// ── Per-frame flags ───────────────────────────────────────────────────────

struct Visibility {
  bool visible = true;
};

// ── Scene-global singletons ───────────────────────────────────────────────

struct LightDir {
  glm::vec3 value{-0.5f, -1.0f, -0.2f};
};

struct LightColor {
  glm::vec3 value{1.0f, 1.0f, 1.0f};
};

// ── Model Assets and Internal Node Overrides ────────────────────────────────

struct ModelSource {
  std::string value; // "@asset(assets://...)"
};

struct GltfInternalNode {
  bool placeholder = true; // dummy field for C compatibility
};

struct GltfDefaultTransform {
  glm::vec3 position{0.0f};
  glm::vec3 rotation{0.0f};
  glm::vec3 scale{1.0f};
};

struct GltfDefaultMaterial {
  std::shared_ptr<Forma::Material> value;
};

} // namespace Mundus
