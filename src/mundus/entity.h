#pragma once

#include "forma/material.h"
#include "memoria/asset_manager.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace Mundus {

struct Transform {
  glm::vec3 position{0.0f};
  glm::vec3 rotation{0.0f};
  glm::vec3 scale{1.0f};
  glm::vec3 angularVelocity{0.0f};

  glm::mat4 getLocalMatrix() const;
};

struct Entity {
  std::string name;
  Transform transform;

  // Hierarchy
  int parentIndex = -1;
  std::vector<int> children;
  glm::mat4 globalTransform{1.0f};

  // Components
  std::shared_ptr<Memoria::MeshAsset> mesh;
  std::shared_ptr<Forma::Material> material;
};

} // namespace Mundus
