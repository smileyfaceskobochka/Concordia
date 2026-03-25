#include "asset_manager.h"
#include <stdexcept>
#include <SDL3/SDL.h>
#include <stb_image.h>
#include <iostream>

namespace Memoria {

AssetManager::AssetManager(Allocator& allocator, VkDevice device, VkQueue transferQueue, VkCommandPool transferPool)
    : m_allocator(allocator), m_device(device), m_transferQueue(transferQueue), m_transferPool(transferPool) {
}

AssetManager::~AssetManager() {
    for (auto& pair : m_meshes) {
        pair.second->destroy(m_allocator);
    }
    for (auto& pair : m_textures) {
        pair.second->destroy(m_allocator, m_device);
    }
}

std::shared_ptr<MeshAsset> AssetManager::loadMesh(const std::string& path, bool createCubeFallback) {
    if (m_meshes.find(path) != m_meshes.end()) {
        return m_meshes[path];
    }

    std::vector<Forma::Vertex> vertices;
    std::vector<uint32_t> indices;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "AssetManager: Requesting load of mesh from %s", path.c_str());

    bool success = false;
    try {
        Forma::Mesh::loadFromOBJ(path, vertices, indices);
        success = true;
    } catch (...) {
        if (!createCubeFallback) {
            throw std::runtime_error("AssetManager failed to load mesh: " + path);
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "AssetManager: OBJ load failed or file not found, creating procedural cube for %s", path.c_str());
        Forma::Mesh::createCube(vertices, indices);
    }

    auto mesh = std::make_shared<MeshAsset>();
    mesh->vertexCount = static_cast<uint32_t>(vertices.size());
    mesh->indexCount = static_cast<uint32_t>(indices.size());

    size_t vSize = vertices.size() * sizeof(Forma::Vertex);
    size_t iSize = indices.size() * sizeof(uint32_t);

    // Upload Vertex Buffer
    {
        VkBuffer stagingBuffer;
        VmaAllocation stagingAlloc;
        m_allocator.createBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingAlloc);

        void* data;
        vmaMapMemory(m_allocator.getVma(), stagingAlloc, &data);
        memcpy(data, vertices.data(), vSize);
        vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);

        m_allocator.createBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, mesh->vertexBuffer, mesh->vertexAllocation);
        m_allocator.copyBuffer(stagingBuffer, mesh->vertexBuffer, vSize, m_transferQueue, m_transferPool, m_device);

        m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);
    }

    // Upload Index Buffer
    {
        VkBuffer stagingBuffer;
        VmaAllocation stagingAlloc;
        m_allocator.createBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingAlloc);

        void* data;
        vmaMapMemory(m_allocator.getVma(), stagingAlloc, &data);
        memcpy(data, indices.data(), iSize);
        vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);

        m_allocator.createBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY, mesh->indexBuffer, mesh->indexAllocation);
        m_allocator.copyBuffer(stagingBuffer, mesh->indexBuffer, iSize, m_transferQueue, m_transferPool, m_device);

        m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);
    }

    m_meshes[path] = mesh;
    return mesh;
}

std::shared_ptr<TextureAsset> AssetManager::loadTexture(const std::string& path) {
    if (m_textures.find(path) != m_textures.end()) {
        return m_textures[path];
    }

    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    
    if (!pixels) {
        throw std::runtime_error("AssetManager: Failed to load texture image: " + path);
    }

    uint32_t width = static_cast<uint32_t>(texWidth);
    uint32_t height = static_cast<uint32_t>(texHeight);
    VkDeviceSize imageSize = width * height * 4;

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "AssetManager: Loaded texture %s (%ux%u)", path.c_str(), width, height);

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    m_allocator.createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingAlloc);

    void* data;
    vmaMapMemory(m_allocator.getVma(), stagingAlloc, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);

    stbi_image_free(pixels);

    auto texture = std::make_shared<TextureAsset>();
    m_allocator.createImage(width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VMA_MEMORY_USAGE_GPU_ONLY, texture->image, texture->allocation);

    // Transitions and Copy
    m_allocator.transitionImageLayout(texture->image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   m_transferQueue, m_transferPool, m_device);
    
    m_allocator.copyBufferToImage(stagingBuffer, texture->image, width, height, m_transferQueue, m_transferPool, m_device);
    
    m_allocator.transitionImageLayout(texture->image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   m_transferQueue, m_transferPool, m_device);

    m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);

    VkImageViewCreateInfo viewInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = texture->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &texture->view) != VK_SUCCESS) {
        throw std::runtime_error("AssetManager: Failed to create texture image view for " + path);
    }

    m_textures[path] = texture;
    return texture;
}

std::shared_ptr<TextureAsset> AssetManager::loadCubemap(const std::string& directoryPath) {
    if (m_textures.find(directoryPath) != m_textures.end()) {
        return m_textures[directoryPath];
    }

    const char* faceNames[] = { "px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png" };
    stbi_uc* facePixels[6];
    int width, height, channels;

    for (int i = 0; i < 6; ++i) {
        std::string path = directoryPath + "/" + faceNames[i];
        facePixels[i] = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (!facePixels[i]) {
            // Cleanup previous loads
            for (int j = 0; j < i; ++j) stbi_image_free(facePixels[j]);
            throw std::runtime_error("AssetManager: Failed to load cubemap face: " + path);
        }
    }

    VkDeviceSize faceSize = width * height * 4;
    VkDeviceSize totalSize = faceSize * 6;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    m_allocator.createBuffer(totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingAlloc);

    void* mappedData;
    vmaMapMemory(m_allocator.getVma(), stagingAlloc, &mappedData);
    for (int i = 0; i < 6; ++i) {
        memcpy(static_cast<char*>(mappedData) + (faceSize * i), facePixels[i], faceSize);
        stbi_image_free(facePixels[i]);
    }
    vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);

    auto cubemap = std::make_shared<TextureAsset>();
    m_allocator.createImage(width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VMA_MEMORY_USAGE_GPU_ONLY, cubemap->image, cubemap->allocation,
                         6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);

    m_allocator.transitionImageLayout(cubemap->image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   m_transferQueue, m_transferPool, m_device);

    m_allocator.copyBufferToImage(stagingBuffer, cubemap->image, width, height, m_transferQueue, m_transferPool, m_device, 6);

    m_allocator.transitionImageLayout(cubemap->image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   m_transferQueue, m_transferPool, m_device);

    m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);

    VkImageViewCreateInfo viewInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = cubemap->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &cubemap->view) != VK_SUCCESS) {
        throw std::runtime_error("AssetManager: Failed to create cubemap image view for " + directoryPath);
    }

    m_textures[directoryPath] = cubemap;
    return cubemap;
}

std::shared_ptr<TextureAsset> AssetManager::loadCubemapFromCross(const std::string& path) {
    SDL_Log("AssetManager: loadCubemapFromCross checking %s", path.c_str());
    if (m_textures.find(path) != m_textures.end()) {
        return m_textures[path];
    }

    int width, height, channels;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        SDL_Log("AssetManager: stbi_load FAILED for %s", path.c_str());
        throw std::runtime_error("AssetManager: Failed to load cross cubemap texture: " + path);
    }

    SDL_Log("AssetManager: Cross texture loaded: %dx%d, %d channels", width, height, channels);

    // Determine face size. Expecting 4x3 cross or 3x4 cross.
    // Layout 4x3: 
    //   .  T  .  .
    //   L  F  R  B
    //   .  Bo .  .
    uint32_t faceSize = width / 4;
    if (height / 3 != (int)faceSize) {
        // Try 3x4 layout? 
        //   .  T  .
        //   L  F  R
        //   .  Bo .
        //   .  B  .
        if (width / 3 == height / 4) {
             faceSize = width / 3;
        } else {
             stbi_image_free(pixels);
             throw std::runtime_error("AssetManager: Cross cubemap " + path + " has invalid dimensions: " + std::to_string(width) + "x" + std::to_string(height));
        }
    }

    uint32_t cols = width / faceSize;
    // uint32_t rows = height / faceSize;

    VkDeviceSize faceByteSize = faceSize * faceSize * 4;
    VkDeviceSize totalByteSize = faceByteSize * 6;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    m_allocator.createBuffer(totalByteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingAlloc);

    void* mappedData;
    vmaMapMemory(m_allocator.getVma(), stagingAlloc, &mappedData);
    char* dstBase = static_cast<char*>(mappedData);

    // Face mapping (Vulkan order: +X, -X, +Y, -Y, +Z, -Z)
    // +X (Right):  (2, 1)
    // -X (Left):   (0, 1)
    // +Y (Top):    (1, 0)
    // -Y (Bottom): (1, 2)
    // +Z (Front):  (1, 1)
    // -Z (Back):   (3, 1) [If cols == 4]
    
    struct FacePos { int x, y; };
    std::vector<FacePos> faceCoords(6);
    faceCoords[0] = {2, 1}; // +X
    faceCoords[1] = {0, 1}; // -X
    faceCoords[2] = {1, 0}; // +Y 
    faceCoords[3] = {1, 2}; // -Y
    faceCoords[4] = {1, 1}; // +Z
    if (cols >= 4) {
        faceCoords[5] = {3, 1}; // -Z
    } else {
        // Standard 3x4 cross doesn't have 4th column, Back is usually at (1, 3) 
        faceCoords[5] = {1, 3}; 
    }

    for (int i = 0; i < 6; ++i) {
        int fx = faceCoords[i].x * faceSize;
        int fy = faceCoords[i].y * faceSize;
        char* faceDst = dstBase + (faceByteSize * i);

        for (uint32_t y = 0; y < faceSize; ++y) {
            void* srcRow = pixels + ((fy + y) * width + fx) * 4;
            void* dstRow = faceDst + (y * faceSize) * 4;
            memcpy(dstRow, srcRow, faceSize * 4);
        }
    }

    stbi_image_free(pixels);
    vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);

    auto cubemap = std::make_shared<TextureAsset>();
    m_allocator.createImage(faceSize, faceSize, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VMA_MEMORY_USAGE_GPU_ONLY, cubemap->image, cubemap->allocation,
                         6, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);

    m_allocator.transitionImageLayout(cubemap->image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   m_transferQueue, m_transferPool, m_device);

    m_allocator.copyBufferToImage(stagingBuffer, cubemap->image, faceSize, faceSize, m_transferQueue, m_transferPool, m_device, 6);

    m_allocator.transitionImageLayout(cubemap->image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   m_transferQueue, m_transferPool, m_device);

    m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);

    VkImageViewCreateInfo viewInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = cubemap->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;

    if (vkCreateImageView(m_device, &viewInfo, nullptr, &cubemap->view) != VK_SUCCESS) {
        throw std::runtime_error("AssetManager: Failed to create cubemap from cross for " + path);
    }

    m_textures[path] = cubemap;
    return cubemap;
}

} // namespace Memoria
