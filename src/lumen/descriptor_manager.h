#pragma once
#include "memoria/allocator.h"
#include <glm/glm.hpp>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

struct GlobalUBO {
  glm::vec4 lightDir;
  glm::vec4 viewPos;
  glm::vec4 lightColor;
  glm::mat4 view;
  glm::mat4 proj;
  float exposure = 1.0f;
  float gamma = 2.2f;
  float padding[2];
};

namespace Lumen {

class DescriptorManager {
public:
  DescriptorManager(VkDevice device, Memoria::Allocator &allocator);
  ~DescriptorManager();

  void init(VkSampler defaultSampler, uint32_t bindlessCount,
            const std::vector<VkImageView> &initialTextures);

  void updateGlobalUBO(const GlobalUBO &ubo);
  void updateSkybox(VkImageView skyboxView);
  void updateIBL(VkImageView irradiance, VkImageView prefilter, VkImageView brdfLUT);
  void updateBindless(const std::vector<VkImageView> &textureViews);

  const VkDescriptorSetLayout &getGlobalLayout() const { return m_globalLayout; }
  const VkDescriptorSetLayout &getMaterialLayout() const { return m_materialLayout; }
  const VkDescriptorSet &getGlobalSet() const { return m_globalSet; }
  const VkDescriptorSet &getBindlessSet() const { return m_bindlessSet; }
  void *getUBOMapped() const { return m_uboMapped; }

private:
  VkDevice m_device = VK_NULL_HANDLE;
  Memoria::Allocator *m_allocator = nullptr;
  VkSampler m_defaultSampler = VK_NULL_HANDLE;

  VkDescriptorSetLayout m_globalLayout = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_materialLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_pool = VK_NULL_HANDLE;
  VkDescriptorSet m_globalSet = VK_NULL_HANDLE;
  VkDescriptorSet m_bindlessSet = VK_NULL_HANDLE;

  VkBuffer m_ubo = VK_NULL_HANDLE;
  VmaAllocation m_uboAlloc = VK_NULL_HANDLE;
  void *m_uboMapped = nullptr;
};

} // namespace Lumen
