#pragma once

#include <vk_mem_alloc.h>
#include "allocator.h"
#include "../forma/mesh.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

namespace Memoria {

struct MeshAsset {
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = VK_NULL_HANDLE;
    uint32_t vertexCount = 0;

    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VmaAllocation indexAllocation = VK_NULL_HANDLE;
    uint32_t indexCount = 0;

    void destroy(Allocator& allocator) {
        if (vertexBuffer) allocator.destroyBuffer(vertexBuffer, vertexAllocation);
        if (indexBuffer) allocator.destroyBuffer(indexBuffer, indexAllocation);
    }
};

struct TextureAsset {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;

    void destroy(Allocator& allocator, VkDevice device) {
        if (view) vkDestroyImageView(device, view, nullptr);
        if (image) allocator.destroyImage(image, allocation);
    }
};

class AssetManager {
public:
    AssetManager(Allocator& allocator, VkDevice device, VkQueue transferQueue, VkCommandPool transferPool);
    ~AssetManager();

    std::shared_ptr<MeshAsset> loadMesh(const std::string& path, bool createCubeFallback = false);
    std::shared_ptr<TextureAsset> loadTexture(const std::string& path);
    std::shared_ptr<TextureAsset> loadCubemap(const std::string& directoryPath); 
    std::shared_ptr<TextureAsset> loadCubemapFromCross(const std::string& path);

private:
    Allocator& m_allocator;
    VkDevice m_device;
    VkQueue m_transferQueue;
    VkCommandPool m_transferPool;

    std::unordered_map<std::string, std::shared_ptr<MeshAsset>> m_meshes;
    std::unordered_map<std::string, std::shared_ptr<TextureAsset>> m_textures;
};

} // namespace Memoria
