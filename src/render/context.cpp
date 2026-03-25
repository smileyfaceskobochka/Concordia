#include "context.h"
#include <SDL3/SDL_vulkan.h>
#include <stdexcept>

// Shared macro for internal checking
#define VK_CHECK(call)                                                         \
  do {                                                                         \
    VkResult _r = (call);                                                      \
    if (_r != VK_SUCCESS) {                                                    \
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,                               \
                   "Vulkan error %d at " __FILE__ ":%d", _r, __LINE__);       \
      throw std::runtime_error("Vulkan call failed");                          \
    }                                                                          \
  } while (0)

namespace Render {

Context::Context(const Petra::Window& window) {
    // Instance
    Uint32 extCount = 0;
    const char *const *exts = SDL_Vulkan_GetInstanceExtensions(&extCount);

    vkb::InstanceBuilder builder;
    builder.set_app_name("Concordia")
           .require_api_version(1, 1, 0)
           .request_validation_layers(true)
           .use_default_debug_messenger();
    for (Uint32 i = 0; i < extCount; ++i) {
        builder.enable_extension(exts[i]);
    }

    auto instRet = builder.build();
    if (!instRet) {
        throw std::runtime_error("Instance: " + instRet.error().message());
    }
    m_vkbInst = instRet.value();
    m_instance = m_vkbInst.instance;

    // Surface
    if (!SDL_Vulkan_CreateSurface(window.getHandle(), m_instance, nullptr, &m_surface)) {
        throw std::runtime_error(SDL_GetError());
    }

    // Physical Device
    vkb::PhysicalDeviceSelector sel{m_vkbInst};
    auto physRet = sel.set_surface(m_surface)
                      .set_minimum_version(1, 1)
                      .select();
    if (!physRet) {
        throw std::runtime_error("PhysDevice: " + physRet.error().message());
    }
    m_physicalDevice = physRet.value().physical_device;
    m_gpuName = physRet.value().name;

    // Logical Device
    vkb::DeviceBuilder devBuilder{physRet.value()};
    auto devRet = devBuilder.build();
    if (!devRet) {
        throw std::runtime_error("Device: " + devRet.error().message());
    }
    m_vkbDevice = devRet.value();
    m_device = m_vkbDevice.device;

    m_graphicsQueue = m_vkbDevice.get_queue(vkb::QueueType::graphics).value();
    m_graphicsFamily = m_vkbDevice.get_queue_index(vkb::QueueType::graphics).value();

    // Swapchain & RenderPass
    createSwapchain(window);
    createRenderPass();
    SDL_Log("Render::Context initialised.");
}

Context::~Context() {
    if (m_device) {
        if (m_depthView) vkDestroyImageView(m_device, m_depthView, nullptr);
        vkDestroyRenderPass(m_device, m_renderPass, nullptr);
        vkb::destroy_swapchain(m_vkbSwapchain);
        vkb::destroy_device(m_vkbDevice);
    }
    if (m_instance) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        vkb::destroy_instance(m_vkbInst);
    }
}

void Context::createSwapchain(const Petra::Window& window) {
    int pw = 0, ph = 0;
    window.getPixelSize(pw, ph);

    vkb::SwapchainBuilder sb{m_vkbDevice};
    auto ret = sb.set_old_swapchain(m_swapchain)
                 .set_desired_extent(static_cast<uint32_t>(pw),
                                     static_cast<uint32_t>(ph))
                 .build();
    if (!ret) {
        throw std::runtime_error("Swapchain: " + ret.error().message());
    }

    vkb::destroy_swapchain(m_vkbSwapchain);
    m_vkbSwapchain = ret.value();
    m_swapchain = m_vkbSwapchain.swapchain;
    m_swapFormat = m_vkbSwapchain.image_format;
    m_swapExtent = m_vkbSwapchain.extent;
    m_swapImages = m_vkbSwapchain.get_images().value();
    m_swapViews = m_vkbSwapchain.get_image_views().value();
}

void Context::recreateSwapchain(const Petra::Window& window) {
    vkDeviceWaitIdle(m_device);
    createSwapchain(window);
}

void Context::createRenderPass() {
    VkAttachmentDescription colorAtt{};
    colorAtt.format         = m_swapFormat;
    colorAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAtt{};
    depthAtt.format         = m_depthFormat;
    depthAtt.samples        = VK_SAMPLE_COUNT_1_BIT;
    depthAtt.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    depthAtt.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAtt.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription  sub{};
    sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments    = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[] = {colorAtt, depthAtt};
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 2;
    rpci.pAttachments    = attachments;
    rpci.subpassCount    = 1;
    rpci.pSubpasses      = &sub;
    rpci.dependencyCount = 1;
    rpci.pDependencies   = &dep;

    VK_CHECK(vkCreateRenderPass(m_device, &rpci, nullptr, &m_renderPass));
}

void Context::initDepthBuffer(VmaAllocator allocator) {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = m_depthFormat;
    ici.extent = {m_swapExtent.width, m_swapExtent.height, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    aci.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(allocator, &ici, &aci, &m_depthImage, &m_depthAllocation, nullptr));

    VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivci.image = m_depthImage;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = m_depthFormat;
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    ivci.subresourceRange.baseMipLevel = 0;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.baseArrayLayer = 0;
    ivci.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(m_device, &ivci, nullptr, &m_depthView));
}

void Context::cleanupDepthBuffer(VmaAllocator allocator) {
    if (m_depthView) {
        vkDestroyImageView(m_device, m_depthView, nullptr);
        m_depthView = VK_NULL_HANDLE;
    }
    if (m_depthImage) {
        vmaDestroyImage(allocator, m_depthImage, m_depthAllocation);
        m_depthImage = VK_NULL_HANDLE;
    }
}

} // namespace Render
