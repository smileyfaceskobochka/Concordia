#include "asset_manager.h"
#include <stdexcept>
#define GLM_ENABLE_EXPERIMENTAL
#include <SDL3/SDL.h>
#include <stb_image.h>
#define CGLTF_IMPLEMENTATION
#include "../forma/material.h"
#include "../mundus/scene.h"
#include <cgltf.h>
#include <filesystem>
#include <functional>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <iostream>

namespace Memoria {

AssetManager::AssetManager(Allocator &allocator, VkDevice device,
                           VkQueue transferQueue, VkCommandPool transferPool)
    : m_allocator(allocator), m_device(device), m_transferQueue(transferQueue),
      m_transferPool(transferPool) {

  m_defaultWhite = createSolidTexture(255, 255, 255, 255, true);
  m_defaultBlack = createSolidTexture(0, 0, 0, 255, true);
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
  for (auto &pair : m_meshes) {
    pair.second->destroy(m_allocator);
  }
  for (auto &pair : m_textures) {
    pair.second->destroy(m_allocator, m_device);
  }
}

std::shared_ptr<MeshAsset> AssetManager::loadMesh(const std::string &path,
                                                  bool createCubeFallback) {
  if (m_meshes.find(path) != m_meshes.end()) {
    return m_meshes[path];
  }

  std::vector<Forma::Vertex> vertices;
  std::vector<uint32_t> indices;

  SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
              "AssetManager: Requesting load of mesh from %s", path.c_str());

  bool success = false;
  try {
    Forma::Mesh::loadFromOBJ(path, vertices, indices);
    success = true;
  } catch (...) {
    if (!createCubeFallback) {
      throw std::runtime_error("AssetManager failed to load mesh: " + path);
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "AssetManager: OBJ load failed or file not found, creating "
                "procedural cube for %s",
                path.c_str());
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
    m_allocator.createBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer,
                             stagingAlloc);

    void *data;
    vmaMapMemory(m_allocator.getVma(), stagingAlloc, &data);
    memcpy(data, vertices.data(), vSize);
    vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);

    m_allocator.createBuffer(
        vSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, mesh->vertexBuffer, mesh->vertexAllocation);
    m_allocator.copyBuffer(stagingBuffer, mesh->vertexBuffer, vSize,
                           m_transferQueue, m_transferPool, m_device);

    m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);
  }

  // Upload Index Buffer
  {
    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    m_allocator.createBuffer(iSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer,
                             stagingAlloc);

    void *data;
    vmaMapMemory(m_allocator.getVma(), stagingAlloc, &data);
    memcpy(data, indices.data(), iSize);
    vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);

    m_allocator.createBuffer(
        iSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, mesh->indexBuffer, mesh->indexAllocation);
    m_allocator.copyBuffer(stagingBuffer, mesh->indexBuffer, iSize,
                           m_transferQueue, m_transferPool, m_device);

    m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);
  }

  m_meshes[path] = mesh;
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
  VkBuffer stagingBuffer;
  VmaAllocation stagingAlloc;
  m_allocator.createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                           VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer,
                           stagingAlloc);

  void *data;
  vmaMapMemory(m_allocator.getVma(), stagingAlloc, &data);
  memcpy(data, pixels, static_cast<size_t>(imageSize));
  vmaUnmapMemory(m_allocator.getVma(), stagingAlloc);

  auto texture = std::make_shared<TextureAsset>();
  m_allocator.createImage(
      width, height, format, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY, texture->image, texture->allocation);

  m_allocator.transitionImageLayout(texture->image, format,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    m_transferQueue, m_transferPool, m_device);
  m_allocator.copyBufferToImage(stagingBuffer, texture->image, width, height,
                                m_transferQueue, m_transferPool, m_device);
  m_allocator.transitionImageLayout(texture->image, format,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    m_transferQueue, m_transferPool, m_device);

  m_allocator.destroyBuffer(stagingBuffer, stagingAlloc);

  VkImageViewCreateInfo viewInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = texture->image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(m_device, &viewInfo, nullptr, &texture->view) !=
      VK_SUCCESS) {
    throw std::runtime_error(
        "AssetManager: Failed to create texture image view");
  }

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
                           VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer,
                           stagingAlloc);

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
                           VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer,
                           stagingAlloc);

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
        unsigned char *data = (unsigned char *)img->buffer_view->buffer->data +
                              img->buffer_view->offset;
        size_t size = img->buffer_view->size;
        std::string name =
            img->name ? img->name
                      : "embedded_" +
                            std::to_string(reinterpret_cast<uintptr_t>(img));
        return loadTextureFromMemory(data, size, name, srgb);
      }
      return nullptr;
    };

    auto mat = std::make_shared<Forma::Material>();
    mat->shaderName = "pbr";

    if (gmat.has_pbr_metallic_roughness) {
      cgltf_pbr_metallic_roughness &pbr = gmat.pbr_metallic_roughness;
      mat->baseColor = glm::make_vec4(pbr.base_color_factor);
      mat->metallic = pbr.metallic_factor;
      mat->roughness = pbr.roughness_factor;

      mat->albedo = loadGLTFTexture(pbr.base_color_texture, true);
      mat->metallicRoughness =
          loadGLTFTexture(pbr.metallic_roughness_texture, false);
    }

    mat->normal = loadGLTFTexture(gmat.normal_texture, false);
    mat->ao = loadGLTFTexture(gmat.occlusion_texture, false);
    mat->emissive = loadGLTFTexture(gmat.emissive_texture, true);

    // Fallbacks for missing textures
    if (!mat->albedo) {
      mat->albedo = getDefaultWhite();
      SDL_Log("GLTF: Mat %zu: Albedo MISSING (fallback)", i);
    } else
      SDL_Log("GLTF: Mat %zu: Albedo LOADED", i);

    if (!mat->metallicRoughness) {
      mat->metallicRoughness = getDefaultWhite();
      SDL_Log("GLTF: Mat %zu: MR MISSING (fallback)", i);
    } else
      SDL_Log("GLTF: Mat %zu: MR LOADED", i);

    if (!mat->normal)
      mat->normal = getDefaultNormal();
    if (!mat->ao)
      mat->ao = getDefaultWhite();
    if (!mat->emissive)
      mat->emissive = getDefaultBlack();

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
        ent.transform.position = glm::vec3(m[3]);
        ent.transform.scale =
            glm::vec3(glm::length(m[0]), glm::length(m[1]), glm::length(m[2]));
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
        }

        // Fetch reference again at the latest possible time
        auto &primEnt = scene.getEntities()[primIdx];

        // Load Mesh Data
        std::vector<Forma::Vertex> vertices;
        std::vector<uint32_t> indices;

        float *pos = nullptr;
        size_t posCount = 0;
        float *norm = nullptr;
        float *uv = nullptr;
        float *tangent = nullptr;

        for (size_t a = 0; a < prim.attributes_count; ++a) {
          cgltf_attribute &attr = prim.attributes[a];
          if (attr.type == cgltf_attribute_type_position) {
            pos = (float *)((char *)attr.data->buffer_view->buffer->data +
                            attr.data->buffer_view->offset + attr.data->offset);
            posCount = attr.data->count;
          } else if (attr.type == cgltf_attribute_type_normal) {
            norm =
                (float *)((char *)attr.data->buffer_view->buffer->data +
                          attr.data->buffer_view->offset + attr.data->offset);
          } else if (attr.type == cgltf_attribute_type_texcoord) {
            uv = (float *)((char *)attr.data->buffer_view->buffer->data +
                           attr.data->buffer_view->offset + attr.data->offset);
          } else if (attr.type == cgltf_attribute_type_tangent) {
            tangent =
                (float *)((char *)attr.data->buffer_view->buffer->data +
                          attr.data->buffer_view->offset + attr.data->offset);
          }
        }

        if (posCount > 0) {
          vertices.resize(posCount);
          for (size_t v = 0; v < posCount; ++v) {
            vertices[v].pos = {pos[v * 3], pos[v * 3 + 1], pos[v * 3 + 2]};
            if (norm)
              vertices[v].normal = {norm[v * 3], norm[v * 3 + 1],
                                    norm[v * 3 + 2]};
            if (uv)
              vertices[v].texCoord = {uv[v * 2], uv[v * 2 + 1]};
            if (tangent) {
              vertices[v].tangent =
                  glm::vec4(tangent[v * 4 + 0], tangent[v * 4 + 1],
                            tangent[v * 4 + 2], tangent[v * 4 + 3]);
            } else {
              vertices[v].tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
            }
          }
        }

        if (prim.indices) {
          indices.resize(prim.indices->count);
          for (size_t k = 0; k < prim.indices->count; ++k) {
            indices[k] = (uint32_t)cgltf_accessor_read_index(prim.indices, k);
          }
        }

        auto meshAsset = std::make_shared<MeshAsset>();
        meshAsset->vertexCount = static_cast<uint32_t>(vertices.size());
        meshAsset->indexCount = static_cast<uint32_t>(indices.size());

        {
          size_t vSize = vertices.size() * sizeof(Forma::Vertex);
          VkBuffer stagingBuffer;
          VmaAllocation stagingAlloc;
          m_allocator.createBuffer(vSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer,
                                   stagingAlloc);
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
                                     VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer,
                                     stagingAlloc);
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

} // namespace Memoria
