#pragma once

#include <vulkan/vulkan.h>

namespace Memoria {

class Sampler {
public:
    Sampler() = default;
    ~Sampler() = default;

    void init(VkDevice device);
    void destroy(VkDevice device);

    VkSampler getSampler() const { return m_sampler; }

private:
    VkSampler m_sampler = VK_NULL_HANDLE;
};

} // namespace Memoria
