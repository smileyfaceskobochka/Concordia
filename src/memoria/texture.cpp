#include "texture.h"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <stdexcept>
#include <iostream>

namespace Memoria {

void Texture::load(const std::string& path, Allocator& allocator, VkQueue graphicsQueue, VkCommandPool cmdPool, VkDevice device) {
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(path.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    
    if (!pixels) {
        throw std::runtime_error("Failed to load texture image: " + path);
    }

    m_width = static_cast<uint32_t>(texWidth);
    m_height = static_cast<uint32_t>(texHeight);
    VkDeviceSize imageSize = m_width * m_height * 4;

    std::cout << "Texture loaded via STB: " << path << " (" << m_width << "x" << m_height << ")" << std::endl;

    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    allocator.createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingAlloc);

    void* data;
    vmaMapMemory(allocator.getVma(), stagingAlloc, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vmaUnmapMemory(allocator.getVma(), stagingAlloc);

    stbi_image_free(pixels);

    allocator.createImage(m_width, m_height, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TILING_OPTIMAL,
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                         VMA_MEMORY_USAGE_GPU_ONLY, m_image, m_allocation);

    // Initial transition to transfer destination
    allocator.transitionImageLayout(m_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   graphicsQueue, cmdPool, device);
    
    // Copy the staging buffer to the image
    allocator.copyBufferToImage(stagingBuffer, m_image, m_width, m_height, graphicsQueue, cmdPool, device);
    
    // Transition to shader read
    allocator.transitionImageLayout(m_image, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   graphicsQueue, cmdPool, device);

    allocator.destroyBuffer(stagingBuffer, stagingAlloc);

    VkImageViewCreateInfo viewInfo = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &m_view) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture image view");
    }
}

void Texture::destroy(Allocator& allocator, VkDevice device) {
    if (m_view) vkDestroyImageView(device, m_view, nullptr);
    if (m_image) vmaDestroyImage(allocator.getVma(), m_image, m_allocation);
    m_view = VK_NULL_HANDLE;
    m_image = VK_NULL_HANDLE;
}

} // namespace Memoria
