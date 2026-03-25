#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

namespace Render {

struct RenderObject {
    VkBuffer     vertexBuffer;
    VmaAllocation vertexAllocation;
    uint32_t     vertexCount;

    VkBuffer     indexBuffer;
    VmaAllocation indexAllocation;
    uint32_t     indexCount;

    glm::mat4    transform;
};

} // namespace Render
