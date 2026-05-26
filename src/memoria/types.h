#pragma once
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <memory>

namespace Memoria {

class Allocator;

struct MeshAsset {
  VkBuffer vertexBuffer = VK_NULL_HANDLE;
  VmaAllocation vertexAllocation = VK_NULL_HANDLE;
  uint32_t vertexCount = 0;

  VkBuffer indexBuffer = VK_NULL_HANDLE;
  VmaAllocation indexAllocation = VK_NULL_HANDLE;
  uint32_t indexCount = 0;

  glm::vec3 aabbMin{0.0f};
  glm::vec3 aabbMax{0.0f};

  void destroy(Allocator &allocator);
};

struct TextureAsset {
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  uint32_t textureId = 0;
  int width = 0;
  int height = 0;

  void destroy(Allocator &allocator, VkDevice device);
};

} // namespace Memoria
