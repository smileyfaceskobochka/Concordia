#pragma once

#include <memory>
#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include "../memoria/asset_manager.h"
#include "../forma/material.h"

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
