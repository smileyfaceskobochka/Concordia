#include "mesh.h"
#include <stdexcept>
#include <vector>
#include "fast_obj.h"

namespace Forma {

VkVertexInputBindingDescription Vertex::getBindingDescription() {
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

std::vector<VkVertexInputAttributeDescription> Vertex::getAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions(3);

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
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

    return attributeDescriptions;
}

void Mesh::createCube(std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices) {
    outVertices = {
        // Front face (Z=0.5)
        {{-0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f,  0.5f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f}},
        // Back face (Z=-0.5)
        {{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}, {0.0f, 0.0f}},
        {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
        {{-0.5f,  0.5f, -0.5f}, {0.1f, 0.1f, 0.1f}, {1.0f, 1.0f}}
    };

    outIndices = {
        0, 1, 2, 2, 3, 0, // Front
        1, 5, 6, 6, 2, 1, // Right
        7, 6, 5, 5, 4, 7, // Back
        4, 0, 3, 3, 7, 4, // Left
        4, 5, 1, 1, 0, 4, // Bottom
        3, 2, 6, 6, 7, 3  // Top
    };
}

void Mesh::loadFromOBJ(const std::string& path, std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices) {
    fastObjMesh* mesh = fast_obj_read(path.c_str());
    if (!mesh) {
        throw std::runtime_error("Failed to load OBJ: " + path);
    }

    // Reset just in case
    outVertices.clear();
    outIndices.clear();

    uint32_t indexOffset = 0;
    for (unsigned int i = 0; i < mesh->face_count; ++i) {
        unsigned int verticesInFace = mesh->face_vertices[i];
        
        // Triangulate face
        for (unsigned int j = 0; j < verticesInFace; ++j) {
            // Standard triangulation: (0, 1, 2, 3, 4) -> (0, 1, 2), (0, 2, 3), (0, 3, 4)
            if (j >= 3) {
                // We need to re-add the first vertex and the previous vertex of the triangulation
                // Actually, it's easier to just push indices if we reuse vertices.
                // But for simplicity in this DOD model, we'll duplicate vertices for now.
            }

            // Correct triangulation for N vertices (fans):
            if (j >= 3) {
                // Triangle 1: 0, j-1, j
                // But we are in a simple loop.
            }
        }

        // Simpler approach: build a list of all vertices for the face
        std::vector<Vertex> faceVerts(verticesInFace);
        for(unsigned int j = 0; j < verticesInFace; ++j) {
            fastObjIndex idx = mesh->indices[indexOffset + j];
            faceVerts[j].pos = {
                mesh->positions[3 * idx.p + 0],
                mesh->positions[3 * idx.p + 1],
                mesh->positions[3 * idx.p + 2]
            };
            
            // Random-ish color based on face index to see geometry clearly
            float intensity = 0.5f + 0.5f * (float)(i % 10) / 10.0f;
            faceVerts[j].color = {intensity, intensity * 0.8f, intensity * 0.6f};
            
            if (mesh->color_count > 0) {
                faceVerts[j].color = {
                    mesh->colors[3 * idx.p + 0],
                    mesh->colors[3 * idx.p + 1],
                    mesh->colors[3 * idx.p + 2]
                };
            }

            if (mesh->texcoord_count > 0) {
                faceVerts[j].texCoord = {
                    mesh->texcoords[2 * idx.t + 0],
                    1.0f - mesh->texcoords[2 * idx.t + 1] // Invert V for Vulkan
                };
            } else {
                faceVerts[j].texCoord = {0.0f, 0.0f};
            }
        }

        // Fan triangulation
        for (unsigned int j = 1; j < verticesInFace - 1; ++j) {
            uint32_t baseIdx = static_cast<uint32_t>(outVertices.size());
            
            outVertices.push_back(faceVerts[0]);
            outVertices.push_back(faceVerts[j]);
            outVertices.push_back(faceVerts[j + 1]);
            
            outIndices.push_back(baseIdx);
            outIndices.push_back(baseIdx + 1);
            outIndices.push_back(baseIdx + 2);
        }
        
        indexOffset += verticesInFace;
    }

    fast_obj_destroy(mesh);
}

} // namespace Forma
