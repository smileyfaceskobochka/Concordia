#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

namespace Memoria {

class Allocator {
public:
    Allocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);
    ~Allocator();

    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;

    VmaAllocator getVma() const { return m_allocator; }

    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, 
                      VkBuffer& outBuffer, VmaAllocation& outAllocation);
    void destroyBuffer(VkBuffer buffer, VmaAllocation allocation);
    void destroyImage(VkImage image, VmaAllocation allocation);

    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, 
                    VkQueue transferQueue, VkCommandPool commandPool, VkDevice device);

    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling,
                     VkImageUsageFlags usage, VmaMemoryUsage memoryUsage,
                     VkImage& outImage, VmaAllocation& outAllocation,
                     uint32_t layerCount = 1, VkImageCreateFlags flags = 0);
    
    void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height,
                           VkQueue transferQueue, VkCommandPool commandPool, VkDevice device,
                           uint32_t layerCount = 1);

    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout,
                               VkQueue graphicsQueue, VkCommandPool commandPool, VkDevice device);



private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
};

} // namespace Memoria
