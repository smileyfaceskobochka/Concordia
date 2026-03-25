#pragma once
#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <glm/glm.hpp>
#include "../memoria/asset_manager.h"

namespace Forma {

struct Material {
    std::string shaderName = "blinn_phong";
    std::shared_ptr<Memoria::TextureAsset> texture;
    
    glm::vec4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

} // namespace Forma
