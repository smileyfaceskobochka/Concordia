#include "shader_registry.h"
#include "render/context.h"
#include "forma/mesh.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <stdexcept>
#include <algorithm>

namespace Lumen {

static std::string resolvePath(const std::string &path) {
  if (path.compare(0, 9, "assets://") == 0) {
    return std::string(CONCORDIA_ASSETS_DIR) + "/" + path.substr(9);
  }
  return path;
}

static std::string getSpvPath(const std::string &srcPath) {
  std::string resolved = resolvePath(srcPath);
  if (resolved.find(".spv") != std::string::npos) {
    return resolved;
  }
  std::filesystem::path p(resolved);
  std::string filename = p.stem().string();
  std::string ext = p.extension().string();
  if (!ext.empty() && ext[0] == '.') {
    ext = "_" + ext.substr(1);
  }
  return std::string(CONCORDIA_ASSETS_DIR) + "/shaders/compiled/dynamic/" + filename + ext + ".spv";
}

static bool compileShader(const std::string &srcPath, const std::string &destPath, const std::string &stage) {
  std::filesystem::path p(destPath);
  std::string parentDir = p.parent_path().string();
  
  // Create dynamic build directory if it doesn't exist
  SDL_CreateDirectory(parentDir.c_str());

  const char *args[] = {
    "glslc",
    stage == "vertex" ? "-fshader-stage=vertex" : "-fshader-stage=fragment",
    srcPath.c_str(),
    "-o",
    destPath.c_str(),
    nullptr
  };

  SDL_Log("ShaderRegistry: Compiling GLSL %s -> %s", srcPath.c_str(), destPath.c_str());
  SDL_Process *proc = SDL_CreateProcess(args, false);
  if (!proc) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShaderRegistry: Failed to start glslc");
    return false;
  }

  int exitcode = 0;
  SDL_WaitProcess(proc, true, &exitcode);
  SDL_DestroyProcess(proc);

  if (exitcode != 0) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShaderRegistry: glslc exited with code %d", exitcode);
    return false;
  }
  return true;
}

void ShaderRegistry::registerPipeline(const std::string &name,
                                      std::shared_ptr<Pipeline> pipeline) {
  m_pipelines[name] = pipeline;
}

std::vector<std::string> ShaderRegistry::getPipelineNames() const {
  // Return list of available shader templates for the ImGui editor
  return {"pbr", "skybox", "skybox_hdri", "transparent", "pbr_toon"};
}

std::shared_ptr<Pipeline>
ShaderRegistry::getPipeline(const std::string &name) const {
  auto it = m_pipelines.find(name);
  if (it != m_pipelines.end()) {
    return it->second;
  }
  return nullptr;
}

std::shared_ptr<Pipeline> ShaderRegistry::getOrCreatePipeline(
    const Render::Context &context,
    const PipelineKey &key,
    const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts) {
  
  auto it = m_dynamicPipelines.find(key);
  if (it != m_dynamicPipelines.end()) {
    return it->second;
  }

  std::string vsSrc = resolvePath(key.vertexShaderPath);
  std::string fsSrc = resolvePath(key.fragmentShaderPath);
  std::string vsSpv = getSpvPath(key.vertexShaderPath);
  std::string fsSpv = getSpvPath(key.fragmentShaderPath);

  if (vsSrc.find(".spv") == std::string::npos) {
    if (!compileShader(vsSrc, vsSpv, "vertex")) {
      throw std::runtime_error("ShaderRegistry: Failed to compile vertex shader: " + vsSrc);
    }
  }

  if (fsSrc.find(".spv") == std::string::npos) {
    if (!compileShader(fsSrc, fsSpv, "fragment")) {
      throw std::runtime_error("ShaderRegistry: Failed to compile fragment shader: " + fsSrc);
    }
  }

  PipelineConfig config{};
  config.vertexShaderPath = vsSpv;
  config.fragmentShaderPath = fsSpv;
  config.pushConstantSize = 160;
  config.bindingDescription = Forma::Vertex::getBindingDescription();
  config.attributeDescriptions = Forma::Vertex::getAttributeDescriptions();
  config.descriptorSetLayouts = descriptorSetLayouts;
  config.depthTest = key.depthTest;
  config.depthWriteEnable = key.depthWriteEnable;
  config.depthCompareOp = key.depthCompareOp;
  config.cullMode = key.cullMode;
  config.frontFace = key.frontFace;
  config.blendEnable = key.blendEnable;

  SDL_Log("ShaderRegistry: Creating dynamic pipeline: VS=%s, FS=%s, Blend=%d, DepthWrite=%d, CullMode=%u",
          vsSpv.c_str(), fsSpv.c_str(), key.blendEnable, key.depthWriteEnable, (uint32_t)key.cullMode);

  auto pipeline = std::make_shared<Pipeline>();
  pipeline->init(context, config);

  m_dynamicPipelines[key] = pipeline;
  return pipeline;
}

void ShaderRegistry::destroy(VkDevice device) {
  for (auto &pair : m_pipelines) {
    pair.second->destroy(device);
  }
  m_pipelines.clear();

  for (auto &pair : m_dynamicPipelines) {
    pair.second->destroy(device);
  }
  m_dynamicPipelines.clear();
}

} // namespace Lumen
