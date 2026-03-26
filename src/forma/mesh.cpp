#include "mesh.h"
#include "fast_obj.h"

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

void Mesh::loadFromOBJ(const std::string &path,
                       std::vector<Vertex> &outVertices,
                       std::vector<uint32_t> &outIndices) {
  fastObjMesh *mesh = fast_obj_read(path.c_str());
  if (!mesh) {
    throw std::runtime_error("Failed to load OBJ: " + path);
  }

  outVertices.clear();
  outIndices.clear();

  std::unordered_map<Vertex, uint32_t, VertexHash> uniqueVertices;

  uint32_t indexOffset = 0;

  for (unsigned int i = 0; i < mesh->face_count; ++i) {
    unsigned int verticesInFace = mesh->face_vertices[i];
    if (verticesInFace < 3) {
      indexOffset += verticesInFace;
      continue;
    }

    std::vector<Vertex> faceVerts(verticesInFace);

    for (unsigned int j = 0; j < verticesInFace; ++j) {
      fastObjIndex idx = mesh->indices[indexOffset + j];

      Vertex v{};
      v.pos = {mesh->positions[3 * idx.p + 0], mesh->positions[3 * idx.p + 1],
               mesh->positions[3 * idx.p + 2]};

      v.color = {1.0f, 1.0f, 1.0f};

      if (idx.n) {
        v.normal = {mesh->normals[3 * idx.n + 0], mesh->normals[3 * idx.n + 1],
                    mesh->normals[3 * idx.n + 2]};
      } else {
        v.normal = {0.0f, 0.0f, 1.0f};
      }

      if (mesh->texcoord_count > 0 && idx.t) {
        v.texCoord = {mesh->texcoords[2 * idx.t + 0],
                      1.0f - mesh->texcoords[2 * idx.t + 1]};
      } else {
        v.texCoord = {0.0f, 0.0f};
      }

      v.tangent = {0.0f, 0.0f, 0.0f, 1.0f};
      faceVerts[j] = v;
    }

    // Fan triangulation
    for (unsigned int j = 1; j < verticesInFace - 1; ++j) {
      Vertex &v0 = faceVerts[0];
      Vertex &v1 = faceVerts[j];
      Vertex &v2 = faceVerts[j + 1];

      glm::vec3 edge1 = v1.pos - v0.pos;
      glm::vec3 edge2 = v2.pos - v0.pos;

      glm::vec2 deltaUV1 = v1.texCoord - v0.texCoord;
      glm::vec2 deltaUV2 = v2.texCoord - v0.texCoord;

      float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
      float f = (std::abs(denom) > 1e-8f) ? (1.0f / denom) : 0.0f;

      glm::vec3 tangent(f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x),
                        f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y),
                        f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z));

      tangent = safeNormalize(tangent);
      if (glm::dot(tangent, tangent) <= 1e-20f) {
        tangent = {1.0f, 0.0f, 0.0f};
      }

      v0.tangent = {tangent.x, tangent.y, tangent.z, 1.0f};
      v1.tangent = {tangent.x, tangent.y, tangent.z, 1.0f};
      v2.tangent = {tangent.x, tangent.y, tangent.z, 1.0f};

      Vertex *tri[3] = {&v0, &v1, &v2};
      for (int k = 0; k < 3; ++k) {
        auto it = uniqueVertices.find(*tri[k]);
        if (it == uniqueVertices.end()) {
          uint32_t newIndex = static_cast<uint32_t>(outVertices.size());
          uniqueVertices.emplace(*tri[k], newIndex);
          outVertices.push_back(*tri[k]);
          outIndices.push_back(newIndex);
        } else {
          outIndices.push_back(it->second);
        }
      }
    }

    indexOffset += verticesInFace;
  }

  fast_obj_destroy(mesh);
}

} // namespace Forma