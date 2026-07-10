#include "descriptor_manager.h"
#include "render/vk_check.h"
#include <cstring>

namespace Lumen {

DescriptorManager::DescriptorManager(VkDevice device, Memoria::Allocator &allocator)
    : m_device(device), m_allocator(&allocator) {}

DescriptorManager::~DescriptorManager() {
  if (m_device == VK_NULL_HANDLE) return;

  if (m_uboMapped) vmaUnmapMemory(m_allocator->getVma(), m_uboAlloc);
  if (m_ubo) m_allocator->destroyBuffer(m_ubo, m_uboAlloc);

  if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
  if (m_globalLayout) vkDestroyDescriptorSetLayout(m_device, m_globalLayout, nullptr);
  if (m_materialLayout) vkDestroyDescriptorSetLayout(m_device, m_materialLayout, nullptr);
  if (m_materialParamLayout) vkDestroyDescriptorSetLayout(m_device, m_materialParamLayout, nullptr);
}

void DescriptorManager::init(VkSampler defaultSampler, uint32_t bindlessCount,
                              const std::vector<VkImageView> &initialTextures) {
  m_defaultSampler = defaultSampler;
  // Global set layout: 5 bindings (UBO + skybox + irradiance + prefilter + BRDF)
  VkDescriptorSetLayoutBinding globalBindings[5] = {};
  globalBindings[0].binding = 0;
  globalBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  globalBindings[0].descriptorCount = 1;
  globalBindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  for (uint32_t i = 1; i < 5; ++i) {
    globalBindings[i].binding = i;
    globalBindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    globalBindings[i].descriptorCount = 1;
    globalBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    globalBindings[i].pImmutableSamplers = &defaultSampler;
  }

  VkDescriptorSetLayoutCreateInfo globalLayoutInfo = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  globalLayoutInfo.bindingCount = 5;
  globalLayoutInfo.pBindings = globalBindings;
  VK_CHECK(vkCreateDescriptorSetLayout(m_device, &globalLayoutInfo, nullptr,
                                        &m_globalLayout));

  // Bindless set layout (binding 0 = array of sampled images)
  VkDescriptorSetLayoutBinding bindlessBinding{};
  bindlessBinding.binding = 0;
  bindlessBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  bindlessBinding.descriptorCount = bindlessCount;
  bindlessBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  bindlessBinding.pImmutableSamplers = nullptr;

  VkDescriptorBindingFlags bindlessFlags =
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
      VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

  VkDescriptorSetLayoutBindingFlagsCreateInfo bindlessFlagInfo = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
  bindlessFlagInfo.bindingCount = 1;
  bindlessFlagInfo.pBindingFlags = &bindlessFlags;

  VkDescriptorSetLayoutCreateInfo matLayoutInfo = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  matLayoutInfo.pNext = &bindlessFlagInfo;
  matLayoutInfo.flags =
      VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
  matLayoutInfo.bindingCount = 1;
  matLayoutInfo.pBindings = &bindlessBinding;
  VK_CHECK(vkCreateDescriptorSetLayout(m_device, &matLayoutInfo, nullptr,
                                        &m_materialLayout));

  // Material parameters set layout: 1 binding (uniform buffer at binding 0)
  VkDescriptorSetLayoutBinding paramBinding{};
  paramBinding.binding = 0;
  paramBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  paramBinding.descriptorCount = 1;
  paramBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  paramBinding.pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo paramLayoutInfo = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  paramLayoutInfo.bindingCount = 1;
  paramLayoutInfo.pBindings = &paramBinding;
  VK_CHECK(vkCreateDescriptorSetLayout(m_device, &paramLayoutInfo, nullptr,
                                        &m_materialParamLayout));

  // Pool
  VkDescriptorPoolSize poolSizes[2] = {};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[0].descriptorCount = 1;
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[1].descriptorCount = bindlessCount + 4;

  VkDescriptorPoolCreateInfo poolInfo = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
  poolInfo.maxSets = 2;
  poolInfo.poolSizeCount = 2;
  poolInfo.pPoolSizes = poolSizes;
  VK_CHECK(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_pool));

  // Allocate global set
  VkDescriptorSetAllocateInfo allocInfo = {
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = m_pool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &m_globalLayout;
  VK_CHECK(vkAllocateDescriptorSets(m_device, &allocInfo, &m_globalSet));

  // Allocate bindless set
  allocInfo.pSetLayouts = &m_materialLayout;
  VK_CHECK(vkAllocateDescriptorSets(m_device, &allocInfo, &m_bindlessSet));

  // UBO
  VkDeviceSize uboSize = sizeof(GlobalUBO);
  m_allocator->createBuffer(uboSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VMA_MEMORY_USAGE_AUTO, m_ubo, m_uboAlloc,
                             VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
  vmaMapMemory(m_allocator->getVma(), m_uboAlloc, &m_uboMapped);

  // Write UBO binding
  VkDescriptorBufferInfo uboInfo{};
  uboInfo.buffer = m_ubo;
  uboInfo.range = uboSize;

  VkWriteDescriptorSet uboWrite = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  uboWrite.dstSet = m_globalSet;
  uboWrite.dstBinding = 0;
  uboWrite.descriptorCount = 1;
  uboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboWrite.pBufferInfo = &uboInfo;

  vkUpdateDescriptorSets(m_device, 1, &uboWrite, 0, nullptr);

  // Write bindless set
  updateBindless(initialTextures);
}

void DescriptorManager::updateGlobalUBO(const GlobalUBO &ubo) {
  std::memcpy(m_uboMapped, &ubo, sizeof(GlobalUBO));
}

void DescriptorManager::updateSkybox(VkImageView skyboxView) {
  VkDescriptorImageInfo imgInfo{};
  imgInfo.imageView = skyboxView;
  imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  // Sampler is immutable in the layout — imgInfo.sampler is ignored

  VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = m_globalSet;
  write.dstBinding = 1;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = &imgInfo;
  vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

void DescriptorManager::updateIBL(VkImageView irradiance, VkImageView prefilter,
                                   VkImageView brdfLUT) {
  VkDescriptorImageInfo imgInfos[3] = {};
  VkWriteDescriptorSet writes[3] = {};

  VkImageView views[3] = {irradiance, prefilter, brdfLUT};
  for (uint32_t i = 0; i < 3; ++i) {
    imgInfos[i].imageView = views[i];
    imgInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    writes[i].dstSet = m_globalSet;
    writes[i].dstBinding = 2 + i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[i].pImageInfo = &imgInfos[i];
  }

  vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
}

void DescriptorManager::updateBindless(const std::vector<VkImageView> &textureViews) {
  uint32_t count = static_cast<uint32_t>(textureViews.size());
  if (count == 0) return;

  std::vector<VkDescriptorImageInfo> imgInfos(count);
  for (uint32_t i = 0; i < count; ++i) {
    imgInfos[i].sampler = m_defaultSampler;
    imgInfos[i].imageView = textureViews[i];
    imgInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }

  VkWriteDescriptorSet write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = m_bindlessSet;
  write.dstBinding = 0;
  write.dstArrayElement = 0;
  write.descriptorCount = count;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = imgInfos.data();

  vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
}

} // namespace Lumen
