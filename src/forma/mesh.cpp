#include "mesh.h"
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace Forma {

namespace {

static glm::vec3 safeNormalize(const glm::vec3 &v) {
  float len2 = glm::dot(v, v);
  if (len2 <= 1e-20f) {
    return glm::vec3(0.0f, 0.0f, 0.0f);
  }
  return v / std::sqrt(len2);
}

struct VertexHash {
  size_t operator()(const Vertex &v) const {
    auto hf = std::hash<float>{};

    size_t h = 0;
    auto hashCombine = [&](size_t x) {
      h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };

    hashCombine(hf(v.pos.x));
    hashCombine(hf(v.pos.y));
    hashCombine(hf(v.pos.z));

    hashCombine(hf(v.color.x));
    hashCombine(hf(v.color.y));
    hashCombine(hf(v.color.z));

    hashCombine(hf(v.normal.x));
    hashCombine(hf(v.normal.y));
    hashCombine(hf(v.normal.z));

    hashCombine(hf(v.texCoord.x));
    hashCombine(hf(v.texCoord.y));

    hashCombine(hf(v.tangent.x));
    hashCombine(hf(v.tangent.y));
    hashCombine(hf(v.tangent.z));
    hashCombine(hf(v.tangent.w));

    return h;
  }
};

} // namespace

VkVertexInputBindingDescription Vertex::getBindingDescription() {
  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = sizeof(Vertex);
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  return bindingDescription;
}

std::vector<VkVertexInputAttributeDescription>
Vertex::getAttributeDescriptions() {
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions(5);

  attributeDescriptions[0].binding = 0;
  attributeDescriptions[0].location = 0;
  attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[0].offset = offsetof(Vertex, pos);

  attributeDescriptions[1].binding = 0;
  attributeDescriptions[1].location = 1;
  attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[1].offset = offsetof(Vertex, color);

  attributeDescriptions[2].binding = 0;
  attributeDescriptions[2].location = 2;
  attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributeDescriptions[2].offset = offsetof(Vertex, normal);

  attributeDescriptions[3].binding = 0;
  attributeDescriptions[3].location = 3;
  attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
  attributeDescriptions[3].offset = offsetof(Vertex, texCoord);

  attributeDescriptions[4].binding = 0;
  attributeDescriptions[4].location = 4;
  attributeDescriptions[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
  attributeDescriptions[4].offset = offsetof(Vertex, tangent);

  return attributeDescriptions;
}

void Mesh::createCube(std::vector<Vertex> &outVertices,
                      std::vector<uint32_t> &outIndices) {
  outVertices = {
      // Front (+Z)
      {{-0.5f, -0.5f, 0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 0.0f, 1.0f},
       {0.0f, 0.0f},
       {1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.5f, -0.5f, 0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 0.0f, 1.0f},
       {1.0f, 0.0f},
       {1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.5f, 0.5f, 0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 0.0f, 1.0f},
       {1.0f, 1.0f},
       {1.0f, 0.0f, 0.0f, 1.0f}},
      {{-0.5f, 0.5f, 0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 0.0f, 1.0f},
       {0.0f, 1.0f},
       {1.0f, 0.0f, 0.0f, 1.0f}},

      // Back (-Z)
      {{-0.5f, -0.5f, -0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 0.0f, -1.0f},
       {1.0f, 0.0f},
       {-1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.5f, -0.5f, -0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 0.0f, -1.0f},
       {0.0f, 0.0f},
       {-1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.5f, 0.5f, -0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 0.0f, -1.0f},
       {0.0f, 1.0f},
       {-1.0f, 0.0f, 0.0f, 1.0f}},
      {{-0.5f, 0.5f, -0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 0.0f, -1.0f},
       {1.0f, 1.0f},
       {-1.0f, 0.0f, 0.0f, 1.0f}},

      // Bottom (-Y)
      {{-0.5f, -0.5f, -0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, -1.0f, 0.0f},
       {0.0f, 0.0f},
       {1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.5f, -0.5f, -0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, -1.0f, 0.0f},
       {1.0f, 0.0f},
       {1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.5f, -0.5f, 0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, -1.0f, 0.0f},
       {1.0f, 1.0f},
       {1.0f, 0.0f, 0.0f, 1.0f}},
      {{-0.5f, -0.5f, 0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, -1.0f, 0.0f},
       {0.0f, 1.0f},
       {1.0f, 0.0f, 0.0f, 1.0f}},

      // Top (+Y)
      {{-0.5f, 0.5f, -0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 1.0f, 0.0f},
       {0.0f, 1.0f},
       {1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.5f, 0.5f, -0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 1.0f, 0.0f},
       {1.0f, 1.0f},
       {1.0f, 0.0f, 0.0f, 1.0f}},
      {{0.5f, 0.5f, 0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 1.0f, 0.0f},
       {1.0f, 0.0f},
       {1.0f, 0.0f, 0.0f, 1.0f}},
      {{-0.5f, 0.5f, 0.5f},
       {1.0f, 1.0f, 1.0f},
       {0.0f, 1.0f, 0.0f},
       {0.0f, 0.0f},
       {1.0f, 0.0f, 0.0f, 1.0f}},

      // Left (-X)
      {{-0.5f, -0.5f, -0.5f},
       {1.0f, 1.0f, 1.0f},
       {-1.0f, 0.0f, 0.0f},
       {0.0f, 0.0f},
       {0.0f, 0.0f, 1.0f, 1.0f}},
      {{-0.5f, -0.5f, 0.5f},
       {1.0f, 1.0f, 1.0f},
       {-1.0f, 0.0f, 0.0f},
       {1.0f, 0.0f},
       {0.0f, 0.0f, 1.0f, 1.0f}},
      {{-0.5f, 0.5f, 0.5f},
       {1.0f, 1.0f, 1.0f},
       {-1.0f, 0.0f, 0.0f},
       {1.0f, 1.0f},
       {0.0f, 0.0f, 1.0f, 1.0f}},
      {{-0.5f, 0.5f, -0.5f},
       {1.0f, 1.0f, 1.0f},
       {-1.0f, 0.0f, 0.0f},
       {0.0f, 1.0f},
       {0.0f, 0.0f, 1.0f, 1.0f}},

      // Right (+X)
      {{0.5f, -0.5f, -0.5f},
       {1.0f, 1.0f, 1.0f},
       {1.0f, 0.0f, 0.0f},
       {1.0f, 0.0f},
       {0.0f, 0.0f, -1.0f, 1.0f}},
      {{0.5f, -0.5f, 0.5f},
       {1.0f, 1.0f, 1.0f},
       {1.0f, 0.0f, 0.0f},
       {0.0f, 0.0f},
       {0.0f, 0.0f, -1.0f, 1.0f}},
      {{0.5f, 0.5f, 0.5f},
       {1.0f, 1.0f, 1.0f},
       {1.0f, 0.0f, 0.0f},
       {0.0f, 1.0f},
       {0.0f, 0.0f, -1.0f, 1.0f}},
      {{0.5f, 0.5f, -0.5f},
       {1.0f, 1.0f, 1.0f},
       {1.0f, 0.0f, 0.0f},
       {1.0f, 1.0f},
       {0.0f, 0.0f, -1.0f, 1.0f}},
  };

    outIndices = {
       0,  1,  2,  2,  3,  0,  // Front
       4,  7,  6,  6,  5,  4,  // Back
       8,  9,  10, 10, 11, 8,  // Bottom
       12, 15, 14, 14, 13, 12, // Top
       16, 17, 18, 18, 19, 16, // Left
       20, 23, 22, 22, 21, 20  // Right
   };
}
 
} // namespace Forma