#pragma once

#include "../forma/mesh.h"
#include "allocator.h"
#include <string>
#include <vk_mem_alloc.h>

namespace Mundus {
class Scene;
}
#include <memory>
#include <unordered_map>
#include <vector>

namespace Memoria {

struct MeshAsset {
  VkBuffer vertexBuffer = VK_NULL_HANDLE;
  VmaAllocation vertexAllocation = VK_NULL_HANDLE;
  uint32_t vertexCount = 0;

  VkBuffer indexBuffer = VK_NULL_HANDLE;
  VmaAllocation indexAllocation = VK_NULL_HANDLE;
  uint32_t indexCount = 0;

  void destroy(Allocator &allocator) {
    if (vertexBuffer)
      allocator.destroyBuffer(vertexBuffer, vertexAllocation);
    if (indexBuffer)
      allocator.destroyBuffer(indexBuffer, indexAllocation);
  }
};

struct TextureAsset {
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;

  void destroy(Allocator &allocator, VkDevice device) {
    if (view)
      vkDestroyImageView(device, view, nullptr);
    if (image)
      allocator.destroyImage(image, allocation);
  }
};

class AssetManager {
public:
  AssetManager(Allocator &allocator, VkDevice device, VkQueue transferQueue,
               VkCommandPool transferPool);
  ~AssetManager();

  std::shared_ptr<MeshAsset> loadMesh(const std::string &path,
                                      bool createCubeFallback = false);
  std::shared_ptr<TextureAsset> loadTexture(const std::string &path,
                                            bool srgb = true);
  std::shared_ptr<TextureAsset> loadTextureFromMemory(const unsigned char *data,
                                                      size_t size,
                                                      const std::string &name,
                                                      bool srgb = true);
  std::shared_ptr<TextureAsset> loadCubemap(const std::string &directoryPath);
  std::shared_ptr<TextureAsset> loadCubemapFromCross(const std::string &path);

  // GLTF Support
  void loadGLTF(const std::string &path, Mundus::Scene &scene,
                int parentIndex = -1);

  // Default textures for PBR fallbacks
  std::shared_ptr<TextureAsset> getDefaultWhite() { return m_defaultWhite; }
  std::shared_ptr<TextureAsset> getDefaultBlack() { return m_defaultBlack; }
  std::shared_ptr<TextureAsset> getDefaultNormal() { return m_defaultNormal; }
  std::shared_ptr<TextureAsset> getDefaultBRDF() { return m_defaultBRDF; }

private:
  std::shared_ptr<TextureAsset>
  createSolidTexture(unsigned char r, unsigned char g, unsigned char b,
                     unsigned char a, bool srgb = true);
  std::shared_ptr<TextureAsset>
  loadTextureFromSTB(unsigned char *pixels, int width, int height, bool srgb);

  Allocator &m_allocator;
  VkDevice m_device;
  VkQueue m_transferQueue;
  VkCommandPool m_transferPool;

  std::shared_ptr<TextureAsset> m_defaultWhite;
  std::shared_ptr<TextureAsset> m_defaultBlack;
  std::shared_ptr<TextureAsset> m_defaultNormal;
  std::shared_ptr<TextureAsset> m_defaultBRDF;

  std::unordered_map<std::string, std::shared_ptr<MeshAsset>> m_meshes;
  std::unordered_map<std::string, std::shared_ptr<TextureAsset>> m_textures;
};

} // namespace Memoria
