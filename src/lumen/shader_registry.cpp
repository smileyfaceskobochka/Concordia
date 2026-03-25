#include "shader_registry.h"

namespace Lumen {

void ShaderRegistry::registerPipeline(const std::string& name, std::shared_ptr<Pipeline> pipeline) {
    m_pipelines[name] = pipeline;
}

std::shared_ptr<Pipeline> ShaderRegistry::getPipeline(const std::string& name) const {
    auto it = m_pipelines.find(name);
    if (it != m_pipelines.end()) {
        return it->second;
    }
    return nullptr;
}

void ShaderRegistry::destroy(VkDevice device) {
    for (auto& pair : m_pipelines) {
        pair.second->destroy(device);
    }
    m_pipelines.clear();
}

} // namespace Lumen
