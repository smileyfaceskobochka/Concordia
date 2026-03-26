#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace Render {
class Context;
}

namespace Lumen {

struct PipelineConfig {
  std::string vertexShaderPath;
  std::string fragmentShaderPath;
  uint32_t pushConstantSize = 0;
  VkVertexInputBindingDescription bindingDescription;
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
  bool depthTest = true;
  bool depthWriteEnable = true;
  VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
  VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
};

class Pipeline {
public:
  Pipeline() = default;
  ~Pipeline();

  void init(const Render::Context &context, const PipelineConfig &config);
  void destroy(VkDevice device);

  void bind(VkCommandBuffer cb) const;
  VkPipelineLayout getLayout() const { return m_layout; }

private:
  VkPipeline m_pipeline = VK_NULL_HANDLE;
  VkPipelineLayout m_layout = VK_NULL_HANDLE;

  VkShaderModule createShaderModule(VkDevice device, const std::string &path);
};

} // namespace Lumen
