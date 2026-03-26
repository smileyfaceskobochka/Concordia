#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace Memoria {

class Allocator {
public:
  Allocator(VkInstance instance, VkPhysicalDevice physicalDevice,
            VkDevice device);
  ~Allocator();

  Allocator(const Allocator &) = delete;
  Allocator &operator=(const Allocator &) = delete;

  VmaAllocator getVma() const { return m_allocator; }

  void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                    VmaMemoryUsage memoryUsage, VkBuffer &outBuffer,
                    VmaAllocation &outAllocation);
  void destroyBuffer(VkBuffer buffer, VmaAllocation allocation);
  void destroyImage(VkImage image, VmaAllocation allocation);

  void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size,
                  VkQueue transferQueue, VkCommandPool commandPool,
                  VkDevice device);

  void createImage(uint32_t width, uint32_t height, VkFormat format,
                   VkImageTiling tiling, VkImageUsageFlags usage,
                   VmaMemoryUsage memoryUsage, VkImage &outImage,
                   VmaAllocation &outAllocation, uint32_t layerCount = 1,
                   VkImageCreateFlags flags = 0, uint32_t mipLevels = 1);

  void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
                         uint32_t height, VkQueue transferQueue,
                         VkCommandPool commandPool, VkDevice device,
                         uint32_t layerCount = 1, uint32_t mipLevel = 0);

  void transitionImageLayout(VkImage image, VkFormat format,
                             VkImageLayout oldLayout, VkImageLayout newLayout,
                             VkQueue graphicsQueue, VkCommandPool commandPool,
                             VkDevice device, uint32_t layerCount = 1,
                             uint32_t mipLevels = 1);

  void generateMipmaps(VkImage image, VkFormat format, uint32_t texWidth,
                       uint32_t texHeight, uint32_t mipLevels,
                       VkQueue graphicsQueue, VkCommandPool commandPool,
                       VkDevice device, uint32_t layerCount = 1);

private:
  VmaAllocator m_allocator = VK_NULL_HANDLE;
  VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
};

} // namespace Memoria
