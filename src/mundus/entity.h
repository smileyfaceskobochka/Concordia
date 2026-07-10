#pragma once

#include "forma/material.h"
#include "memoria/asset_manager.h"
#include "mundus/components.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace Mundus {

struct Entity {
  std::string name;
  std::string id;           // stable identifier (defaults to name if empty)
  Transform transform;

  // Hierarchy
  int parentIndex = -1;
  std::vector<int> children;
  glm::mat4 globalTransform{1.0f};

  // Components
  std::shared_ptr<Memoria::MeshAsset> mesh;
  std::shared_ptr<Forma::Material> material;

  // Serialization source tracking
  std::string meshSource;      // file path or "@primitive(cube)"

  bool visible = true;
  bool effectiveVisible = true;
};

} // namespace Mundus
