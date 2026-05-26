#define GLM_ENABLE_EXPERIMENTAL
#include "asset_manager.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cgltf.h>
#include <filesystem>
#include <forma/material.h>
#include <functional>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <iostream>
#include <mundus/scene.h>
#include <stb_image.h>
#include <stdexcept>
#include <vk_mem_alloc.h>

namespace Memoria {

AssetManager::AssetManager(Allocator &allocator, VkDevice device,
                           VkQueue transferQueue, VkCommandPool transferPool)
    : m_allocator(allocator), m_device(device), m_transferQueue(transferQueue),
      m_transferPool(transferPool) {

  m_defaultWhiteSRGB = createSolidTexture(255, 255, 255, 255, true);
  m_defaultWhiteLinear = createSolidTexture(255, 255, 255, 255, false);
  m_defaultBlackSRGB = createSolidTexture(0, 0, 0, 255, true);
  m_defaultBlackLinear = createSolidTexture(0, 0, 0, 255, false);
  m_defaultNormal = createSolidTexture(128, 128, 255, 255, false);
  m_defaultBRDF = createSolidTexture(255, 0, 0, 255, false);
}

std::shared_ptr<TextureAsset>
AssetManager::createSolidTexture(unsigned char r, unsigned char g,
                                 unsigned char b, unsigned char a, bool srgb) {
  unsigned char pixels[] = {r, g, b, a};
  return loadTextureFromSTB(pixels, 1, 1, srgb);
}

AssetManager::~AssetManager() {
  for (auto &mesh : m_meshStore) {
    mesh->destroy(m_allocator);
  }
  for (auto &tex : m_textureLinearStore) {
    tex->destroy(m_allocator, m_device);
  }
}

std::shared_ptr<MeshAsset> AssetManager::createCubeMesh() {
  std::vector<Forma::Vertex> vertices;
  std::vector<uint32_t> indices;
  Forma::Mesh::createCube(vertices, indices);
 
  auto mesh = std::make_shared<MeshAsset>();
  mesh->vertexCount = static_cast<uint32_t>(vertices.size());
  mesh->indexCount = static_cast<uint32_t>(indices.size());
 
  size_t vSize = vertices.size() * sizeof(Forma::Vertex);
  size_t iSize = indices.size() * sizeof(uint32_t);
 
  VkBuffer stagingBuffer;
  VmaAllocation stagingAlloc;
  m_allocator.createBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VMA_MEMORY_USAGE_AUTO, stagingBuffer,
                           stagingAlloc,
                           VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
  void *p;
  vmaMapMemory(m_allocator.getVma(), stagingAlloc, &p);
  memcpy(p, vertices.data(), vSize);
  vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);
  m_allocator.createBuffer(vSize,
                           VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           VMA_MEMORY_USAGE_GPU_ONLY,
                           mesh->vertexBuffer,
                           mesh->vertexAllocation);
  m_allocator.copyBuffer(stagingBuffer, mesh->vertexBuffer, vSize,
                        m_transferQueue, m_transferPool, m_device);
  m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);
 
  if (!indices.empty()) {
    size_t iSize_buf = indices.size() * sizeof(uint32_t);
    m_allocator.createBuffer(iSize_buf, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VMA_MEMORY_USAGE_AUTO, stagingBuffer,
                             stagingAlloc,
                             VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    vmaMapMemory(m_allocator.getVma(), stagingAlloc, &p);
    memcpy(p, indices.data(), iSize_buf);
    vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);
    m_allocator.createBuffer(iSize_buf,
                             VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                             VMA_MEMORY_USAGE_GPU_ONLY,
                             mesh->indexBuffer,
                             mesh->indexAllocation);
    m_allocator.copyBuffer(stagingBuffer, mesh->indexBuffer, iSize_buf,
                           m_transferQueue, m_transferPool, m_device);
    m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);
  }
 
  m_meshStore.push_back(mesh);
  m_meshes["@primitive(cube)"] = mesh;
  return mesh;
}
 
std::shared_ptr<TextureAsset> AssetManager::loadTexture(const std::string &path,
                                                           bool srgb) {
  std::string key = path + (srgb ? "_srgb" : "_linear");
  if (m_textures.count(key))
    return m_textures[key];

  int texWidth, texHeight, texChannels;
  unsigned char *pixels = stbi_load(path.c_str(), &texWidth, &texHeight,
                                    &texChannels, STBI_rgb_alpha);
  if (!pixels)
    throw std::runtime_error("AssetManager: Failed to load texture: " + path);

  auto tex = loadTextureFromSTB(pixels, texWidth, texHeight, srgb);
  stbi_image_free(pixels);
  m_textures[key] = tex;
  return tex;
}

std::shared_ptr<TextureAsset>
AssetManager::loadTextureFromMemory(const unsigned char *data, size_t size,
                                    const std::string &name, bool srgb) {
  std::string key = name + (srgb ? "_srgb" : "_linear");
  if (m_textures.count(key))
    return m_textures[key];

  int w, h, c;
  unsigned char *pixels = stbi_load_from_memory(data, static_cast<int>(size),
                                                &w, &h, &c, STBI_rgb_alpha);
  if (!pixels)
    throw std::runtime_error(
        "AssetManager: Failed to load texture from memory: " + name);

  auto tex = loadTextureFromSTB(pixels, w, h, srgb);
  stbi_image_free(pixels);
  m_textures[key] = tex;
  return tex;
}

std::shared_ptr<TextureAsset>
AssetManager::loadTextureFromSTB(unsigned char *pixels, int width, int height,
                                 bool srgb) {
  VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  VkDeviceSize imageSize = width * height * 4;

  // Compute mip levels
  uint32_t mipLevels = static_cast<uint32_t>(
      std::floor(std::log2(std::max(width, height)))) + 1;

  VkBuffer stagingBuffer;
  VmaAllocation stagingAlloc;
  m_allocator.createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VMA_MEMORY_USAGE_AUTO, stagingBuffer,
                           stagingAlloc,
                           VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

  void *data;
  vmaMapMemory(m_allocator.getVma(), stagingAlloc, &data);
  memcpy(data, pixels, static_cast<size_t>(imageSize));
  vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);

  auto texture = std::make_shared<TextureAsset>();
  // TRANSFER_SRC_BIT is required so mip blit can read from each level
  m_allocator.createImage(
      width, height, format, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
          VK_IMAGE_USAGE_SAMPLED_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY, texture->image, texture->allocation,
      1, 0, mipLevels);

  // Transition all mip levels to TRANSFER_DST for the initial upload
  m_allocator.transitionImageLayout(texture->image, format,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    m_transferQueue, m_transferPool, m_device,
                                    1, mipLevels);
  m_allocator.copyBufferToImage(stagingBuffer, texture->image, width, height,
                                m_transferQueue, m_transferPool, m_device);

  // generateMipmaps transitions each level to SHADER_READ_ONLY when done
  m_allocator.generateMipmaps(texture->image, format, width, height,
                              mipLevels, m_transferQueue, m_transferPool,
                              m_device);

  m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);

  VkImageViewCreateInfo viewInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = texture->image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(m_device, &viewInfo, nullptr, &texture->view) !=
      VK_SUCCESS) {
    throw std::runtime_error(
        "AssetManager: Failed to create texture image view");
  }

  texture->textureId = static_cast<uint32_t>(m_textureLinearStore.size());
  texture->width = width;
  texture->height = height;
  texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  m_textureLinearStore.push_back(texture);

  return texture;
}

std::shared_ptr<TextureAsset>
AssetManager::loadCubemap(const std::string &directoryPath) {
  if (m_textures.find(directoryPath) != m_textures.end()) {
    return m_textures[directoryPath];
  }

  const char *faceNames[] = {"px.png", "nx.png", "py.png",
                             "ny.png", "pz.png", "nz.png"};
  stbi_uc *facePixels[6];
  int width, height, channels;

  for (int i = 0; i < 6; ++i) {
    std::string path = directoryPath + "/" + faceNames[i];
    facePixels[i] =
        stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (!facePixels[i]) {
      // Cleanup previous loads
      for (int j = 0; j < i; ++j)
        stbi_image_free(facePixels[j]);
      throw std::runtime_error("AssetManager: Failed to load cubemap face: " +
                               path);
    }
  }

  VkDeviceSize faceSize = width * height * 4;
  VkDeviceSize totalSize = faceSize * 6;

  VkBuffer stagingBuffer;
  VmaAllocation stagingAlloc;
  m_allocator.createBuffer(totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VMA_MEMORY_USAGE_AUTO, stagingBuffer,
                           stagingAlloc,
                           VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

  void *mappedData;
  vmaMapMemory(m_allocator.getVma(), stagingAlloc, &mappedData);
  for (int i = 0; i < 6; ++i) {
    memcpy(static_cast<char *>(mappedData) + (faceSize * i), facePixels[i],
           faceSize);
    stbi_image_free(facePixels[i]);
  }
  vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);

  auto cubemap = std::make_shared<TextureAsset>();
  m_allocator.createImage(
      width, height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY, cubemap->image, cubemap->allocation, 6,
      VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT);

  m_allocator.transitionImageLayout(cubemap->image, VK_FORMAT_R8G8B8A8_SRGB,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    m_transferQueue, m_transferPool, m_device);

  m_allocator.copyBufferToImage(stagingBuffer, cubemap->image, width, height,
                                m_transferQueue, m_transferPool, m_device, 6);

  m_allocator.transitionImageLayout(cubemap->image, VK_FORMAT_R8G8B8A8_SRGB,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
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

  if (vkCreateImageView(m_device, &viewInfo, nullptr, &cubemap->view) !=
      VK_SUCCESS) {
    throw std::runtime_error(
        "AssetManager: Failed to create cubemap image view for " +
        directoryPath);
  }

  m_textures[directoryPath] = cubemap;
  return cubemap;
}

std::shared_ptr<TextureAsset>
AssetManager::loadCubemapFromCross(const std::string &path) {
  SDL_Log("AssetManager: loadCubemapFromCross checking %s", path.c_str());
  if (m_textures.find(path) != m_textures.end()) {
    return m_textures[path];
  }

  int width, height, channels;
  stbi_uc *pixels =
      stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
  if (!pixels) {
    SDL_Log("AssetManager: stbi_load FAILED for %s", path.c_str());
    throw std::runtime_error(
        "AssetManager: Failed to load cross cubemap texture: " + path);
  }

  SDL_Log("AssetManager: Cross texture loaded: %dx%d, %d channels", width,
          height, channels);

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
      throw std::runtime_error(
          "AssetManager: Cross cubemap " + path + " has invalid dimensions: " +
          std::to_string(width) + "x" + std::to_string(height));
    }
  }

  uint32_t cols = width / faceSize;
  // uint32_t rows = height / faceSize;

  VkDeviceSize faceByteSize = faceSize * faceSize * 4;
  VkDeviceSize totalByteSize = faceByteSize * 6;

  VkBuffer stagingBuffer;
  VmaAllocation stagingAlloc;
  m_allocator.createBuffer(totalByteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VMA_MEMORY_USAGE_AUTO, stagingBuffer,
                           stagingAlloc,
                           VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

  void *mappedData;
  vmaMapMemory(m_allocator.getVma(), stagingAlloc, &mappedData);
  char *dstBase = static_cast<char *>(mappedData);

  // Face mapping (Vulkan order: +X, -X, +Y, -Y, +Z, -Z)
  // +X (Right):  (2, 1)
  // -X (Left):   (0, 1)
  // +Y (Top):    (1, 0)
  // -Y (Bottom): (1, 2)
  // +Z (Front):  (1, 1)
  // -Z (Back):   (3, 1) [If cols == 4]

  struct FacePos {
    int x, y;
  };
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
    char *faceDst = dstBase + (faceByteSize * i);

    for (uint32_t y = 0; y < faceSize; ++y) {
      void *srcRow = pixels + ((fy + y) * width + fx) * 4;
      void *dstRow = faceDst + (y * faceSize) * 4;
      memcpy(dstRow, srcRow, faceSize * 4);
    }
  }

  stbi_image_free(pixels);
  vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);
  uint32_t mipLevels =
      static_cast<uint32_t>(std::floor(std::log2(faceSize))) + 1;

  auto cubemap = std::make_shared<TextureAsset>();
  m_allocator.createImage(
      faceSize, faceSize, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
          VK_IMAGE_USAGE_SAMPLED_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY, cubemap->image, cubemap->allocation, 6,
      VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, mipLevels);

  m_allocator.transitionImageLayout(
      cubemap->image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_transferQueue, m_transferPool,
      m_device, 6, mipLevels);

  m_allocator.copyBufferToImage(stagingBuffer, cubemap->image, faceSize,
                                faceSize, m_transferQueue, m_transferPool,
                                m_device, 6, 0);

  // Generate mipmaps
  m_allocator.generateMipmaps(cubemap->image, VK_FORMAT_R8G8B8A8_SRGB, faceSize,
                              faceSize, mipLevels, m_transferQueue,
                              m_transferPool, m_device, 6);

  // transitionImageLayout was already handled inside generateMipmaps for all
  // mips (to SHADER_READ_ONLY) Actually, generateMipmaps leaves them in
  // SHADER_READ_ONLY_OPTIMAL.

  m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);

  VkImageViewCreateInfo viewInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = cubemap->image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
  viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 6;

  if (vkCreateImageView(m_device, &viewInfo, nullptr, &cubemap->view) !=
      VK_SUCCESS) {
    throw std::runtime_error(
        "AssetManager: Failed to create cubemap from cross for " + path);
  }

  m_textures[path] = cubemap;
  return cubemap;
}

std::shared_ptr<TextureAsset>
AssetManager::loadHDR(const std::string &path) {
  if (m_textures.find(path) != m_textures.end()) {
    return m_textures[path];
  }

  int w, h, c;
  float *hdrPixels = stbi_loadf(path.c_str(), &w, &h, &c, 4);
  if (!hdrPixels) {
    throw std::runtime_error("AssetManager: Failed to load HDR: " + path);
  }

  VkDeviceSize imageSize = static_cast<VkDeviceSize>(w) * h * 4 * sizeof(float);

  VkBuffer stagingBuffer;
  VmaAllocation stagingAlloc;
  m_allocator.createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VMA_MEMORY_USAGE_AUTO, stagingBuffer,
                           stagingAlloc,
                           VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);

  void *data;
  vmaMapMemory(m_allocator.getVma(), stagingAlloc, &data);
  memcpy(data, hdrPixels, static_cast<size_t>(imageSize));
  vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);
  stbi_image_free(hdrPixels);

  auto texture = std::make_shared<TextureAsset>();
  m_allocator.createImage(
      w, h, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY, texture->image, texture->allocation,
      1, 0, 1);

  m_allocator.transitionImageLayout(
      texture->image, VK_FORMAT_R32G32B32A32_SFLOAT,
      VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      m_transferQueue, m_transferPool, m_device, 1, 1);
  m_allocator.copyBufferToImage(stagingBuffer, texture->image, w, h,
                                m_transferQueue, m_transferPool, m_device, 1);
  m_allocator.transitionImageLayout(
      texture->image, VK_FORMAT_R32G32B32A32_SFLOAT,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, m_transferQueue,
      m_transferPool, m_device, 1, 1);

  m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);

  VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = texture->image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R32G32B32A32_SFLOAT;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(m_device, &viewInfo, nullptr, &texture->view) !=
      VK_SUCCESS) {
    throw std::runtime_error(
        "AssetManager: Failed to create HDR texture image view");
  }

  texture->textureId = static_cast<uint32_t>(m_textureLinearStore.size());
  texture->width = w;
  texture->height = h;
  texture->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  m_textures[path] = texture;
  m_textureLinearStore.push_back(texture);

  return texture;
}

std::vector<AssetManager::SkyboxAsset> AssetManager::scanSkyboxes() const {
  std::vector<SkyboxAsset> results;
  auto baseDir = std::string(CONCORDIA_ASSETS_DIR) + "/images/skybox";

  auto scanDir = [&](const std::string &subDir, bool isHDR) {
    std::string dirPath = baseDir + "/" + subDir;
    if (!std::filesystem::exists(dirPath))
      return;
    for (auto &entry : std::filesystem::directory_iterator(dirPath)) {
      if (!entry.is_regular_file())
        continue;
      auto ext = entry.path().extension().string();
      if (isHDR) {
        if (ext != ".hdr")
          continue;
      } else {
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg")
          continue;
      }
      SkyboxAsset asset;
      asset.name = entry.path().stem().string();
      asset.path = entry.path().string();
      asset.isHDR = isHDR;
      results.push_back(std::move(asset));
    }
  };

  scanDir("cubemap", false);
  scanDir("hdri", true);

  // Sort by name for consistent ordering
  std::sort(results.begin(), results.end(),
            [](const SkyboxAsset &a, const SkyboxAsset &b) {
              return a.name < b.name;
            });

  return results;
}

void AssetManager::loadGLTF(const std::string &path, Mundus::Scene &scene,
                            int parentIndex) {
  SDL_Log("AssetManager: loadGLTF: parsing %s", path.c_str());
  cgltf_options options = {};
  cgltf_data *data = nullptr;
  cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
  if (result != cgltf_result_success) {
    throw std::runtime_error("AssetManager: Failed to parse GLTF: " + path);
  }
  SDL_Log("AssetManager: loadGLTF: parsed, loading buffers...");

  result = cgltf_load_buffers(&options, data, path.c_str());
  if (result != cgltf_result_success) {
    cgltf_free(data);
    throw std::runtime_error("AssetManager: Failed to load GLTF buffers: " +
                             path);
  }
  SDL_Log("AssetManager: loadGLTF: buffers loaded, mapping materials...");

  std::filesystem::path modelPath(path);
  std::string baseDir = modelPath.parent_path().string();

  // Map GLTF materials to our Materials
  std::vector<std::shared_ptr<Forma::Material>> materials;
  for (size_t i = 0; i < data->materials_count; ++i) {
    cgltf_material &gmat = data->materials[i];
    auto loadGLTFTexture = [&](cgltf_texture_view &view,
                               bool srgb) -> std::shared_ptr<TextureAsset> {
      if (!view.texture || !view.texture->image)
        return nullptr;
      cgltf_image *img = view.texture->image;
      if (img->uri) {
        return loadTexture(baseDir + "/" + img->uri, srgb);
      } else if (img->buffer_view) {
        unsigned char *data_ptr = (unsigned char *)img->buffer_view->buffer->data +
                                  img->buffer_view->offset;
        size_t size = img->buffer_view->size;
        size_t imgIdx = img - data->images;
        std::string name = img->name ? img->name
                                     : "embedded_img_" + std::to_string(imgIdx);
        return loadTextureFromMemory(data_ptr, size, name, srgb);
      }
      return nullptr;
    };

    auto mat = std::make_shared<Forma::Material>();
    mat->shaderName = "pbr";

    auto textureSource = [&](cgltf_texture_view &view) -> std::string {
      if (!view.texture || !view.texture->image) return {};
      cgltf_image *img = view.texture->image;
      if (img->uri) return baseDir + "/" + img->uri;
      if (img->buffer_view) {
        size_t imgIdx = img - data->images;
        return img->name ? std::string(img->name)
                         : "embedded_img_" + std::to_string(imgIdx);
      }
      return {};
    };

    if (gmat.has_pbr_metallic_roughness) {
      cgltf_pbr_metallic_roughness &pbr = gmat.pbr_metallic_roughness;
      mat->baseColor = glm::make_vec4(pbr.base_color_factor);
      mat->metallic = pbr.metallic_factor;
      mat->roughness = pbr.roughness_factor;

      mat->albedo = loadGLTFTexture(pbr.base_color_texture, true);
      mat->albedoSource = textureSource(pbr.base_color_texture);
      mat->metallicRoughness =
          loadGLTFTexture(pbr.metallic_roughness_texture, false);
      mat->metallicRoughnessSource = textureSource(pbr.metallic_roughness_texture);
    }

    mat->normal = loadGLTFTexture(gmat.normal_texture, false);
    mat->normalSource = textureSource(gmat.normal_texture);
    mat->ao = loadGLTFTexture(gmat.occlusion_texture, false);
    mat->aoSource = textureSource(gmat.occlusion_texture);
    mat->emissive = loadGLTFTexture(gmat.emissive_texture, true);
    mat->emissiveSource = textureSource(gmat.emissive_texture);

    // Set bindless indices
    // Fallbacks for missing textures
    if (!mat->albedo) mat->albedo = getDefaultWhite();
    if (!mat->normal) mat->normal = getDefaultNormal();
    if (!mat->metallicRoughness) mat->metallicRoughness = getDefaultWhiteLinear();
    if (!mat->ao) mat->ao = getDefaultWhiteLinear();
    if (!mat->emissive) mat->emissive = getDefaultBlack();

    // Set bindless indices (after fallbacks!)
    mat->albedoIdx = mat->albedo->textureId;
    mat->normalIdx = mat->normal->textureId;
    mat->metallicRoughnessIdx = mat->metallicRoughness->textureId;
    mat->aoIdx = mat->ao->textureId;
    mat->emissiveIdx = mat->emissive->textureId;

    materials.push_back(mat);
  }

  // Default material if mesh has none
  auto defaultMat = std::make_shared<Forma::Material>();
  defaultMat->shaderName = "pbr";

  // Recursive Node Loader
  SDL_Log("AssetManager: loadGLTF: starting node processing...");
  std::function<void(cgltf_node *, int)> processNode = [&](cgltf_node *node,
                                                           int parentIdx) {
    SDL_Log("AssetManager: loadGLTF: processing node %s",
            node->name ? node->name : "unnamed");
    int entityIdx =
        scene.addEntity(node->name ? node->name : "GLTF_Node", parentIdx);

    // Track mesh source for serialization
    {
      auto &ent = scene.getEntities()[entityIdx];
      ent.meshSource = path;
    }

    // Transform (Fetch reference AFTER possibly reallocating in addEntity)
    {
      auto &ent = scene.getEntities()[entityIdx];
      if (node->has_translation)
        ent.transform.position = glm::make_vec3(node->translation);
      if (node->has_rotation)
        ent.transform.rotation =
            glm::eulerAngles(glm::make_quat(node->rotation));
      if (node->has_scale)
        ent.transform.scale = glm::make_vec3(node->scale);
      if (node->has_matrix) {
        glm::mat4 m = glm::make_mat4(node->matrix);
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::quat rotation;
        glm::decompose(m, ent.transform.scale, rotation, ent.transform.position,
                       skew, perspective);
        ent.transform.rotation = glm::eulerAngles(rotation);
      }
    }

    if (node->mesh) {
      for (size_t i = 0; i < node->mesh->primitives_count; ++i) {
        cgltf_primitive &prim = node->mesh->primitives[i];

        int primIdx = entityIdx;
        if (node->mesh->primitives_count > 1) {
          primIdx = scene.addEntity(node->name ? (std::string(node->name) +
                                                   "_prim" + std::to_string(i))
                                                : "GLTF_Primitive",
                                    entityIdx);
          scene.getEntities()[primIdx].meshSource = path;
        }

        // Fetch reference again at the latest possible time
        auto &primEnt = scene.getEntities()[primIdx];

        // Load Mesh Data
        std::vector<Forma::Vertex> vertices;
        std::vector<uint32_t> indices;

        cgltf_accessor *posAccessor = nullptr;
        cgltf_accessor *normAccessor = nullptr;
        cgltf_accessor *uvAccessor = nullptr;
        cgltf_accessor *tangentAccessor = nullptr;

        for (size_t a = 0; a < prim.attributes_count; ++a) {
          cgltf_attribute &attr = prim.attributes[a];
          if (attr.type == cgltf_attribute_type_position) {
            posAccessor = attr.data;
          } else if (attr.type == cgltf_attribute_type_normal) {
            normAccessor = attr.data;
          } else if (attr.type == cgltf_attribute_type_texcoord) {
            uvAccessor = attr.data;
          } else if (attr.type == cgltf_attribute_type_tangent) {
            tangentAccessor = attr.data;
          }
        }

        if (posAccessor) {
          size_t posCount = posAccessor->count;
          vertices.resize(posCount);

          std::vector<float> posData(posCount * 3);
          cgltf_accessor_unpack_floats(posAccessor, posData.data(),
                                       posData.size());

          std::vector<float> normData;
          if (normAccessor) {
            normData.resize(posCount * 3);
            cgltf_accessor_unpack_floats(normAccessor, normData.data(),
                                         normData.size());
          }

          std::vector<float> uvData;
          if (uvAccessor) {
            uvData.resize(posCount * 2);
            cgltf_accessor_unpack_floats(uvAccessor, uvData.data(),
                                         uvData.size());
          }

          std::vector<float> tangentData;
          if (tangentAccessor) {
            tangentData.resize(posCount * 4);
            cgltf_accessor_unpack_floats(tangentAccessor, tangentData.data(),
                                         tangentData.size());
          }

          for (size_t v = 0; v < posCount; ++v) {
            vertices[v].pos = {posData[v * 3 + 0], posData[v * 3 + 1],
                               posData[v * 3 + 2]};
            if (normAccessor) {
              vertices[v].normal = {normData[v * 3 + 0], normData[v * 3 + 1],
                                     normData[v * 3 + 2]};
            } else {
              // Default normal, will be updated to face normal if this is a triangle
              vertices[v].normal = {0.0f, 0.0f, 1.0f};
            }
            if (uvAccessor) {
              vertices[v].texCoord = {uvData[v * 2 + 0], uvData[v * 2 + 1]};
            }
            if (tangentAccessor) {
              vertices[v].tangent =
                  glm::vec4(tangentData[v * 4 + 0], tangentData[v * 4 + 1],
                            tangentData[v * 4 + 2], tangentData[v * 4 + 3]);
            } else {
              vertices[v].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
          }
 
          if (prim.indices) {
            indices.resize(prim.indices->count);
            for (size_t k = 0; k < prim.indices->count; ++k) {
              indices[k] = (uint32_t)cgltf_accessor_read_index(prim.indices, k);
            }
          }
 
          // Compute normals if missing
          if (!normAccessor && !indices.empty()) {
            SDL_Log("AssetManager: Computing normals for GLTF primitive...");
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
              uint32_t i0 = indices[i];
              uint32_t i1 = indices[i + 1];
              uint32_t i2 = indices[i + 2];
              if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;
              glm::vec3 edge1 = vertices[i1].pos - vertices[i0].pos;
              glm::vec3 edge2 = vertices[i2].pos - vertices[i0].pos;
              glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));
              vertices[i0].normal += faceNormal;
              vertices[i1].normal += faceNormal;
              vertices[i2].normal += faceNormal;
            }
            for (auto &v : vertices) {
              v.normal = glm::normalize(v.normal);
            }
          }
        }
 
        auto meshAsset = std::make_shared<MeshAsset>();
        meshAsset->vertexCount = static_cast<uint32_t>(vertices.size());
        meshAsset->indexCount = static_cast<uint32_t>(indices.size());

        // Compute AABB
        if (!vertices.empty()) {
          glm::vec3 bMin = vertices[0].pos;
          glm::vec3 bMax = vertices[0].pos;
          for (auto &v : vertices) {
            bMin = glm::min(bMin, v.pos);
            bMax = glm::max(bMax, v.pos);
          }
          meshAsset->aabbMin = bMin;
          meshAsset->aabbMax = bMax;
        }

        {
          size_t vSize = vertices.size() * sizeof(Forma::Vertex);
          VkBuffer stagingBuffer;
          VmaAllocation stagingAlloc;
          m_allocator.createBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VMA_MEMORY_USAGE_AUTO, stagingBuffer,
                                   stagingAlloc,
                                   VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
          void *p;
          vmaMapMemory(m_allocator.getVma(), stagingAlloc, &p);
          memcpy(p, vertices.data(), vSize);
          vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);
          m_allocator.createBuffer(vSize,
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                   VMA_MEMORY_USAGE_GPU_ONLY,
                                   meshAsset->vertexBuffer,
                                   meshAsset->vertexAllocation);
          m_allocator.copyBuffer(stagingBuffer, meshAsset->vertexBuffer, vSize,
                                 m_transferQueue, m_transferPool, m_device);
          m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);

          if (!indices.empty()) {
            size_t iSize = indices.size() * sizeof(uint32_t);
            m_allocator.createBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     VMA_MEMORY_USAGE_AUTO, stagingBuffer,
                                     stagingAlloc,
                                     VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
            vmaMapMemory(m_allocator.getVma(), stagingAlloc, &p);
            memcpy(p, indices.data(), iSize);
            vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);
            m_allocator.createBuffer(iSize,
                                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                         VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                     VMA_MEMORY_USAGE_GPU_ONLY,
                                     meshAsset->indexBuffer,
                                     meshAsset->indexAllocation);
            m_allocator.copyBuffer(stagingBuffer, meshAsset->indexBuffer, iSize,
                                   m_transferQueue, m_transferPool, m_device);
            m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);
          }
        }

        primEnt.mesh = meshAsset;
        m_meshStore.push_back(meshAsset);
        m_meshes[path] = meshAsset;

        if (prim.material) {
          for (size_t mIdx = 0; mIdx < data->materials_count; ++mIdx) {
            if (&data->materials[mIdx] == prim.material) {
              primEnt.material = materials[mIdx];
              break;
            }
          }
        } else {
          primEnt.material = defaultMat;
        }
      }
    }

    for (size_t i = 0; i < node->children_count; ++i) {
      processNode(node->children[i], entityIdx);
    }
  };

  cgltf_scene *scenePtr = data->scene;
  if (!scenePtr && data->scenes_count > 0)
    scenePtr = &data->scenes[0];

  if (scenePtr) {
    for (size_t i = 0; i < scenePtr->nodes_count; ++i) {
      processNode(scenePtr->nodes[i], parentIndex);
    }
  }

  cgltf_free(data);
  SDL_Log("AssetManager: Successfully loaded GLTF %s", path.c_str());
}

std::shared_ptr<MeshAsset> AssetManager::getMesh(const std::string &source) {
  auto it = m_meshes.find(source);
  if (it != m_meshes.end())
    return it->second;

  if (source == "@primitive(cube)") {
    auto mesh = createCubeMesh();
    m_meshes[source] = mesh;
    return mesh;
  }

  // Resolve relative file paths against assets root
  if (!source.empty() && source[0] != '@') {
    std::string full = std::string(CONCORDIA_ASSETS_DIR) + "/" + source;
    auto it2 = m_meshes.find(full);
    if (it2 != m_meshes.end())
      return it2->second;
  }

  return nullptr;
}

std::shared_ptr<TextureAsset>
AssetManager::resolveTexture(const std::string &source, bool srgb) {
  if (source.empty())
    return nullptr;
  std::string key = source + (srgb ? "_srgb" : "_linear");
  auto it = m_textures.find(key);
  if (it != m_textures.end())
    return it->second;
  // File-based: try loading from assets root
  if (source.find('/') != std::string::npos) {
    std::string full = std::string(CONCORDIA_ASSETS_DIR) + "/" + source;
    return loadTexture(full, srgb);
  }
  return nullptr;
}

} // namespace Memoria
