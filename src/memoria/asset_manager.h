#pragma once

#include "allocator.h"
#include <flecs.h>
#include <forma/mesh.h>
#include <string>
#include <vk_mem_alloc.h>
#include <memory>
#include <unordered_map>
#include <vector>

#include "types.h"

namespace Mundus {
struct ShaderAsset;
}

namespace Memoria {

class AssetManager {
public:
  struct Manifest {
    std::vector<std::string> preloadMeshes;
    std::string defaultSkybox;
  };

  struct SkyboxAsset {
    std::string name;
    std::string path;
    bool isHDR = false;
  };

  struct SkyboxScanDir {
    std::string path;
    bool isHDR = false;
  };

  struct DefaultMaterial {
    std::string shader = "pbr";
    glm::vec4 baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
  };

  AssetManager(Allocator &allocator, VkDevice device, VkQueue transferQueue,
               VkCommandPool transferPool);
  ~AssetManager();

  bool loadManifest(const char *path, flecs::world &ecs);

  const Manifest &getManifest() const { return m_manifest; }
  const DefaultMaterial &getDefaultMaterial() const { return m_defaultMaterial; }
  const std::vector<SkyboxScanDir> &getSkyboxScanDirs() const { return m_skyboxScanDirs; }
  const std::vector<std::string> &getSkyboxFaceNames() const { return m_skyboxFaceNames; }

  std::shared_ptr<MeshAsset> createCubeMesh();
  std::shared_ptr<TextureAsset> loadTexture(const std::string &path,
                                              bool srgb = true);
  std::shared_ptr<TextureAsset> loadTextureFromMemory(const unsigned char *data,
                                                        size_t size,
                                                        const std::string &name,
                                                        bool srgb = true);
  std::shared_ptr<TextureAsset> loadCubemap(const std::string &directoryPath);
  std::shared_ptr<TextureAsset> loadCubemapFromCross(const std::string &path);
  std::shared_ptr<TextureAsset> loadHDR(const std::string &path);

  std::vector<SkyboxAsset> scanSkyboxes() const;

  std::shared_ptr<MeshAsset> getMesh(const std::string &source);
  std::shared_ptr<TextureAsset> resolveTexture(const std::string &source, bool srgb);
  std::shared_ptr<Mundus::ShaderAsset> loadShader(const std::string &path);

  void loadGLTF(const std::string &path, flecs::world &ecs,
                flecs::entity_t parent = 0, bool instantiate = true);

  const std::vector<std::shared_ptr<TextureAsset>> &getLoadedTextures() const {
    return m_textureLinearStore;
  }

  std::shared_ptr<TextureAsset> getDefaultWhite() { return m_defaultWhiteSRGB; }
  std::shared_ptr<TextureAsset> getDefaultWhiteLinear() { return m_defaultWhiteLinear; }
  std::shared_ptr<TextureAsset> getDefaultBlack() { return m_defaultBlackSRGB; }
  std::shared_ptr<TextureAsset> getDefaultBlackLinear() { return m_defaultBlackLinear; }
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

  Manifest m_manifest;
  DefaultMaterial m_defaultMaterial;
  std::vector<SkyboxScanDir> m_skyboxScanDirs;
  std::vector<std::string> m_skyboxFaceNames;

  std::shared_ptr<TextureAsset> m_defaultWhiteSRGB;
  std::shared_ptr<TextureAsset> m_defaultWhiteLinear;
  std::shared_ptr<TextureAsset> m_defaultBlackSRGB;
  std::shared_ptr<TextureAsset> m_defaultBlackLinear;
  std::shared_ptr<TextureAsset> m_defaultNormal;
  std::shared_ptr<TextureAsset> m_defaultBRDF;

  std::unordered_map<std::string, std::shared_ptr<MeshAsset>> m_meshes;
  std::unordered_map<std::string, std::shared_ptr<TextureAsset>> m_textures;
  std::unordered_map<std::string, std::shared_ptr<Mundus::ShaderAsset>> m_shaders;
  std::vector<std::shared_ptr<MeshAsset>> m_meshStore;
  std::vector<std::shared_ptr<TextureAsset>> m_textureLinearStore;
};

} // namespace Memoria
