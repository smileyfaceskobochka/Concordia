#pragma once

#include <glm/glm.hpp>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace Render {

struct RenderObject {
  VkBuffer vertexBuffer;
  VmaAllocation vertexAllocation;
  uint32_t vertexCount;

  VkBuffer indexBuffer;
  VmaAllocation indexAllocation;
  uint32_t indexCount;

  glm::mat4 transform;
};

} // namespace Render
