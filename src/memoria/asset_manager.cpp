#define GLM_ENABLE_EXPERIMENTAL
#include "asset_manager.h"
#include "auxilia/ctoon.hpp"
#include "mundus/components.h"
#include "mundus/schema.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cgltf.h>
#include <filesystem>
#include <forma/material.h>
#include <functional>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <stb_image.h>
#include <stdexcept>
#include <vk_mem_alloc.h>

namespace Memoria {

static inline const char *obj_str(ctoon_value *obj, const char *key) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  return v && v->type == CTOON_STRING ? v->str_val : nullptr;
}
static inline double obj_num(ctoon_value *obj, const char *key, double def = 0.0) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  return v && v->type == CTOON_NUMBER ? v->num_val : def;
}
static inline bool obj_bool(ctoon_value *obj, const char *key, bool def = false) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  return v && v->type == CTOON_BOOL ? v->bool_val : def;
}

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
    if (mesh) {
      mesh->destroy(m_allocator);
    }
  }
  for (auto &pair : m_meshes) {
    if (pair.second) {
      pair.second->destroy(m_allocator);
    }
  }
  for (auto &tex : m_textureLinearStore) {
    if (tex) {
      tex->destroy(m_allocator, m_device);
    }
  }
  for (auto &pair : m_textures) {
    if (pair.second) {
      pair.second->destroy(m_allocator, m_device);
    }
  }
}

bool AssetManager::loadManifest(const char *path, flecs::world &ecs) {
  Auxilia::ctoon_doc doc;
  if (!doc.load_file(path)) {
    SDL_Log("AssetManager: failed to load manifest: %s", path);
    return false;
  }
  ctoon_value *root = doc.get();
  std::string errors;
  if (!Mundus::Schema::validateManifest(root, errors)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Asset manifest schema violations:\n%s", errors.c_str());
  }

  // Parse preload meshes
  ctoon_value *arr = ctoon_obj_get(root, "preload_meshes");
  if (arr && arr->type == CTOON_ARRAY) {
    std::string prefix = std::string(CONCORDIA_ASSETS_DIR) + "/";
    for (size_t i = 0; i < arr->len; ++i) {
      ctoon_value *e = &arr->arr[i];
      if (e->type != CTOON_STRING || !e->str_val)
        continue;
      const char *s = e->str_val;
      m_manifest.preloadMeshes.push_back(s);
      size_t len = strlen(s);

      if (len > 11 && strncmp(s, "@primitive(", 11) == 0 && s[len - 1] == ')') {
        // Primitive mesh — create if not already cached
        if (!m_meshes.count(s))
          createCubeMesh();
        SDL_Log("AssetManager: preloaded %s", s);
      } else if (len > 17 && strncmp(s, "@asset(assets://", 16) == 0 &&
                 s[len - 1] == ')') {
        // GLTF model — extract relative path and load
        std::string relPath(s + 16, len - 17);
        std::string fullPath = prefix + relPath;
        if (!m_meshes.count(fullPath)) {
          loadGLTF(fullPath, ecs, 0, false);
          // Strip the assets dir prefix from loaded entity sources
          auto strip = [&](std::string &t) {
            if (t.compare(0, prefix.size(), prefix) == 0)
              t = t.substr(prefix.size());
          };
          ecs.query_builder<>()
            .with<Mundus::MeshSource>()
            .build()
            .each([&](flecs::iter &it, size_t row) {
              flecs::entity e = it.entity(row);
              auto &ms = e.get_mut<Mundus::MeshSource>();
              strip(ms.value);
              auto &mar = e.get_mut<Mundus::MeshAssetRef>();
              if (mar.value) {
                // Update cache key to stripped path
                m_meshes[ms.value] = mar.value;
              }
              auto &matRef = e.get_mut<Mundus::MaterialRef>();
              if (matRef.value) {
                strip(matRef.value->albedoSource);
                strip(matRef.value->normalSource);
                strip(matRef.value->metallicRoughnessSource);
                strip(matRef.value->aoSource);
                strip(matRef.value->emissiveSource);
              }
            });
        }
        SDL_Log("AssetManager: preloaded %s", s);
      }
    }
  }

  // Parse default skybox
  ctoon_value *sky = ctoon_obj_get(root, "default_skybox");
  if (sky && sky->type == CTOON_STRING && sky->str_val) {
    const char *s = sky->str_val;
    size_t len = strlen(s);
    if (len > 17 && strncmp(s, "@asset(assets://", 16) == 0 && s[len - 1] == ')') {
      m_manifest.defaultSkybox = std::string(s + 16, len - 17);
    }
  }

  // Parse skybox config
  ctoon_value *skyboxCfg = ctoon_obj_get(root, "skybox");
  if (skyboxCfg && skyboxCfg->type == CTOON_OBJECT) {
    // Scan directories
    ctoon_value *dirs = ctoon_obj_get(skyboxCfg, "scan_directories");
    if (dirs && dirs->type == CTOON_ARRAY) {
      for (size_t i = 0; i < dirs->len; ++i) {
        ctoon_value *d = &dirs->arr[i];
        if (d->type != CTOON_OBJECT) continue;
        const char *p = obj_str(d, "path");
        if (!p) continue;
        SkyboxScanDir sd;
        sd.path = p;
        sd.isHDR = obj_bool(d, "is_hdr", false);
        m_skyboxScanDirs.push_back(sd);
      }
    }
    // Face names
    ctoon_value *faces = ctoon_obj_get(skyboxCfg, "face_names");
    if (faces && faces->type == CTOON_ARRAY) {
      for (size_t i = 0; i < faces->len; ++i) {
        if (faces->arr[i].type == CTOON_STRING && faces->arr[i].str_val)
          m_skyboxFaceNames.push_back(faces->arr[i].str_val);
      }
    }
  }
  if (m_skyboxScanDirs.empty()) {
    m_skyboxScanDirs.push_back({"cubemap", false});
    m_skyboxScanDirs.push_back({"hdri", true});
  }
  if (m_skyboxFaceNames.empty()) {
    m_skyboxFaceNames = {"px.png", "nx.png", "py.png",
                         "ny.png", "pz.png", "nz.png"};
  }

  // Parse default material
  ctoon_value *dm = ctoon_obj_get(root, "default_material");
  if (dm && dm->type == CTOON_OBJECT) {
    const char *s = obj_str(dm, "shader");
    if (s) m_defaultMaterial.shader = s;
    const char *bc = obj_str(dm, "base_color");
    if (bc) {
      glm::vec4 v;
      if (sscanf(bc, "@color(%f,%f,%f,%f)", &v.x, &v.y, &v.z, &v.w) == 4)
        m_defaultMaterial.baseColor = v;
    }
    m_defaultMaterial.roughness =
        (float)obj_num(dm, "roughness", 0.5);
    m_defaultMaterial.metallic =
        (float)obj_num(dm, "metallic", 0.0);
  }

  SDL_Log("AssetManager: manifest loaded from %s", path);
  return true;
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

  stbi_uc *facePixels[6];
  int width, height, channels;

  for (int i = 0; i < 6; ++i) {
    std::string faceName = (i < (int)m_skyboxFaceNames.size())
                               ? m_skyboxFaceNames[i]
                               : std::string("face") + std::to_string(i) + ".png";
    std::string path = directoryPath + "/" + faceName;
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

  for (auto &sc : m_skyboxScanDirs) {
    std::string dirPath = baseDir + "/" + sc.path;
    if (!std::filesystem::exists(dirPath))
      continue;
    for (auto &entry : std::filesystem::directory_iterator(dirPath)) {
      if (!entry.is_regular_file())
        continue;
      auto ext = entry.path().extension().string();
      if (sc.isHDR) {
        if (ext != ".hdr")
          continue;
      } else {
        if (ext != ".png" && ext != ".jpg" && ext != ".jpeg")
          continue;
      }
      SkyboxAsset asset;
      asset.name = entry.path().stem().string();
      asset.path = entry.path().string();
      asset.isHDR = sc.isHDR;
      results.push_back(std::move(asset));
    }
  }

  // Sort by name for consistent ordering
  std::sort(results.begin(), results.end(),
            [](const SkyboxAsset &a, const SkyboxAsset &b) {
              return a.name < b.name;
            });

  return results;
}

void AssetManager::loadGLTF(const std::string &path, flecs::world &ecs,
                            flecs::entity_t parent, bool instantiate) {
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
    SDL_Log("AssetManager: loadGLTF material[%zu] name=%s, alpha_mode=%d, baseColor=(%f, %f, %f, %f)",
            i, gmat.name ? gmat.name : "unnamed", (int)gmat.alpha_mode,
            gmat.has_pbr_metallic_roughness ? gmat.pbr_metallic_roughness.base_color_factor[0] : 1.0f,
            gmat.has_pbr_metallic_roughness ? gmat.pbr_metallic_roughness.base_color_factor[1] : 1.0f,
            gmat.has_pbr_metallic_roughness ? gmat.pbr_metallic_roughness.base_color_factor[2] : 1.0f,
            gmat.has_pbr_metallic_roughness ? gmat.pbr_metallic_roughness.base_color_factor[3] : 1.0f);
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
    bool isTransparent = (gmat.alpha_mode == cgltf_alpha_mode_blend) ||
                         (gmat.has_transmission && gmat.transmission.transmission_factor > 0.0f);
    if (isTransparent) {
      mat->shaderName = "transparent";
    } else {
      mat->shaderName = "pbr";
    }

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
      if (gmat.has_transmission && gmat.transmission.transmission_factor > 0.0f) {
        mat->baseColor.w = 1.0f - gmat.transmission.transmission_factor;
      }
      mat->metallic = pbr.metallic_factor;
      mat->roughness = pbr.roughness_factor;

      mat->albedo = loadGLTFTexture(pbr.base_color_texture, true);
      mat->albedoSource = textureSource(pbr.base_color_texture);
      mat->metallicRoughness =
          loadGLTFTexture(pbr.metallic_roughness_texture, false);
      mat->metallicRoughnessSource = textureSource(pbr.metallic_roughness_texture);
    } else if (gmat.has_transmission && gmat.transmission.transmission_factor > 0.0f) {
      mat->baseColor.w = 1.0f - gmat.transmission.transmission_factor;
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
  std::function<flecs::entity(cgltf_node *, flecs::entity)> processNode;
  processNode = [&](cgltf_node *node, flecs::entity parentEnt) -> flecs::entity {
    SDL_Log("AssetManager: loadGLTF: processing node %s",
            node->name ? node->name : "unnamed");

    std::string meshSrc = path;
    std::string ap = std::string(CONCORDIA_ASSETS_DIR) + "/";
    if (meshSrc.compare(0, ap.size(), ap) == 0) {
      meshSrc = meshSrc.substr(ap.size());
    }

    flecs::entity e;
    if (instantiate) {
      const char *nodeName = node->name ? node->name : "GLTF_Node";
      e = ecs.entity()
        .set<Mundus::Name>({nodeName})
        .set<Mundus::MeshSource>({meshSrc})
        .set<Mundus::Visibility>({true})
        .set<Mundus::EffectiveVisibility>({true})
        .set<Mundus::GlobalTransform>({glm::mat4(1.0f)})
        .set<Mundus::MeshAssetRef>({nullptr})
        .set<Mundus::MaterialRef>({nullptr})
        .set<Mundus::GltfInternalNode>({});

      if (parentEnt)
        e.add(flecs::ChildOf, parentEnt);
    }

    // Transform
    Mundus::Transform tf;
    if (node->has_translation)
      tf.position = glm::make_vec3(node->translation);
    if (node->has_rotation)
      tf.rotation = glm::eulerAngles(glm::make_quat(node->rotation));
    if (node->has_scale)
      tf.scale = glm::make_vec3(node->scale);
    if (node->has_matrix) {
      glm::mat4 m = glm::make_mat4(node->matrix);
      glm::vec3 skew;
      glm::vec4 perspective;
      glm::quat rotation;
      glm::decompose(m, tf.scale, rotation, tf.position, skew, perspective);
      tf.rotation = glm::eulerAngles(rotation);
    }
    if (instantiate) {
      e.set<Mundus::Transform>(tf);
      e.set<Mundus::GltfDefaultTransform>({tf.position, tf.rotation, tf.scale});
    }

    if (node->mesh) {
      for (size_t i = 0; i < node->mesh->primitives_count; ++i) {
        cgltf_primitive &prim = node->mesh->primitives[i];

        flecs::entity primEnt = e;
        if (instantiate && node->mesh->primitives_count > 1) {
          std::string primName = node->name ?
            (std::string(node->name) + "_prim" + std::to_string(i)) :
            "GLTF_Primitive";
          primEnt = ecs.entity()
            .set<Mundus::Name>({primName})
            .set<Mundus::MeshSource>({meshSrc})
            .set<Mundus::Visibility>({true})
            .set<Mundus::EffectiveVisibility>({true})
            .set<Mundus::GlobalTransform>({glm::mat4(1.0f)})
            .set<Mundus::Transform>({tf})
            .set<Mundus::MeshAssetRef>({nullptr})
            .set<Mundus::MaterialRef>({nullptr})
            .set<Mundus::GltfInternalNode>({});
          primEnt.add(flecs::ChildOf, e);
        }
        if (instantiate) {
          primEnt.set<Mundus::GltfDefaultTransform>({tf.position, tf.rotation, tf.scale});
        }

        // Load / Reuse Mesh Data
        std::shared_ptr<MeshAsset> meshAsset;
        std::string meshKey = path + "::mesh" + std::to_string(node->mesh - data->meshes) + "::prim" + std::to_string(i);
        auto cachedIt = m_meshes.find(meshKey);
        if (cachedIt != m_meshes.end()) {
          meshAsset = cachedIt->second;
        } else {
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
              for (size_t iIdx = 0; iIdx + 2 < indices.size(); iIdx += 3) {
                uint32_t i0 = indices[iIdx];
                uint32_t i1 = indices[iIdx + 1];
                uint32_t i2 = indices[iIdx + 2];
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

          meshAsset = std::make_shared<MeshAsset>();
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

          m_meshStore.push_back(meshAsset);
          m_meshes[meshKey] = meshAsset;
          m_meshes[meshSrc] = meshAsset;
        }

        if (instantiate) {
          primEnt.set<Mundus::MeshAssetRef>({meshAsset});

          if (prim.material) {
            for (size_t mIdx = 0; mIdx < data->materials_count; ++mIdx) {
              if (&data->materials[mIdx] == prim.material) {
                primEnt.set<Mundus::MaterialRef>({materials[mIdx]});
                primEnt.set<Mundus::GltfDefaultMaterial>({materials[mIdx]});
                break;
              }
            }
          } else {
            primEnt.set<Mundus::MaterialRef>({defaultMat});
            primEnt.set<Mundus::GltfDefaultMaterial>({defaultMat});
          }
        }
      }
    }

    for (size_t i = 0; i < node->children_count; ++i) {
      processNode(node->children[i], e);
    }

    return e;
  };

  cgltf_scene *scenePtr = data->scene;
  if (!scenePtr && data->scenes_count > 0)
    scenePtr = &data->scenes[0];

  if (scenePtr) {
    flecs::entity root;
    for (size_t i = 0; i < scenePtr->nodes_count; ++i) {
      root = processNode(scenePtr->nodes[i],
                         parent ? ecs.entity(parent) : flecs::entity());
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

static std::string stripGLSLComments(const std::string &code) {
  std::string clean;
  clean.reserve(code.size());
  size_t i = 0;
  while (i < code.size()) {
    if (i + 1 < code.size() && code[i] == '/' && code[i + 1] == '/') {
      i += 2;
      while (i < code.size() && code[i] != '\n') i++;
    } else if (i + 1 < code.size() && code[i] == '/' && code[i + 1] == '*') {
      i += 2;
      while (i + 1 < code.size() && !(code[i] == '*' && code[i + 1] == '/')) i++;
      i += 2;
    } else {
      clean += code[i];
      i++;
    }
  }
  return clean;
}

static std::vector<std::pair<std::string, std::string>> parseGLSLUniforms(const std::string &code) {
  std::vector<std::pair<std::string, std::string>> uniforms;
  std::string clean = stripGLSLComments(code);
  size_t pos = 0;
  while (true) {
    pos = clean.find("uniform", pos);
    if (pos == std::string::npos) break;

    bool isWord = (pos == 0 || isspace(clean[pos - 1]) || clean[pos - 1] == ';');
    if (isWord) {
      size_t semi = clean.find(';', pos);
      if (semi != std::string::npos) {
        std::string decl = clean.substr(pos, semi - pos);
        if (decl.find('{') != std::string::npos) {
          pos = semi + 1;
          continue;
        }
        
        bool isLayout = false;
        if (pos > 0) {
          size_t prevSemi = clean.rfind(';', pos - 1);
          size_t layoutPos = clean.rfind("layout", pos - 1);
          if (layoutPos != std::string::npos && (prevSemi == std::string::npos || layoutPos > prevSemi)) {
            isLayout = true;
          }
        }
        if (isLayout) {
          pos = semi + 1;
          continue;
        }
        std::vector<std::string> tokens;
        std::string tok;
        for (char c : decl) {
          if (isspace(c)) {
            if (!tok.empty()) {
              tokens.push_back(tok);
              tok.clear();
            }
          } else {
            tok += c;
          }
        }
        if (!tok.empty()) tokens.push_back(tok);

        size_t uIdx = std::string::npos;
        for (size_t i = 0; i < tokens.size(); ++i) {
          if (tokens[i] == "uniform") {
            uIdx = i;
            break;
          }
        }

        if (uIdx != std::string::npos && uIdx + 2 < tokens.size()) {
          std::string type = tokens[uIdx + 1];
          std::string name = tokens[uIdx + 2];
          size_t bracket = name.find('[');
          if (bracket != std::string::npos) {
            name = name.substr(0, bracket);
          }

          if (name != "ubo" && name != "skybox" && name != "irradianceMap" &&
              name != "prefilterMap" && name != "brdfLUT" && name != "globalTextures" &&
              type != "GlobalUBO" && type != "samplerCube" &&
              name != "matParams" && name.compare(0, 14, "globalTextures") != 0) {
            uniforms.push_back({type, name});
          }
        }
        pos = semi + 1;
      } else {
        pos += 7;
      }
    } else {
      pos += 7;
    }
  }
  return uniforms;
}

static std::vector<Mundus::UniformMember> layoutMaterialParams(
    const std::vector<std::pair<std::string, std::string>> &uniforms, uint32_t &outTotalSize) {
  std::vector<Mundus::UniformMember> members;
  uint32_t offset = 0;
  for (auto &u : uniforms) {
    std::string type = u.first;
    std::string name = u.second;

    uint32_t size = 0;
    uint32_t align = 4;

    if (type == "float" || type == "int" || type == "uint" || type == "sampler2D") {
      size = 4;
      align = 4;
    } else if (type == "vec2") {
      size = 8;
      align = 8;
    } else if (type == "vec3" || type == "vec4") {
      size = 16;
      align = 16;
    } else if (type == "mat4") {
      size = 64;
      align = 16;
    }

    offset = (offset + align - 1) & ~(align - 1);
    members.push_back({name, type, offset, size});
    offset += size;
  }
  outTotalSize = (offset + 15) & ~15;
  return members;
}

static std::string cleanGLSLSource(const std::string &source, const std::vector<std::pair<std::string, std::string>> &customUniforms) {
  std::stringstream ss(source);
  std::string line;
  std::string clean;
  while (std::getline(ss, line)) {
    bool isCustomUniform = false;
    if (line.find("uniform") != std::string::npos) {
      for (auto &cu : customUniforms) {
        size_t nPos = line.find(cu.second);
        if (nPos != std::string::npos) {
          bool isWord = (nPos == 0 || isspace(line[nPos - 1]) || line[nPos - 1] == ';');
          if (isWord) {
            isCustomUniform = true;
            break;
          }
        }
      }
    }
    if (isCustomUniform) {
      clean += "// [Stripped dynamic uniform] " + line + "\n";
    } else {
      clean += line + "\n";
    }
  }
  return clean;
}

static bool writeStringToFile(const std::string &path, const std::string &content) {
  SDL_IOStream *stream = SDL_IOFromFile(path.c_str(), "w");
  if (!stream) {
    return false;
  }
  size_t written = SDL_WriteIO(stream, content.c_str(), content.size());
  SDL_CloseIO(stream);
  return written == content.size();
}

static VkCompareOp parseCompareOp(const char *str) {
  if (strcmp(str, "less") == 0) return VK_COMPARE_OP_LESS;
  if (strcmp(str, "less_or_equal") == 0) return VK_COMPARE_OP_LESS_OR_EQUAL;
  if (strcmp(str, "equal") == 0) return VK_COMPARE_OP_EQUAL;
  if (strcmp(str, "greater") == 0) return VK_COMPARE_OP_GREATER;
  if (strcmp(str, "greater_or_equal") == 0) return VK_COMPARE_OP_GREATER_OR_EQUAL;
  if (strcmp(str, "not_equal") == 0) return VK_COMPARE_OP_NOT_EQUAL;
  if (strcmp(str, "always") == 0) return VK_COMPARE_OP_ALWAYS;
  if (strcmp(str, "never") == 0) return VK_COMPARE_OP_NEVER;
  return VK_COMPARE_OP_LESS;
}

static std::string resolvePath(const std::string &path) {
  if (path.compare(0, 16, "@asset(assets://") == 0 && path.back() == ')') {
    return std::string(CONCORDIA_ASSETS_DIR) + "/" + path.substr(16, path.size() - 17);
  }
  if (path.compare(0, 9, "assets://") == 0) {
    return std::string(CONCORDIA_ASSETS_DIR) + "/" + path.substr(9);
  }
  return path;
}

std::shared_ptr<Mundus::ShaderAsset> AssetManager::loadShader(const std::string &path) {
  std::string resolved = resolvePath(path);
  auto it = m_shaders.find(resolved);
  if (it != m_shaders.end()) return it->second;

  SDL_Log("AssetManager: Loading shader manifest %s", resolved.c_str());

  Auxilia::ctoon_doc doc;
  if (!doc.load_file(resolved.c_str())) {
    throw std::runtime_error("AssetManager: Failed to load shader manifest: " + resolved);
  }

  std::string errors;
  if (!Mundus::Schema::validateShader(doc.get(), errors)) {
    throw std::runtime_error("AssetManager: Shader manifest validation failed for " + resolved + ":\n" + errors);
  }

  ctoon_value *root = doc.get();
  ctoon_value *shVal = ctoon_obj_get(root, "shader");

  auto shader = std::make_shared<Mundus::ShaderAsset>();

  ctoon_value *nameVal = ctoon_obj_get(shVal, "name");
  if (nameVal && nameVal->type == CTOON_STRING) {
    shader->name = nameVal->str_val;
  }

  ctoon_value *stages = ctoon_obj_get(shVal, "stages");
  ctoon_value *vertVal = ctoon_obj_get(stages, "vertex");
  if (vertVal && vertVal->type == CTOON_STRING) {
    shader->vertPath = resolvePath(vertVal->str_val);
  }
  ctoon_value *fragVal = ctoon_obj_get(stages, "fragment");
  if (fragVal && fragVal->type == CTOON_STRING) {
    shader->fragPath = resolvePath(fragVal->str_val);
  }
  ctoon_value *lightVal = ctoon_obj_get(stages, "light");
  if (lightVal && lightVal->type == CTOON_STRING) {
    shader->lightPath = resolvePath(lightVal->str_val);
  }

  ctoon_value *rs = ctoon_obj_get(shVal, "render_state");
  if (rs) {
    ctoon_value *dt = ctoon_obj_get(rs, "depth_test");
    if (dt && dt->type == CTOON_BOOL) shader->depthTest = dt->bool_val != 0;

    ctoon_value *dw = ctoon_obj_get(rs, "depth_write");
    if (dw && dw->type == CTOON_BOOL) shader->depthWrite = dw->bool_val != 0;

    ctoon_value *dc = ctoon_obj_get(rs, "depth_compare");
    if (dc && dc->type == CTOON_STRING) {
      shader->depthCompareOp = parseCompareOp(dc->str_val);
    }

    ctoon_value *cm = ctoon_obj_get(rs, "cull_mode");
    if (cm && cm->type == CTOON_STRING) {
      if (strcmp(cm->str_val, "none") == 0) shader->cullMode = VK_CULL_MODE_NONE;
      else if (strcmp(cm->str_val, "back") == 0) shader->cullMode = VK_CULL_MODE_BACK_BIT;
      else if (strcmp(cm->str_val, "front") == 0) shader->cullMode = VK_CULL_MODE_FRONT_BIT;
    }

    ctoon_value *ff = ctoon_obj_get(rs, "front_face");
    if (ff && ff->type == CTOON_STRING) {
      if (strcmp(ff->str_val, "ccw") == 0) shader->frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
      else if (strcmp(ff->str_val, "cw") == 0) shader->frontFace = VK_FRONT_FACE_CLOCKWISE;
    }

    ctoon_value *bm = ctoon_obj_get(rs, "blend_mode");
    if (bm && bm->type == CTOON_STRING) {
      shader->blendEnable = (strcmp(bm->str_val, "blend") == 0);
    }
  }

  ctoon_value *defaults = ctoon_obj_get(shVal, "defaults");
  if (defaults && defaults->type == CTOON_OBJECT) {
    for (size_t i = 0; i < defaults->len; ++i) {
      std::string key = defaults->pairs[i].key;
      ctoon_value *val = &defaults->pairs[i].val;
      std::string strVal;
      if (val->type == CTOON_NUMBER) {
        strVal = std::to_string(val->num_val);
      } else if (val->type == CTOON_BOOL) {
        strVal = val->bool_val ? "true" : "false";
      } else if (val->type == CTOON_STRING) {
        strVal = val->str_val;
      }
      if (!strVal.empty()) {
        shader->paramDefaults[key] = strVal;
      }
    }
  }

  size_t vsSize = 0;
  void *vsData = SDL_LoadFile(shader->vertPath.c_str(), &vsSize);
  if (!vsData) {
    throw std::runtime_error("AssetManager: Failed to load vertex shader source: " + shader->vertPath);
  }
  std::string vsCode((const char *)vsData, vsSize);
  SDL_free(vsData);

  size_t fsSize = 0;
  void *fsData = SDL_LoadFile(shader->fragPath.c_str(), &fsSize);
  if (!fsData) {
    throw std::runtime_error("AssetManager: Failed to load fragment shader source: " + shader->fragPath);
  }
  std::string fsCode((const char *)fsData, fsSize);
  SDL_free(fsData);

  std::string lightCode;
  if (!shader->lightPath.empty()) {
    size_t lsSize = 0;
    void *lsData = SDL_LoadFile(shader->lightPath.c_str(), &lsSize);
    if (!lsData) {
      throw std::runtime_error("AssetManager: Failed to load light shader source: " + shader->lightPath);
    }
    lightCode = std::string((const char *)lsData, lsSize);
    SDL_free(lsData);
  }

  auto vsUniforms = parseGLSLUniforms(vsCode);
  auto fsUniforms = parseGLSLUniforms(fsCode);
  auto lsUniforms = !lightCode.empty() ? parseGLSLUniforms(lightCode) : std::vector<std::pair<std::string, std::string>>();

  std::vector<std::pair<std::string, std::string>> combinedUniforms = vsUniforms;
  auto addUniqueUniforms = [&](const std::vector<std::pair<std::string, std::string>> &list) {
    for (auto &fu : list) {
      bool exists = false;
      for (auto &vu : combinedUniforms) {
        if (vu.second == fu.second) {
          exists = true;
          break;
        }
      }
      if (!exists) {
        combinedUniforms.push_back(fu);
      }
    }
  };
  addUniqueUniforms(fsUniforms);
  addUniqueUniforms(lsUniforms);

  uint32_t totalSize = 0;
  auto members = layoutMaterialParams(combinedUniforms, totalSize);
  shader->paramSize = totalSize;
  shader->paramMembers = members;

  std::string vsClean = cleanGLSLSource(vsCode, combinedUniforms);
  std::string fsClean = cleanGLSLSource(fsCode, combinedUniforms);
  std::string lsClean = !lightCode.empty() ? cleanGLSLSource(lightCode, combinedUniforms) : "";

  if (!members.empty()) {
    std::string paramsBlock = "\nlayout(std140, set = 2, binding = 0) uniform MaterialParams {\n";
    std::string macros;

    for (auto &m : members) {
      if (m.type == "sampler2D") {
        paramsBlock += "    uint " + m.name + "_idx;\n";
        macros += "#define " + m.name + " globalTextures[nonuniformEXT(" + m.name + "_idx)]\n";
      } else {
        paramsBlock += "    " + m.type + " " + m.name + ";\n";
      }
    }
    paramsBlock += "};\n\n";
    paramsBlock += macros + "\n";

    auto injectParams = [&](std::string &code) {
      size_t vPos = code.find("#version 450");
      if (vPos == std::string::npos) vPos = 0;
      else {
        vPos = code.find('\n', vPos);
        if (vPos == std::string::npos) vPos = 0;
        else vPos += 1;
      }
      code.insert(vPos, paramsBlock);
    };
    injectParams(vsClean);
    injectParams(fsClean);
  }

  if (!lsClean.empty()) {
    // Strip void main() from the light code if present (e.g. dummy stubs)
    size_t lmPos = lsClean.find("void main()");
    if (lmPos != std::string::npos) {
      size_t braceOpen = lsClean.find('{', lmPos);
      if (braceOpen != std::string::npos) {
        size_t braceClose = lsClean.find('}', braceOpen);
        if (braceClose != std::string::npos) {
          lsClean.erase(lmPos, braceClose - lmPos + 1);
        }
      }
    }

    // Prepend LIGHT_STAGE_OVERRIDE define to fsClean
    size_t vPos = fsClean.find("#version 450");
    if (vPos != std::string::npos) {
      vPos = fsClean.find('\n', vPos);
      if (vPos != std::string::npos) {
        fsClean.insert(vPos + 1, "#define LIGHT_STAGE_OVERRIDE 1\n");
      }
    } else {
      fsClean.insert(0, "#define LIGHT_STAGE_OVERRIDE 1\n");
    }

    // Inject cleaned light code into fragment shader right before void main()
    size_t mainPos = fsClean.find("void main()");
    if (mainPos != std::string::npos) {
      fsClean.insert(mainPos, lsClean + "\n\n");
    }
  }

  std::filesystem::path p(resolved);
  std::string base = p.stem().string();
  std::string genVertPath = std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/dynamic/" + base + "_generated_vert.glsl";
  std::string genFragPath = std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/dynamic/" + base + "_generated_frag.glsl";

  SDL_CreateDirectory((std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/dynamic").c_str());

  if (!writeStringToFile(genVertPath, vsClean)) {
    throw std::runtime_error("AssetManager: Failed to write generated vertex GLSL: " + genVertPath);
  }
  if (!writeStringToFile(genFragPath, fsClean)) {
    throw std::runtime_error("AssetManager: Failed to write generated fragment GLSL: " + genFragPath);
  }

  shader->vertPath = genVertPath;
  shader->fragPath = genFragPath;

  m_shaders[resolved] = shader;
  return shader;
}

} // namespace Memoria
