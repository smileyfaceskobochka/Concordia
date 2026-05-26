#include "shader_registry.h"

namespace Lumen {

void ShaderRegistry::registerPipeline(const std::string &name,
                                      std::shared_ptr<Pipeline> pipeline) {
  m_pipelines[name] = pipeline;
}

std::vector<std::string> ShaderRegistry::getPipelineNames() const {
  std::vector<std::string> names;
  names.reserve(m_pipelines.size());
  for (auto &pair : m_pipelines)
    names.push_back(pair.first);
  return names;
}

std::shared_ptr<Pipeline>
ShaderRegistry::getPipeline(const std::string &name) const {
  auto it = m_pipelines.find(name);
  if (it != m_pipelines.end()) {
    return it->second;
  }
  return nullptr;
}

void ShaderRegistry::destroy(VkDevice device) {
  for (auto &pair : m_pipelines) {
    pair.second->destroy(device);
  }
  m_pipelines.clear();
}

} // namespace Lumen
