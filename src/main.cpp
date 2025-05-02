#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>

// simple VK error check
#define VK_CHECK(fn)                                                           \
  do {                                                                         \
    if ((fn) != VK_SUCCESS)                                                    \
      throw std::runtime_error("Vulkan error: " #fn);                          \
  } while (0)

// load SPIR‑V
static std::vector<char> readFile(const std::string &path) {
  std::ifstream f(path, std::ios::ate | std::ios::binary);
  if (!f)
    throw std::runtime_error("Failed to open " + path);
  size_t sz = (size_t)f.tellg();
  std::vector<char> buf(sz);
  f.seekg(0);
  f.read(buf.data(), sz);
  return buf;
}

int main() {
  // 1) SDL init
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    // return 1;
  }

  std::cout << "Video driver selected: " << SDL_GetCurrentVideoDriver() << std::endl;

  // 2) create Vulkan window
  SDL_Window *win = SDL_CreateWindow("Vulkan Triangle", 800, 600,
                                     SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (!win) {
    std::cerr << "SDL_CreateWindow failed\n";
    return 1;
  }

  // 3) instance extensions from SDL
  Uint32 ec = 0;
  const char *const *exts = SDL_Vulkan_GetInstanceExtensions(&ec);
  std::vector<const char *> instExts(exts, exts + ec);

  // 4) create instance
  VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  ai.pApplicationName = "Triangle";
  ai.apiVersion = VK_API_VERSION_1_0;
  VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ii.pApplicationInfo = &ai;
  ii.enabledExtensionCount = (uint32_t)instExts.size();
  ii.ppEnabledExtensionNames = instExts.data();
  VkInstance instance;
  VK_CHECK(vkCreateInstance(&ii, nullptr, &instance));

  // 5) create surface
  VkSurfaceKHR surface;
  if (!SDL_Vulkan_CreateSurface(win, instance, nullptr, &surface)) {
    std::cerr << "SDL_Vulkan_CreateSurface failed\n";
    return 1;
  }

  // 6) pick GPU + queue family
  uint32_t pc = 0;
  VK_CHECK(vkEnumeratePhysicalDevices(instance, &pc, nullptr));
  std::vector<VkPhysicalDevice> pds(pc);
  VK_CHECK(vkEnumeratePhysicalDevices(instance, &pc, pds.data()));
  VkPhysicalDevice pd = VK_NULL_HANDLE;
  uint32_t qfam = 0;
  for (auto &d : pds) {
    uint32_t qc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, nullptr);
    std::vector<VkQueueFamilyProperties> qps(qc);
    vkGetPhysicalDeviceQueueFamilyProperties(d, &qc, qps.data());
    for (uint32_t i = 0; i < qc; i++) {
      VkBool32 pres = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &pres);
      if ((qps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && pres) {
        pd = d;
        qfam = i;
        break;
      }
    }
    if (pd)
      break;
  }
  if (!pd)
    throw std::runtime_error("No GPU");

  // 7) logical device + queue (enable swapchain as device extension)
  float pr = 1.0f;
  VkDeviceQueueCreateInfo dq{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  dq.queueFamilyIndex = qfam;
  dq.queueCount = 1;
  dq.pQueuePriorities = &pr;
  const char *devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  di.queueCreateInfoCount = 1;
  di.pQueueCreateInfos = &dq;
  di.enabledExtensionCount = 1;
  di.ppEnabledExtensionNames = devExts;
  VkDevice device;
  VK_CHECK(vkCreateDevice(pd, &di, nullptr, &device));
  VkQueue queue;
  vkGetDeviceQueue(device, qfam, 0, &queue);

  // 8) swapchain
  VkSurfaceCapabilitiesKHR caps;
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pd, surface, &caps));
  uint32_t fc = 0;
  VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &fc, nullptr));
  std::vector<VkSurfaceFormatKHR> fmts(fc);
  VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(pd, surface, &fc, fmts.data()));
  VkFormat fmt = fmts[0].format;
  VkColorSpaceKHR cs = fmts[0].colorSpace;
  VkExtent2D ext = (caps.currentExtent.width == 0xFFFFFFFFu)
                       ? VkExtent2D{800, 600}
                       : caps.currentExtent;
  VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  sci.surface = surface;
  sci.minImageCount = caps.minImageCount + 1;
  sci.imageFormat = fmt;
  sci.imageColorSpace = cs;
  sci.imageExtent = ext;
  sci.imageArrayLayers = 1;
  sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  sci.clipped = VK_TRUE;
  VkSwapchainKHR swapchain;
  VK_CHECK(vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain));

  // 9) image views
  uint32_t ic = 0;
  VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &ic, nullptr));
  std::vector<VkImage> imgs(ic);
  VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &ic, imgs.data()));
  std::vector<VkImageView> ivs(ic);
  for (uint32_t i = 0; i < ic; i++) {
    VkImageViewCreateInfo ivc{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivc.image = imgs[i];
    ivc.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivc.format = fmt;
    ivc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivc.subresourceRange.levelCount = 1;
    ivc.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(device, &ivc, nullptr, &ivs[i]));
  }

  // 10) render pass
  VkAttachmentDescription ca{};
  ca.format = fmt;
  ca.samples = VK_SAMPLE_COUNT_1_BIT;
  ca.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  ca.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  ca.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  ca.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sp{};
  sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sp.colorAttachmentCount = 1;
  sp.pColorAttachments = &cr;
  VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rpci.attachmentCount = 1;
  rpci.pAttachments = &ca;
  rpci.subpassCount = 1;
  rpci.pSubpasses = &sp;
  VkRenderPass rp;
  VK_CHECK(vkCreateRenderPass(device, &rpci, nullptr, &rp));

  // 11) framebuffers
  std::vector<VkFramebuffer> fbs(ic);
  for (uint32_t i = 0; i < ic; i++) {
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = rp;
    fci.attachmentCount = 1;
    fci.pAttachments = &ivs[i];
    fci.width = ext.width;
    fci.height = ext.height;
    fci.layers = 1;
    VK_CHECK(vkCreateFramebuffer(device, &fci, nullptr, &fbs[i]));
  }

  // 12) load shaders
  auto vsp = readFile("vert.spv"), fsp = readFile("frag.spv");
  VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  sm.codeSize = vsp.size();
  sm.pCode = reinterpret_cast<const uint32_t *>(vsp.data());
  VkShaderModule vm, fm;
  VK_CHECK(vkCreateShaderModule(device, &sm, nullptr, &vm));
  sm.codeSize = fsp.size();
  sm.pCode = reinterpret_cast<const uint32_t *>(fsp.data());
  VK_CHECK(vkCreateShaderModule(device, &sm, nullptr, &fm));

  VkPipelineShaderStageCreateInfo stages[2] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
       VK_SHADER_STAGE_VERTEX_BIT, vm, "main", nullptr},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
       VK_SHADER_STAGE_FRAGMENT_BIT, fm, "main", nullptr}};

  // 13) pipeline (no vertex-input, gl_VertexIndex)
  VkPipelineVertexInputStateCreateInfo vips{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vips.vertexBindingDescriptionCount = 0;
  vips.vertexAttributeDescriptionCount = 0;
  VkPipelineInputAssemblyStateCreateInfo ias{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport vp{0, 0, (float)ext.width, (float)ext.height, 0, 1};
  VkRect2D sc{{0, 0}, ext};
  VkPipelineViewportStateCreateInfo pvs{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  pvs.viewportCount = 1;
  pvs.pViewports = &vp;
  pvs.scissorCount = 1;
  pvs.pScissors = &sc;
  VkPipelineRasterizationStateCreateInfo prs{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  prs.polygonMode = VK_POLYGON_MODE_FILL;
  prs.cullMode = VK_CULL_MODE_NONE;
  prs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  prs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo pms{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  pms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState pca{};
  pca.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo pcb{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  pcb.attachmentCount = 1;
  pcb.pAttachments = &pca;
  VkPipelineLayoutCreateInfo plci{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  VkPipelineLayout pl;
  VK_CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &pl));
  VkGraphicsPipelineCreateInfo gpci{
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gpci.stageCount = 2;
  gpci.pStages = stages;
  gpci.pVertexInputState = &vips;
  gpci.pInputAssemblyState = &ias;
  gpci.pViewportState = &pvs;
  gpci.pRasterizationState = &prs;
  gpci.pMultisampleState = &pms;
  gpci.pColorBlendState = &pcb;
  gpci.layout = pl;
  gpci.renderPass = rp;
  gpci.subpass = 0;
  VkPipeline pipe;
  VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr,
                                     &pipe));

  // 14) cmd pool + buffers
  VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpci.queueFamilyIndex = qfam;
  VkCommandPool cp;
  VK_CHECK(vkCreateCommandPool(device, &cpci, nullptr, &cp));
  std::vector<VkCommandBuffer> cbs(ic);
  VkCommandBufferAllocateInfo cbi{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cbi.commandPool = cp;
  cbi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbi.commandBufferCount = ic;
  VK_CHECK(vkAllocateCommandBuffers(device, &cbi, cbs.data()));
  for (uint32_t i = 0; i < ic; i++) {
    VkCommandBufferBeginInfo cbb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(cbs[i], &cbb));
    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpbi.renderPass = rp;
    rpbi.framebuffer = fbs[i];
    rpbi.renderArea.extent = ext;
    VkClearValue cv{{0, 0, 0, 1}};
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &cv;
    vkCmdBeginRenderPass(cbs[i], &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cbs[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdDraw(cbs[i], 3, 1, 0, 0);
    vkCmdEndRenderPass(cbs[i]);
    VK_CHECK(vkEndCommandBuffer(cbs[i]));
  }

  // 15) main loop
  bool run = true;
  SDL_Event e;
  while (run) {
    while (SDL_PollEvent(&e))
      if (e.type == SDL_EVENT_QUIT)
        run = false;
    uint32_t idx;
    VK_CHECK(vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                   VK_NULL_HANDLE, VK_NULL_HANDLE, &idx));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cbs[idx];
    VK_CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &idx;
    VK_CHECK(vkQueuePresentKHR(queue, &pi));
  }

  std::cout << "Exiting and cleaning up." << '\n';

  // 16) cleanup (reverse order) …
  vkDeviceWaitIdle(device);
  vkDestroyPipeline(device, pipe, nullptr);
  vkDestroyPipelineLayout(device, pl, nullptr);
  vkDestroyShaderModule(device, fm, nullptr);
  vkDestroyShaderModule(device, vm, nullptr);
  for (auto fb : fbs)
    vkDestroyFramebuffer(device, fb, nullptr);
  vkDestroyRenderPass(device, rp, nullptr);
  for (auto iv : ivs)
    vkDestroyImageView(device, iv, nullptr);
  vkDestroySwapchainKHR(device, swapchain, nullptr);
  vkDestroyCommandPool(device, cp, nullptr);
  vkDestroyDevice(device, nullptr);
  vkDestroySurfaceKHR(instance, surface, nullptr);
  vkDestroyInstance(instance, nullptr);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
