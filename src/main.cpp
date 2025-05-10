// main.cpp
// Переписанный пример отрисовки треугольника с использованием простого ECS и C++23 и информативными логами

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <stdexcept>
#include <iostream>
#include <fstream>
#include <vector>
#include <functional>
#include <unordered_map>

// Макрос для логирования и проверки VK-ошибок
#define VK_CHECK_LOG(fn)                                                         \
  do {                                                                           \
    VkResult res = (fn);                                                         \
    if (res != VK_SUCCESS) {                                                     \
      std::cerr << "[VK_ERROR] " #fn " -> " << res                           \
                << " (" << __FILE__ << ":" << __LINE__ << ")" << std::endl; \
      throw std::runtime_error("Vulkan error: " #fn);                           \
    } else {                                                                     \
      std::cout << "[VK_OK] " #fn << std::endl;                                \
    }                                                                            \
  } while (0)

using Entity = uint32_t;
constexpr Entity INVALID_ENTITY = 0;

// --- Простая ECS ---
class EntityManager {
  Entity _nextId = 1;
public:
  Entity create() noexcept { return _nextId++; }
};

struct RenderComponent {
  std::function<void(VkCommandBuffer)> recordDraw;
};

class ComponentStorage {
  std::unordered_map<Entity, RenderComponent> _renders;
public:
  void add(Entity e, RenderComponent rc) { _renders[e] = std::move(rc); }
  auto& renders() noexcept { return _renders; }
};

class RenderSystem {
public:
  static void recordAll(VkCommandBuffer cb, ComponentStorage& storage) {
    for (auto& [ent, rc] : storage.renders()) {
      rc.recordDraw(cb);
    }
  }
};

// --- Утилиты ---
static std::vector<char> readFile(const std::string& path) {
  std::ifstream f(path, std::ios::ate | std::ios::binary);
  if (!f) throw std::runtime_error("Failed to open " + path);
  size_t sz = static_cast<size_t>(f.tellg());
  std::vector<char> buf(sz);
  f.seekg(0);
  f.read(buf.data(), sz);
  return buf;
}

// VulkanContext: все ресурсы и запись команд
class VulkanContext {
public:
  SDL_Window* window;
  VkInstance instance;
  VkDevice device;
  VkQueue queue;
  VkSurfaceKHR surface;
  VkSwapchainKHR swapchain;
  VkRenderPass renderPass;
  VkPipelineLayout pipelineLayout;
  VkPipeline pipeline;
  VkCommandPool cmdPool;
  std::vector<VkCommandBuffer> cmdBuffers;
  std::vector<VkFramebuffer> framebuffers;
  std::vector<VkImageView> imageViews;
  VkExtent2D extent;
  VkFormat format;
  VkColorSpaceKHR colorSpace;

  VulkanContext(SDL_Window* win) : window(win) {
    initInstance();
    createSurface();
    pickPhysicalDevice();
    createDeviceAndQueue();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createFramebuffers();
    createCommandPoolAndBuffers();
    recordCommandBuffers();
  }

  ~VulkanContext() {
    vkDeviceWaitIdle(device);
    for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    for (auto iv : imageViews) vkDestroyImageView(device, iv, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroyCommandPool(device, cmdPool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyInstance(instance, nullptr);
  }

private:
  VkPhysicalDevice physDevice{};
  uint32_t queueFamily{};

  void initInstance() {
    Uint32 ec = 0;
    const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&ec);
    std::vector<const char*> instExts(exts, exts + ec);
    std::cout << "[Init] Vulkan extensions count=" << instExts.size() << std::endl;
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "ECS Triangle";
    appInfo.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ii.pApplicationInfo = &appInfo;
    ii.enabledExtensionCount = static_cast<uint32_t>(instExts.size());
    ii.ppEnabledExtensionNames = instExts.data();
    VK_CHECK_LOG(vkCreateInstance(&ii, nullptr, &instance));
  }

  void createSurface() {
    std::cout << "[Init] Creating Vulkan surface" << std::endl;
    if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface))
      throw std::runtime_error("Failed to create surface");
  }

  void pickPhysicalDevice() {
    std::cout << "[GPU] Enumerating devices" << std::endl;
    uint32_t count = 0;
    VK_CHECK_LOG(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    std::vector<VkPhysicalDevice> devices(count);
    VK_CHECK_LOG(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    for (auto& dev : devices) {
      uint32_t qcount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, nullptr);
      std::vector<VkQueueFamilyProperties> qprops(qcount);
      vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, qprops.data());
      for (uint32_t i = 0; i < qcount; ++i) {
        VkBool32 support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &support);
        if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && support) {
          physDevice = dev;
          queueFamily = i;
          std::cout << "[GPU] Selected queue family=" << i << std::endl;
          return;
        }
      }
    }
    throw std::runtime_error("No suitable GPU");
  }

  void createDeviceAndQueue() {
    std::cout << "[Device] Creating logical device" << std::endl;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo dq{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    dq.queueFamilyIndex = queueFamily;
    dq.queueCount = 1;
    dq.pQueuePriorities = &priority;
    const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &dq;
    di.enabledExtensionCount = 1;
    di.ppEnabledExtensionNames = devExts;
    VK_CHECK_LOG(vkCreateDevice(physDevice, &di, nullptr, &device));
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
  }

  void createSwapchain() {
    std::cout << "[Swapchain] Creating swapchain" << std::endl;
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK_LOG(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDevice, surface, &caps));
    uint32_t fmtCount = 0;
    VK_CHECK_LOG(vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &fmtCount, nullptr));
    std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
    VK_CHECK_LOG(vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, surface, &fmtCount, fmts.data()));
    format = fmts[0].format;
    colorSpace = fmts[0].colorSpace;
    extent = (caps.currentExtent.width == UINT32_MAX)
             ? VkExtent2D{800,600} : caps.currentExtent;
    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = surface;
    sci.minImageCount = caps.minImageCount + 1;
    sci.imageFormat = format;
    sci.imageColorSpace = colorSpace;
    sci.imageExtent = extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    VK_CHECK_LOG(vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain));
  }

  void createImageViews() {
    std::cout << "[Swapchain] Creating image views" << std::endl;
    uint32_t imgCount = 0;
    VK_CHECK_LOG(vkGetSwapchainImagesKHR(device, swapchain, &imgCount, nullptr));
    std::vector<VkImage> images(imgCount);
    VK_CHECK_LOG(vkGetSwapchainImagesKHR(device, swapchain, &imgCount, images.data()));
    imageViews.resize(imgCount);
    for (uint32_t i = 0; i < imgCount; ++i) {
      VkImageViewCreateInfo ivc{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      ivc.image = images[i];
      ivc.viewType = VK_IMAGE_VIEW_TYPE_2D;
      ivc.format = format;
      ivc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      ivc.subresourceRange.levelCount = 1;
      ivc.subresourceRange.layerCount = 1;
      VK_CHECK_LOG(vkCreateImageView(device, &ivc, nullptr, &imageViews[i]));
    }
  }

  void createRenderPass() {
    std::cout << "[RenderPass] Creating render pass" << std::endl;
    VkAttachmentDescription att{};
    att.format = format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sub;
    VK_CHECK_LOG(vkCreateRenderPass(device, &rpci, nullptr, &renderPass));
  }

  void createGraphicsPipeline() {
    std::cout << "[Pipeline] Creating graphics pipeline" << std::endl;
    auto vertCode = readFile("vert.spv");
    auto fragCode = readFile("frag.spv");
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = vertCode.size();
    smi.pCode = reinterpret_cast<const uint32_t*>(vertCode.data());
    VkShaderModule vertModule;
    VK_CHECK_LOG(vkCreateShaderModule(device, &smi, nullptr, &vertModule));
    smi.codeSize = fragCode.size();
    smi.pCode = reinterpret_cast<const uint32_t*>(fragCode.data());
    VkShaderModule fragModule;
    VK_CHECK_LOG(vkCreateShaderModule(device, &smi, nullptr, &fragModule));
    VkPipelineShaderStageCreateInfo stages[2] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
        VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
        VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr}
    };
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vin.vertexBindingDescriptionCount = 0;
    vin.vertexAttributeDescriptionCount = 0;
    VkPipelineInputAssemblyStateCreateInfo iai{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    iai.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport vp{0, 0, (float)extent.width, (float)extent.height, 0, 1};
    VkRect2D scissor{{0,0}, extent};
    VkPipelineViewportStateCreateInfo vpState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vpState.viewportCount = 1;
    vpState.pViewports = &vp;
    vpState.scissorCount = 1;
    vpState.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cbAttach{};
    cbAttach.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cbState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cbState.attachmentCount = 1;
    cbState.pAttachments = &cbAttach;
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VK_CHECK_LOG(vkCreatePipelineLayout(device, &plci, nullptr, &pipelineLayout));
    VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vin;
    gpci.pInputAssemblyState = &iai;
    gpci.pViewportState = &vpState;
    gpci.pRasterizationState = &raster;
    gpci.pMultisampleState = &ms;
    gpci.pColorBlendState = &cbState;
    gpci.layout = pipelineLayout;
    gpci.renderPass = renderPass;
    gpci.subpass = 0;
    VK_CHECK_LOG(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &pipeline));
    vkDestroyShaderModule(device, vertModule, nullptr);
    vkDestroyShaderModule(device, fragModule, nullptr);
  }

  void createFramebuffers() {
    std::cout << "[Framebuffers] Creating framebuffers" << std::endl;
    framebuffers.resize(imageViews.size());
    for (size_t i = 0; i < imageViews.size(); ++i) {
      VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      fci.renderPass = renderPass;
      fci.attachmentCount = 1;
      fci.pAttachments = &imageViews[i];
      fci.width = extent.width;
      fci.height = extent.height;
      fci.layers = 1;
      VK_CHECK_LOG(vkCreateFramebuffer(device, &fci, nullptr, &framebuffers[i]));
    }
  }

  void createCommandPoolAndBuffers() {
    std::cout << "[CmdPool] Creating command pool and buffers" << std::endl;
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = queueFamily;
    VK_CHECK_LOG(vkCreateCommandPool(device, &cpci, nullptr, &cmdPool));
    cmdBuffers.resize(framebuffers.size());
    VkCommandBufferAllocateInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbi.commandPool = cmdPool;
    cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbi.commandBufferCount = static_cast<uint32_t>(cmdBuffers.size());
    VK_CHECK_LOG(vkAllocateCommandBuffers(device, &cbi, cmdBuffers.data()));
  }

  void recordCommandBuffers() {
    std::cout << "[CmdBuf] Recording command buffers" << std::endl;
    for (size_t i = 0; i < cmdBuffers.size(); ++i) {
      VkCommandBufferBeginInfo bbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      VK_CHECK_LOG(vkBeginCommandBuffer(cmdBuffers[i], &bbi));
      VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      rpbi.renderPass = renderPass;
      rpbi.framebuffer = framebuffers[i];
      rpbi.renderArea.extent = extent;
      VkClearValue clearColor{{0,0,0,1}};
      rpbi.clearValueCount = 1;
      rpbi.pClearValues = &clearColor;
      vkCmdBeginRenderPass(cmdBuffers[i], &rpbi, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(cmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
      vkCmdDraw(cmdBuffers[i], 3, 1, 0, 0);  // отрисовка треугольника
      vkCmdEndRenderPass(cmdBuffers[i]);
      VK_CHECK_LOG(vkEndCommandBuffer(cmdBuffers[i]));
    }
  }
};

// --- Application ---
class Application {
  SDL_Window* window = nullptr;
  VulkanContext* ctx = nullptr;
  EntityManager em;
  ComponentStorage storage;

public:
  void run() {
    initSDL();
    ctx = new VulkanContext(window);

    // Добавляем ECS RenderComponent (остаётся для будущего расширения)
    Entity e = em.create();
    storage.add(e, RenderComponent{[&](VkCommandBuffer cb) {
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->pipeline);
      vkCmdDraw(cb, 3, 1, 0, 0);
    }});

    bool running = true;
    SDL_Event ev;
    while (running) {
      while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_EVENT_QUIT) running = false;
      }
      uint32_t idx = 0;
      VK_CHECK_LOG(vkAcquireNextImageKHR(ctx->device, ctx->swapchain,
                                         UINT64_MAX, VK_NULL_HANDLE, VK_NULL_HANDLE, &idx));
      VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
      si.commandBufferCount = 1;
      si.pCommandBuffers = &ctx->cmdBuffers[idx];
      VK_CHECK_LOG(vkQueueSubmit(ctx->queue, 1, &si, VK_NULL_HANDLE));
      VK_CHECK_LOG(vkQueueWaitIdle(ctx->queue));
      VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
      pi.swapchainCount = 1;
      pi.pSwapchains = &ctx->swapchain;
      pi.pImageIndices = &idx;
      VK_CHECK_LOG(vkQueuePresentKHR(ctx->queue, &pi));
    }
    cleanup();
  }

private:
  void initSDL() {
    if (!SDL_Init(SDL_INIT_VIDEO))
      throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    window = SDL_CreateWindow("ECS Vulkan Triangle", 800, 600,
      SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!window)
      throw std::runtime_error("SDL_CreateWindow failed");
  }

  void cleanup() {
    delete ctx;
    SDL_DestroyWindow(window);
    SDL_Quit();
  }
};

int main() {
  try {
    Application app;
    app.run();
  } catch (const std::exception& e) {
    std::cerr << "Fatal error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
