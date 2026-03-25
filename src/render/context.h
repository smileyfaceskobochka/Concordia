#pragma once

#include "petra/window.h"
#include <VkBootstrap.h>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>

namespace Render {

class Context {
public:
    Context(const Petra::Window& window);
    ~Context();

    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    void recreateSwapchain(const Petra::Window& window);

    VkInstance getInstance() const { return m_instance; }
    VkDevice getDevice() const { return m_device; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkSwapchainKHR getSwapchain() const { return m_swapchain; }
    VkRenderPass getRenderPass() const { return m_renderPass; }
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    uint32_t getGraphicsQueueFamily() const { return m_graphicsFamily; }

    const std::vector<VkImageView>& getSwapchainImageViews() const { return m_swapViews; }
    uint32_t getImageCount() const { return static_cast<uint32_t>(m_swapImages.size()); }
    VkExtent2D getSwapchainExtent() const { return m_swapExtent; }
    VkFormat getSwapchainFormat() const { return m_swapFormat; }
    VkImageView getDepthImageView() const { return m_depthView; }
    void initDepthBuffer(VmaAllocator allocator);
    void cleanupDepthBuffer(VmaAllocator allocator);
    std::string getGPUName() const { return m_gpuName; }

private:
    void createSwapchain(const Petra::Window& window);
    void createRenderPass();

    vkb::Instance m_vkbInst;
    vkb::Device m_vkbDevice;
    vkb::Swapchain m_vkbSwapchain;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsFamily = 0;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    std::vector<VkImage> m_swapImages;
    std::vector<VkImageView> m_swapViews;
    VkFormat m_swapFormat{};
    VkExtent2D m_swapExtent{};

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    
    VkImage m_depthImage = VK_NULL_HANDLE;
    VmaAllocation m_depthAllocation = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;
    VmaAllocator m_vma = VK_NULL_HANDLE;
    VkFormat m_depthFormat = VK_FORMAT_D32_SFLOAT;

    std::string m_gpuName;
};

} // namespace Render
