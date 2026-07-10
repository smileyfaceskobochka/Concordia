#pragma once

#include "pipeline.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace Render {
class Context;
}

namespace Lumen {

struct PipelineKey {
  std::string vertexShaderPath;
  std::string fragmentShaderPath;
  VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
  VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  bool depthTest = true;
  bool depthWriteEnable = true;
  VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
  bool blendEnable = false;

  bool operator==(const PipelineKey &o) const {
    return vertexShaderPath == o.vertexShaderPath &&
           fragmentShaderPath == o.fragmentShaderPath &&
           cullMode == o.cullMode &&
           frontFace == o.frontFace &&
           depthTest == o.depthTest &&
           depthWriteEnable == o.depthWriteEnable &&
           depthCompareOp == o.depthCompareOp &&
           blendEnable == o.blendEnable;
  }
};

struct PipelineKeyHash {
  std::size_t operator()(const PipelineKey &k) const {
    std::size_t h1 = std::hash<std::string>{}(k.vertexShaderPath);
    std::size_t h2 = std::hash<std::string>{}(k.fragmentShaderPath);
    std::size_t h3 = k.cullMode;
    std::size_t h4 = k.frontFace;
    std::size_t h5 = k.depthTest ? 1 : 0;
    std::size_t h6 = k.depthWriteEnable ? 1 : 0;
    std::size_t h7 = k.depthCompareOp;
    std::size_t h8 = k.blendEnable ? 1 : 0;
    return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4) ^ (h6 << 5) ^ (h7 << 6) ^ (h8 << 7);
  }
};

class ShaderRegistry {
public:
  ShaderRegistry() = default;
  ~ShaderRegistry() = default;

  // Legacy compatibility:
  void registerPipeline(const std::string &name,
                        std::shared_ptr<Pipeline> pipeline);
  std::shared_ptr<Pipeline> getPipeline(const std::string &name) const;
  std::vector<std::string> getPipelineNames() const;

  // New Dynamic API:
  std::shared_ptr<Pipeline> getOrCreatePipeline(const Render::Context &context,
                                                const PipelineKey &key,
                                                const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts);

  void destroy(VkDevice device);

private:
  std::unordered_map<std::string, std::shared_ptr<Pipeline>> m_pipelines;
  std::unordered_map<PipelineKey, std::shared_ptr<Pipeline>, PipelineKeyHash> m_dynamicPipelines;
};

} // namespace Lumen
