#pragma once

#include <unordered_map>
#include <string>
#include <memory>
#include "pipeline.h"

namespace Render {
    class Context;
}

namespace Lumen {

class ShaderRegistry {
public:
    ShaderRegistry() = default;
    ~ShaderRegistry() = default;

    void registerPipeline(const std::string& name, std::shared_ptr<Pipeline> pipeline);
    std::shared_ptr<Pipeline> getPipeline(const std::string& name) const;

    void destroy(VkDevice device);

private:
    std::unordered_map<std::string, std::shared_ptr<Pipeline>> m_pipelines;
};

} // namespace Lumen
