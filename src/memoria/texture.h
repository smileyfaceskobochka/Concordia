#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>
#include "allocator.h"

namespace Memoria {

class Texture {
public:
    Texture() = default;
    ~Texture() = default;

    void load(const std::string& path, Allocator& allocator, VkQueue graphicsQueue, VkCommandPool cmdPool, VkDevice device);
    void destroy(Allocator& allocator, VkDevice device);

    VkImage getImage() const { return m_image; }
    VkImageView getView() const { return m_view; }

private:
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
};

} // namespace Memoria
