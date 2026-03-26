#pragma once
#include "../memoria/asset_manager.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vulkan/vulkan.h>

namespace Forma {

struct Material {
  std::string shaderName = "blinn_phong";
  std::shared_ptr<Memoria::TextureAsset> albedo;
  std::shared_ptr<Memoria::TextureAsset> normal;
  std::shared_ptr<Memoria::TextureAsset> metallicRoughness;
  std::shared_ptr<Memoria::TextureAsset> ao;
  std::shared_ptr<Memoria::TextureAsset> emissive;

  glm::vec4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
  float roughness = 0.5f;
  float metallic = 0.0f;

  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
};

} // namespace Forma
