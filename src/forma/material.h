#pragma once
#include "memoria/types.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vulkan/vulkan.h>

namespace Forma {

struct Material {
  std::string shaderName = "pbr";
  std::shared_ptr<Memoria::TextureAsset> albedo;
  std::shared_ptr<Memoria::TextureAsset> normal;
  std::shared_ptr<Memoria::TextureAsset> metallicRoughness;
  std::shared_ptr<Memoria::TextureAsset> ao;
  std::shared_ptr<Memoria::TextureAsset> emissive;

  // Texture source paths (for serialization / resolve)
  std::string albedoSource;
  std::string normalSource;
  std::string metallicRoughnessSource;
  std::string aoSource;
  std::string emissiveSource;

  // Bindless indices
  uint32_t albedoIdx = 0;
  uint32_t normalIdx = 0;
  uint32_t metallicRoughnessIdx = 0;
  uint32_t aoIdx = 0;
  uint32_t emissiveIdx = 0;

  glm::vec4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
  float roughness = 0.5f;
  float metallic = 0.0f;

  // Dynamic Shader & Param UBO Support
  std::string shaderManifestPath;
  std::vector<uint8_t> paramData;
  VkBuffer paramBuffer = VK_NULL_HANDLE;
  void* paramAllocation = nullptr; // VmaAllocation
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
};

} // namespace Forma
