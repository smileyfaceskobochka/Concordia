#include "ibl_generator.h"
#include "memoria/allocator.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <stb_image.h>
#include <vector>
#include <thread>

namespace Lumen {

// ----------------------------------------------------------------
//  Math helpers
// ----------------------------------------------------------------

static constexpr float PI = 3.14159265359f;

static float radicalInverseVdC(uint32_t bits) {
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10f;
}

static glm::vec2 hammersley(uint32_t i, uint32_t N) {
  return glm::vec2(float(i) / float(N), radicalInverseVdC(i));
}

static glm::vec3 importanceSampleGGX(glm::vec2 xi, float roughness) {
  float a = roughness * roughness;
  float phi = 2.0f * PI * xi.x;
  float cosTheta =
      sqrt((1.0f - xi.y) / (1.0f + (a * a - 1.0f) * xi.y));
  float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
  return glm::vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

static float DistributionGGX(glm::vec3 N, glm::vec3 H, float r) {
  float a = r * r;
  float a2 = a * a;
  float NdotH = std::max(dot(N, H), 0.0f);
  float d = (NdotH * NdotH) * (a2 - 1.0f) + 1.0f;
  return a2 / std::max(PI * d * d, 1e-7f);
}

static float GeometrySchlickGGX(float NdotV, float r) {
  float a = r * r;
  float k = ((a + 1.0f) * (a + 1.0f)) / 8.0f;
  return NdotV / (NdotV * (1.0f - k) + k);
}

static float GeometrySmith(glm::vec3 N, glm::vec3 V, glm::vec3 L, float r) {
  return GeometrySchlickGGX(std::max(dot(N, V), 0.0f), r) *
         GeometrySchlickGGX(std::max(dot(N, L), 0.0f), r);
}

static glm::vec3 tangentToWorld(glm::vec3 sample, glm::vec3 N) {
  glm::vec3 up = std::abs(N.z) < 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f)
                                         : glm::vec3(1.0f, 0.0f, 0.0f);
  glm::vec3 T = glm::normalize(glm::cross(up, N));
  glm::vec3 B = glm::cross(N, T);
  return glm::normalize(sample.x * T + sample.y * B + sample.z * N);
}

// ----------------------------------------------------------------
//  Cubemap direction lookup (Vulkan convention)
// ----------------------------------------------------------------

static glm::vec3 cubeFaceDir(int face, float s, float t) {
  float u = 2.0f * s - 1.0f;
  float v = 2.0f * t - 1.0f;
  switch (face) {
  case 0:
    return glm::normalize(glm::vec3(1.0f, -v, -u)); // +X
  case 1:
    return glm::normalize(glm::vec3(-1.0f, -v, u)); // -X
  case 2:
    return glm::normalize(glm::vec3(u, 1.0f, v));   // +Y
  case 3:
    return glm::normalize(glm::vec3(u, -1.0f, -v)); // -Y
  case 4:
    return glm::normalize(glm::vec3(u, -v, 1.0f));  // +Z
  case 5:
    return glm::normalize(glm::vec3(-u, -v, -1.0f));// -Z
  }
  return glm::vec3(0.0f);
}

// ----------------------------------------------------------------
//  Bilinear cubemap sampling from 6 face arrays (float, linear)
// ----------------------------------------------------------------

static void dirToFaceUV(glm::vec3 dir, int &face, float &u, float &v) {
  float absX = std::abs(dir.x), absY = std::abs(dir.y), absZ = std::abs(dir.z);
  float maxCoord;
  if (absX >= absY && absX >= absZ) {
    maxCoord = absX;
    if (dir.x > 0.0f) {
      face = 0; // +X
      u = (-dir.z / maxCoord + 1.0f) * 0.5f;
      v = (-dir.y / maxCoord + 1.0f) * 0.5f;
    } else {
      face = 1; // -X
      u = (dir.z / maxCoord + 1.0f) * 0.5f;
      v = (-dir.y / maxCoord + 1.0f) * 0.5f;
    }
  } else if (absY >= absZ) {
    maxCoord = absY;
    if (dir.y > 0.0f) {
      face = 2; // +Y
      u = (dir.x / maxCoord + 1.0f) * 0.5f;
      v = (dir.z / maxCoord + 1.0f) * 0.5f;
    } else {
      face = 3; // -Y
      u = (dir.x / maxCoord + 1.0f) * 0.5f;
      v = (-dir.z / maxCoord + 1.0f) * 0.5f;
    }
  } else {
    maxCoord = absZ;
    if (dir.z > 0.0f) {
      face = 4; // +Z
      u = (dir.x / maxCoord + 1.0f) * 0.5f;
      v = (-dir.y / maxCoord + 1.0f) * 0.5f;
    } else {
      face = 5; // -Z
      u = (-dir.x / maxCoord + 1.0f) * 0.5f;
      v = (-dir.y / maxCoord + 1.0f) * 0.5f;
    }
  }
}

static glm::vec3 sampleCubemapBilinear(
    const std::vector<std::vector<float>> &faces, int faceSize,
    glm::vec3 dir) {
  int face;
  float u, v;
  dirToFaceUV(dir, face, u, v);

  // Clamp
  u = std::min(std::max(u, 0.0f), 1.0f);
  v = std::min(std::max(v, 0.0f), 1.0f);

  float x = u * (faceSize - 1);
  float y = v * (faceSize - 1);
  int ix = (int)x;
  int iy = (int)y;
  float fx = x - ix;
  float fy = y - iy;
  int ix1 = std::min(ix + 1, faceSize - 1);
  int iy1 = std::min(iy + 1, faceSize - 1);

  int stride = 3;
  const float *f = faces[face].data();
  auto pix = [&](int px, int py) -> glm::vec3 {
    int idx = (py * faceSize + px) * stride;
    return glm::vec3(f[idx], f[idx + 1], f[idx + 2]);
  };

  glm::vec3 c00 = pix(ix, iy);
  glm::vec3 c10 = pix(ix1, iy);
  glm::vec3 c01 = pix(ix, iy1);
  glm::vec3 c11 = pix(ix1, iy1);

  return glm::mix(glm::mix(c00, c10, fx), glm::mix(c01, c11, fx), fy);
}

// ----------------------------------------------------------------
//  sRGB <-> Linear
// ----------------------------------------------------------------

static float srgbToLinear(float c) {
  if (c <= 0.04045f)
    return c / 12.92f;
  return std::pow((c + 0.055f) / 1.055f, 2.4f);
}

static float linearToSrgb(float c) {
  if (c <= 0.0031308f)
    return c * 12.92f;
  return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// ----------------------------------------------------------------
//  Extract 6 face planes from cross cubemap image
// ----------------------------------------------------------------

static void extractCrossFaces(const unsigned char *src, int width, int height,
                              std::vector<std::vector<float>> &outFaces,
                              int &outFaceSize) {
  uint32_t faceSize = width / 4;
  if (height / 3 != (int)faceSize) {
    if (width / 3 == height / 4) {
      faceSize = width / 3;
    } else {
      SDL_Log("IBLGenerator: invalid cross dimensions %dx%d", width, height);
      faceSize = std::min(width, height) / 3;
    }
  }
  outFaceSize = faceSize;

  uint32_t cols = width / faceSize;
  uint32_t rows = height / faceSize;

  struct FacePos {
    int x, y;
  };
  FacePos coords[6];
  coords[0] = {2, 1}; // +X
  coords[1] = {0, 1}; // -X
  coords[2] = {1, 0}; // +Y
  coords[3] = {1, 2}; // -Y
  coords[4] = {1, 1}; // +Z
  if (cols >= 4)
    coords[5] = {3, 1}; // -Z
  else
    coords[5] = {1, 3}; // -Z (3x4 layout)

  outFaces.resize(6);
  for (int f = 0; f < 6; ++f) {
    outFaces[f].resize(faceSize * faceSize * 3);
    int fx = coords[f].x * faceSize;
    int fy = coords[f].y * faceSize;
    for (uint32_t y = 0; y < faceSize; ++y) {
      for (uint32_t x = 0; x < faceSize; ++x) {
        const unsigned char *pixel =
            src + ((fy + y) * width + (fx + x)) * 4;
        int idx = (y * faceSize + x) * 3;
        outFaces[f][idx + 0] = srgbToLinear(pixel[0] / 255.0f);
        outFaces[f][idx + 1] = srgbToLinear(pixel[1] / 255.0f);
        outFaces[f][idx + 2] = srgbToLinear(pixel[2] / 255.0f);
      }
    }
  }
}

// ----------------------------------------------------------------
//  Irradiance convolution (cosine-weighted hemisphere)
// ----------------------------------------------------------------

static constexpr int IRRADIANCE_SAMPLES = 256;
static constexpr int IRRADIANCE_FACE_SIZE = 32;

static void generateIrradiance(const std::vector<std::vector<float>> &srcFaces,
                               int srcFaceSize,
                               std::vector<std::vector<float>> &outFaces) {
  outFaces.resize(6);
  for (int f = 0; f < 6; ++f)
    outFaces[f].resize(IRRADIANCE_FACE_SIZE * IRRADIANCE_FACE_SIZE * 3, 0.0f);

  std::vector<std::thread> threads;
  for (int f = 0; f < 6; ++f) {
    threads.push_back(std::thread([&, f]() {
      for (int y = 0; y < IRRADIANCE_FACE_SIZE; ++y) {
        for (int x = 0; x < IRRADIANCE_FACE_SIZE; ++x) {
          float s = (x + 0.5f) / IRRADIANCE_FACE_SIZE;
          float t = (y + 0.5f) / IRRADIANCE_FACE_SIZE;
          glm::vec3 N = cubeFaceDir(f, s, t);

          glm::vec3 acc(0.0f);
          for (uint32_t i = 0; i < IRRADIANCE_SAMPLES; ++i) {
            glm::vec2 xi = hammersley(i, IRRADIANCE_SAMPLES);
            float theta = acos(sqrt(1.0f - xi.x));
            float phi = 2.0f * PI * xi.y;
            glm::vec3 localDir(
                sin(theta) * cos(phi),
                sin(theta) * sin(phi),
                cos(theta));
            glm::vec3 worldDir = tangentToWorld(localDir, N);
            acc += sampleCubemapBilinear(srcFaces, srcFaceSize, worldDir);
          }
          acc /= (float)IRRADIANCE_SAMPLES;

          int idx = (y * IRRADIANCE_FACE_SIZE + x) * 3;
          outFaces[f][idx + 0] = acc.x;
          outFaces[f][idx + 1] = acc.y;
          outFaces[f][idx + 2] = acc.z;
        }
      }
    }));
  }
  for (auto &t : threads) {
    t.join();
  }
}

// ----------------------------------------------------------------
//  Prefiltered environment map (GGX importance sampling)
// ----------------------------------------------------------------

static constexpr int PREFILTER_FACE_SIZE = 128;
static constexpr int PREFILTER_MIP_LEVELS = 5;

static int sampleCountForMip(int mip) {
  return mip == 0 ? 128 : 64 + mip * 32;
}

static void generatePrefilter(const std::vector<std::vector<float>> &srcFaces,
                              int srcFaceSize,
                              std::vector<std::vector<float>> &outFaces,
                              int mipLevel, int numMips) {
  int faceSize = PREFILTER_FACE_SIZE >> mipLevel;
  if (faceSize < 1)
    faceSize = 1;

  outFaces.resize(6);
  for (int f = 0; f < 6; ++f)
    outFaces[f].resize(faceSize * faceSize * 3, 0.0f);

  float roughness = (float)mipLevel / (float)(numMips - 1);
  int numSamples = sampleCountForMip(mipLevel);

  std::vector<std::thread> threads;
  for (int f = 0; f < 6; ++f) {
    threads.push_back(std::thread([&, f]() {
      for (int y = 0; y < faceSize; ++y) {
        for (int x = 0; x < faceSize; ++x) {
          float s = (x + 0.5f) / faceSize;
          float t = (y + 0.5f) / faceSize;
          glm::vec3 N = cubeFaceDir(f, s, t);
          glm::vec3 R = N;
          glm::vec3 V = R;

          glm::vec3 acc(0.0f);
          float totalWeight = 0.0f;

          for (uint32_t i = 0; i < (uint32_t)numSamples; ++i) {
            glm::vec2 xi = hammersley(i, numSamples);
            glm::vec3 H = importanceSampleGGX(xi, roughness);
            glm::vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);

            float NdotL = std::max(dot(N, L), 0.0f);
            if (NdotL > 0.0f) {
              acc += sampleCubemapBilinear(srcFaces, srcFaceSize, L) * NdotL;
              totalWeight += NdotL;
            }
          }

          if (totalWeight > 0.0f)
            acc /= totalWeight;

          int idx = (y * faceSize + x) * 3;
          outFaces[f][idx + 0] = acc.x;
          outFaces[f][idx + 1] = acc.y;
          outFaces[f][idx + 2] = acc.z;
        }
      }
    }));
  }
  for (auto &t : threads) {
    t.join();
  }
}

// ----------------------------------------------------------------
//  BRDF LUT
// ----------------------------------------------------------------

static constexpr int BRDF_LUT_SIZE = 256;
static constexpr int BRDF_SAMPLES = 512;

static void generateBRDF_LUT(std::vector<unsigned char> &data) {
  data.resize(BRDF_LUT_SIZE * BRDF_LUT_SIZE * 4, 0);

  unsigned int numThreads = std::thread::hardware_concurrency();
  if (numThreads == 0) numThreads = 4;
  std::vector<std::thread> threads;
  int rowsPerThread = BRDF_LUT_SIZE / numThreads;

  for (unsigned int t = 0; t < numThreads; ++t) {
    int startY = t * rowsPerThread;
    int endY = (t == numThreads - 1) ? BRDF_LUT_SIZE : (t + 1) * rowsPerThread;
    threads.push_back(std::thread([&, startY, endY]() {
      for (int y = startY; y < endY; ++y) {
        float NdotV = (y + 0.5f) / BRDF_LUT_SIZE;
        for (int x = 0; x < BRDF_LUT_SIZE; ++x) {
          float roughness = (x + 0.5f) / BRDF_LUT_SIZE;

          glm::vec3 V(sqrt(1.0f - NdotV * NdotV), 0.0f, NdotV);
          glm::vec3 N(0.0f, 0.0f, 1.0f);

          float scale = 0.0f, bias = 0.0f;

          for (uint32_t i = 0; i < BRDF_SAMPLES; ++i) {
            glm::vec2 xi = hammersley(i, BRDF_SAMPLES);
            glm::vec3 H = importanceSampleGGX(xi, roughness);
            glm::vec3 L = glm::normalize(2.0f * glm::dot(V, H) * H - V);

            float NdotL = std::max(L.z, 0.0f);
            float NdotH = std::max(H.z, 0.0f);
            float VdotH = std::max(glm::dot(V, H), 0.0f);

            if (NdotL > 0.0f) {
              float G = GeometrySmith(N, V, L, roughness);
              float G_Vis = G * VdotH / (NdotH * NdotV);
              float Fc = std::pow(1.0f - VdotH, 5.0f);

              scale += (1.0f - Fc) * G_Vis;
              bias += Fc * G_Vis;
            }
          }
          scale /= BRDF_SAMPLES;
          bias /= BRDF_SAMPLES;

          int idx = (y * BRDF_LUT_SIZE + x) * 4;
          data[idx + 0] = (unsigned char)(std::min(std::max(scale, 0.0f), 1.0f) * 255.0f);
          data[idx + 1] = (unsigned char)(std::min(std::max(bias, 0.0f), 1.0f) * 255.0f);
          data[idx + 2] = 0;
          data[idx + 3] = 255;
        }
      }
    }));
  }
  for (auto &t : threads) {
    t.join();
  }
}

// ----------------------------------------------------------------
//  GPU upload helpers
// ----------------------------------------------------------------

static std::shared_ptr<Memoria::TextureAsset>
uploadCubemap(Memoria::Allocator &allocator, VkDevice device,
              VkQueue transferQueue, VkCommandPool transferPool,
              const std::vector<std::vector<float>> &faces, int faceSize,
              int mipLevels, VkFormat format) {
  VkDeviceSize faceByteSize = faceSize * faceSize * 4;
  VkDeviceSize totalSize = faceByteSize * 6;

  // Convert float linear data to uint8 (accounting for mip output format)
  // The input faces are always float linear; output may be SRGB or UNORM
  bool isSRGB = (format == VK_FORMAT_R8G8B8A8_SRGB);
  std::vector<unsigned char> pixels(totalSize);
  for (int f = 0; f < 6; ++f) {
    for (int y = 0; y < faceSize; ++y) {
      for (int x = 0; x < faceSize; ++x) {
        int srcIdx = (y * faceSize + x) * 3;
        int dstIdx = (y * faceSize + x) * 4 + f * faceByteSize;
        float r = faces[f][srcIdx + 0];
        float g = faces[f][srcIdx + 1];
        float b = faces[f][srcIdx + 2];
        if (isSRGB) {
          pixels[dstIdx + 0] = (unsigned char)(std::min(std::max(linearToSrgb(r), 0.0f), 1.0f) * 255.0f);
          pixels[dstIdx + 1] = (unsigned char)(std::min(std::max(linearToSrgb(g), 0.0f), 1.0f) * 255.0f);
          pixels[dstIdx + 2] = (unsigned char)(std::min(std::max(linearToSrgb(b), 0.0f), 1.0f) * 255.0f);
        } else {
          pixels[dstIdx + 0] = (unsigned char)(std::min(std::max(r, 0.0f), 1.0f) * 255.0f);
          pixels[dstIdx + 1] = (unsigned char)(std::min(std::max(g, 0.0f), 1.0f) * 255.0f);
          pixels[dstIdx + 2] = (unsigned char)(std::min(std::max(b, 0.0f), 1.0f) * 255.0f);
        }
        pixels[dstIdx + 3] = 255;
      }
    }
  }

  // Staging buffer
  VkBuffer stagingBuffer;
  VmaAllocation stagingAlloc;
  allocator.createBuffer(totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VMA_MEMORY_USAGE_AUTO, stagingBuffer, stagingAlloc,
                         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
  void *mapped;
  vmaMapMemory(allocator.getVma(), stagingAlloc, &mapped);
  memcpy(mapped, pixels.data(), (size_t)totalSize);
  vmaUnmapMemory(allocator.getVma(), stagingAlloc);

  // Create cubemap image
  auto cubemap = std::make_shared<Memoria::TextureAsset>();
  allocator.createImage(
      faceSize, faceSize, format, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
          VK_IMAGE_USAGE_SAMPLED_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY, cubemap->image, cubemap->allocation, 6,
      VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, mipLevels);

  // Transition to transfer dst
  allocator.transitionImageLayout(
      cubemap->image, format, VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, transferQueue, transferPool, device,
      6, mipLevels);

  // Copy base level
  allocator.copyBufferToImage(stagingBuffer, cubemap->image, faceSize, faceSize,
                              transferQueue, transferPool, device, 6, 0);

  // Generate mipmaps
  allocator.generateMipmaps(cubemap->image, format, faceSize, faceSize,
                            mipLevels, transferQueue, transferPool, device, 6);

  allocator.destroyBuffer(stagingBuffer, stagingAlloc);

  // Create image view
  VkImageViewCreateInfo viewInfo = {
      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = cubemap->image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 6;
  vkCreateImageView(device, &viewInfo, nullptr, &cubemap->view);

  cubemap->width = faceSize;
  cubemap->height = faceSize;
  cubemap->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  cubemap->textureId = 0; // Will be set by the caller
  return cubemap;
}

static std::shared_ptr<Memoria::TextureAsset>
upload2DTexture(Memoria::Allocator &allocator, VkDevice device,
                VkQueue transferQueue, VkCommandPool transferPool,
                const unsigned char *pixels, int width, int height,
                VkFormat format) {
  VkDeviceSize size = width * height * 4;
  uint32_t mipLevels = 1;

  VkBuffer stagingBuffer;
  VmaAllocation stagingAlloc;
  allocator.createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VMA_MEMORY_USAGE_AUTO, stagingBuffer, stagingAlloc,
                         VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
  void *mapped;
  vmaMapMemory(allocator.getVma(), stagingAlloc, &mapped);
  memcpy(mapped, pixels, (size_t)size);
  vmaUnmapMemory(allocator.getVma(), stagingAlloc);

  auto tex = std::make_shared<Memoria::TextureAsset>();
  allocator.createImage(
      width, height, format, VK_IMAGE_TILING_OPTIMAL,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VMA_MEMORY_USAGE_GPU_ONLY, tex->image, tex->allocation, 1, 0, mipLevels);

  allocator.transitionImageLayout(tex->image, format,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  transferQueue, transferPool, device);
  allocator.copyBufferToImage(stagingBuffer, tex->image, width, height,
                              transferQueue, transferPool, device);
  allocator.transitionImageLayout(tex->image, format,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                  transferQueue, transferPool, device);

  allocator.destroyBuffer(stagingBuffer, stagingAlloc);

  VkImageViewCreateInfo viewInfo = {
      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = tex->image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;
  vkCreateImageView(device, &viewInfo, nullptr, &tex->view);

  tex->width = width;
  tex->height = height;
  tex->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  tex->textureId = 0;
  return tex;
}

// ----------------------------------------------------------------
//  Equirectangular HDR to 6 cubemap faces
// ----------------------------------------------------------------

static void equirectToFaces(const float *hdrPixels, int w, int h,
                            std::vector<std::vector<float>> &outFaces,
                            int faceSize) {
  outFaces.resize(6);
  for (int f = 0; f < 6; ++f)
    outFaces[f].resize(faceSize * faceSize * 3, 0.0f);

  struct FaceDir { glm::vec3 u, v, w; };
  const FaceDir faceDirs[6] = {
    {{0, 0, -1}, {0, -1, 0}, {1, 0, 0}},   // +X RIGHT
    {{0, 0, 1},  {0, -1, 0}, {-1, 0, 0}},  // -X LEFT
    {{1, 0, 0},  {0, 0, 1},  {0, 1, 0}},   // +Y TOP
    {{1, 0, 0},  {0, 0, -1}, {0, -1, 0}},  // -Y BOTTOM
    {{1, 0, 0},  {0, -1, 0}, {0, 0, 1}},   // +Z FRONT
    {{-1, 0, 0}, {0, -1, 0}, {0, 0, -1}},  // -Z BACK
  };

  for (int face = 0; face < 6; ++face) {
    float *faceDst = outFaces[face].data();
    for (int y = 0; y < faceSize; ++y) {
      for (int x = 0; x < faceSize; ++x) {
        float u = (static_cast<float>(x) + 0.5f) / faceSize * 2.0f - 1.0f;
        float v = (static_cast<float>(y) + 0.5f) / faceSize * 2.0f - 1.0f;
        glm::vec3 dir = glm::normalize(
            faceDirs[face].u * u + faceDirs[face].v * v + faceDirs[face].w);

        float theta = std::acos(std::clamp(dir.y, -1.0f, 1.0f));
        float phi = std::atan2(dir.z, dir.x);
        float eqU = (phi / (2.0f * PI) + 0.5f) * w;
        float eqV = (theta / PI) * h;

        auto sample = [&](float sx, float sy) -> glm::vec3 {
          int px = std::min(static_cast<int>(sx), w - 1);
          int py = std::min(static_cast<int>(sy), h - 1);
          const float *p = &hdrPixels[(py * w + px) * 4];
          return {p[0], p[1], p[2]};
        };

        float ix = std::floor(eqU), iy = std::floor(eqV);
        float fx = eqU - ix, fy = eqV - iy;
        glm::vec3 c00 = sample(ix, iy);
        glm::vec3 c10 = sample(ix + 1, iy);
        glm::vec3 c01 = sample(ix, iy + 1);
        glm::vec3 c11 = sample(ix + 1, iy + 1);
        glm::vec3 col = glm::mix(glm::mix(c00, c10, fx), glm::mix(c01, c11, fx), fy);

        size_t idx = (y * faceSize + x) * 3;
        faceDst[idx + 0] = col.r;
        faceDst[idx + 1] = col.g;
        faceDst[idx + 2] = col.b;
      }
    }
  }
}

// ----------------------------------------------------------------
//  Public API - Generate from pre-extracted face data
// ----------------------------------------------------------------

IBLMaps IBLGenerator::generateFromFaces(
    Memoria::Allocator &allocator, VkDevice device, VkQueue transferQueue,
    VkCommandPool transferPool,
    const std::vector<std::vector<float>> &srcFaces, int srcFaceSize) {
  SDL_Log("IBLGenerator: Generating IBL maps from face data (%dx%d)...",
          srcFaceSize, srcFaceSize);
  IBLMaps result;

  // Step 1: Generate irradiance map
  SDL_Log("IBLGenerator: Generating irradiance map (%dx%d)...",
          IRRADIANCE_FACE_SIZE, IRRADIANCE_FACE_SIZE);
  std::vector<std::vector<float>> irradianceFaces;
  generateIrradiance(srcFaces, srcFaceSize, irradianceFaces);

  int irrMips = (int)(std::floor(std::log2(IRRADIANCE_FACE_SIZE))) + 1;
  result.irradianceMap =
      uploadCubemap(allocator, device, transferQueue, transferPool,
                    irradianceFaces, IRRADIANCE_FACE_SIZE, irrMips,
                    VK_FORMAT_R8G8B8A8_UNORM);

  // Step 4: Generate prefiltered map (with mips)
  SDL_Log("IBLGenerator: Generating prefiltered map (%dx%d, %d mips)...",
          PREFILTER_FACE_SIZE, PREFILTER_FACE_SIZE, PREFILTER_MIP_LEVELS);

  // Build all mip levels into a single face array per level
  int prefilterMips = PREFILTER_MIP_LEVELS;
  std::vector<std::vector<float>> fullPrefiltered[PREFILTER_MIP_LEVELS];

  for (int mip = 0; mip < PREFILTER_MIP_LEVELS; ++mip) {
    generatePrefilter(srcFaces, srcFaceSize, fullPrefiltered[mip], mip,
                      PREFILTER_MIP_LEVELS);
  }

  // Upload full prefiltered cubemap with per-mip content
  {
    VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
    int baseSize = PREFILTER_FACE_SIZE;

    auto cubemap = std::make_shared<Memoria::TextureAsset>();
    allocator.createImage(
        baseSize, baseSize, fmt, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, cubemap->image, cubemap->allocation, 6,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, prefilterMips);

    for (int mip = 0; mip < prefilterMips; ++mip) {
      int mipSize = baseSize >> mip;
      if (mipSize < 1) mipSize = 1;

      VkDeviceSize faceByteSize = mipSize * mipSize * 4;
      VkDeviceSize totalSize = faceByteSize * 6;
      std::vector<unsigned char> pixels(totalSize);

      for (int f = 0; f < 6; ++f) {
        for (int y = 0; y < mipSize; ++y) {
          for (int x = 0; x < mipSize; ++x) {
            int srcIdx = (y * mipSize + x) * 3;
            int dstIdx = (y * mipSize + x) * 4 + f * faceByteSize;
            pixels[dstIdx + 0] = (unsigned char)(
                std::max(0.0f, std::min(fullPrefiltered[mip][f][srcIdx + 0], 1.0f)) *
                255.0f);
            pixels[dstIdx + 1] = (unsigned char)(
                std::max(0.0f, std::min(fullPrefiltered[mip][f][srcIdx + 1], 1.0f)) *
                255.0f);
            pixels[dstIdx + 2] = (unsigned char)(
                std::max(0.0f, std::min(fullPrefiltered[mip][f][srcIdx + 2], 1.0f)) *
                255.0f);
            pixels[dstIdx + 3] = 255;
          }
        }
      }

      VkBuffer stagingBuffer;
      VmaAllocation stagingAlloc;
      allocator.createBuffer(
          totalSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
          stagingBuffer, stagingAlloc,
          VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
      void *mapped;
      vmaMapMemory(allocator.getVma(), stagingAlloc, &mapped);
      memcpy(mapped, pixels.data(), (size_t)totalSize);
      vmaUnmapMemory(allocator.getVma(), stagingAlloc);

      // Allocate one-shot command buffer
      VkCommandBufferAllocateInfo ai = {
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
      ai.commandPool = transferPool;
      ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      ai.commandBufferCount = 1;
      VkCommandBuffer cmd;
      vkAllocateCommandBuffers(device, &ai, &cmd);

      VkCommandBufferBeginInfo bi = {
          VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      vkBeginCommandBuffer(cmd, &bi);

      // Transition this mip: UNDEFINED -> TRANSFER_DST
      VkImageMemoryBarrier toDst = {
          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      toDst.image = cubemap->image;
      toDst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      toDst.subresourceRange.baseMipLevel = mip;
      toDst.subresourceRange.levelCount = 1;
      toDst.subresourceRange.baseArrayLayer = 0;
      toDst.subresourceRange.layerCount = 6;
      toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      toDst.srcAccessMask = 0;
      toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                           nullptr, 1, &toDst);

      // Copy buffer to image at this mip
      VkBufferImageCopy region{};
      region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.imageSubresource.mipLevel = mip;
      region.imageSubresource.baseArrayLayer = 0;
      region.imageSubresource.layerCount = 6;
      region.imageOffset = {0, 0, 0};
      region.imageExtent = {(uint32_t)mipSize, (uint32_t)mipSize, 1};
      vkCmdCopyBufferToImage(cmd, stagingBuffer, cubemap->image,
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

      // Transition this mip: TRANSFER_DST -> SHADER_READ_ONLY
      VkImageMemoryBarrier toRead = {
          VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      toRead.image = cubemap->image;
      toRead.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      toRead.subresourceRange.baseMipLevel = mip;
      toRead.subresourceRange.levelCount = 1;
      toRead.subresourceRange.baseArrayLayer = 0;
      toRead.subresourceRange.layerCount = 6;
      toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
      toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                           nullptr, 0, nullptr, 1, &toRead);

      vkEndCommandBuffer(cmd);

      VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
      si.commandBufferCount = 1;
      si.pCommandBuffers = &cmd;
      vkQueueSubmit(transferQueue, 1, &si, VK_NULL_HANDLE);
      vkQueueWaitIdle(transferQueue);
      vkFreeCommandBuffers(device, transferPool, 1, &cmd);

      allocator.destroyBuffer(stagingBuffer, stagingAlloc);
    }

    VkImageViewCreateInfo viewInfo = {
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = cubemap->image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = fmt;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = prefilterMips;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 6;
    vkCreateImageView(device, &viewInfo, nullptr, &cubemap->view);

    cubemap->width = baseSize;
    cubemap->height = baseSize;
    cubemap->layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    cubemap->textureId = 0;
    result.prefilterMap = cubemap;
  }

  // Step 5: Generate BRDF LUT
  SDL_Log("IBLGenerator: Generating BRDF LUT (%dx%d)...", BRDF_LUT_SIZE,
          BRDF_LUT_SIZE);
  std::vector<unsigned char> brdfData;
  generateBRDF_LUT(brdfData);

  result.brdfLUT =
      upload2DTexture(allocator, device, transferQueue, transferPool,
                      brdfData.data(), BRDF_LUT_SIZE, BRDF_LUT_SIZE,
                      VK_FORMAT_R8G8B8A8_UNORM);

  SDL_Log("IBLGenerator: Done.");
  return result;
}

IBLMaps IBLGenerator::generate(Memoria::Allocator &allocator, VkDevice device,
                               VkQueue transferQueue,
                               VkCommandPool transferPool,
                               const std::string &skyboxCrossPath) {
  SDL_Log("IBLGenerator: Generating IBL maps from %s", skyboxCrossPath.c_str());

  int width, height, channels;
  unsigned char *pixels =
      stbi_load(skyboxCrossPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);
  if (!pixels) {
    SDL_Log("IBLGenerator: FAILED to load skybox cross %s",
            skyboxCrossPath.c_str());
    return {};
  }

  std::vector<std::vector<float>> srcFaces;
  int srcFaceSize = 0;
  extractCrossFaces(pixels, width, height, srcFaces, srcFaceSize);
  stbi_image_free(pixels);
  SDL_Log("IBLGenerator: Extracted 6 faces, faceSize=%d", srcFaceSize);

  return generateFromFaces(allocator, device, transferQueue, transferPool,
                           srcFaces, srcFaceSize);
}

IBLMaps IBLGenerator::generateFromHDR(Memoria::Allocator &allocator,
                                       VkDevice device, VkQueue transferQueue,
                                       VkCommandPool transferPool,
                                       const std::string &hdrPath,
                                       int faceSize) {
  SDL_Log("IBLGenerator: Generating IBL maps from HDR %s", hdrPath.c_str());

  int w, h, c;
  float *hdrPixels = stbi_loadf(hdrPath.c_str(), &w, &h, &c, 4);
  if (!hdrPixels) {
    SDL_Log("IBLGenerator: FAILED to load HDR %s", hdrPath.c_str());
    return {};
  }

  std::vector<std::vector<float>> srcFaces;
  equirectToFaces(hdrPixels, w, h, srcFaces, faceSize);
  stbi_image_free(hdrPixels);

  return generateFromFaces(allocator, device, transferQueue, transferPool,
                           srcFaces, faceSize);
}

} // namespace Lumen
