#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace Forma {

struct Vertex {
  glm::vec3 pos{0.0f};
  glm::vec3 color{1.0f};
  glm::vec3 normal{0.0f, 0.0f, 1.0f};
  glm::vec2 texCoord{0.0f};
  glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};

  static VkVertexInputBindingDescription getBindingDescription();
  static std::vector<VkVertexInputAttributeDescription>
  getAttributeDescriptions();

  bool operator==(const Vertex &other) const {
    return pos == other.pos && color == other.color && normal == other.normal &&
           texCoord == other.texCoord && tangent == other.tangent;
  }
};

struct Mesh {
  VkBuffer vertexBuffer = VK_NULL_HANDLE;
  VmaAllocation vertexAllocation = VK_NULL_HANDLE;
  uint32_t vertexCount = 0;

  VkBuffer indexBuffer = VK_NULL_HANDLE;
  VmaAllocation indexAllocation = VK_NULL_HANDLE;
  uint32_t indexCount = 0;

  static void createCube(std::vector<Vertex> &outVertices,
                          std::vector<uint32_t> &outIndices);
};

} // namespace Forma
