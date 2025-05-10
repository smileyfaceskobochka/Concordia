#include "ISystemGraphics.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>

#define VK_OK_OR_THROW(fn)                                                     \
  do {                                                                         \
    if ((fn) != VK_SUCCESS)                                                    \
      throw std::runtime_error("Vulkan failed: " #fn);                         \
  } while (0)

// Загрузить SPIR‑V из файла
static std::vector<char> readSPV(const std::string &path) {
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file)
    throw std::runtime_error("Cannot open " + path);
  size_t size = file.tellg();
  std::vector<char> buf(size);
  file.seekg(0);
  file.read(buf.data(), size);
  return buf;
}

class VulkanGraphics : public ISystemGraphics {
  // Core Vulkan objects
  VkInstance _inst{};
  VkSurfaceKHR _surf{};
  VkPhysicalDevice _physDev{};
  uint32_t _queueFam{};
  VkDevice _dev{};
  VkQueue _queue{};

  // Swapchain
  VkSwapchainKHR _swap{};
  VkFormat _fmt{};
  VkColorSpaceKHR _cs{};
  VkExtent2D _extent{};
  std::vector<VkImage> _imgs;
  std::vector<VkImageView> _views;

  // Render pass + pipeline
  VkRenderPass _rp{};
  VkDescriptorSetLayout _dsl{};
  VkPipelineLayout _layout{};
  VkPipeline _pipe{};

  // Framebuffers & cmds
  std::vector<VkFramebuffer> _fbs;
  VkCommandPool _cp{};
  std::vector<VkCommandBuffer> _cbs;

  // SSBO & descriptors
  VkBuffer _vb{};
  VkDeviceMemory _vbMem{};
  uint32_t _vCount{};
  VkDescriptorPool _dp{};
  VkDescriptorSet _ds{};

  // Вспомогательная функция для выбора памяти
  uint32_t pickMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(_physDev, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
      if ((typeFilter & (1 << i)) &&
          (mp.memoryTypes[i].propertyFlags & props) == props)
        return i;
    }
    throw std::runtime_error("No suitable memory type");
  }

public:
  bool initInstance(const std::vector<const char *> &exts) override {
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.pApplicationName = "ModularTriangle";
    ai.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ii.pApplicationInfo = &ai;
    ii.enabledExtensionCount = (uint32_t)exts.size();
    ii.ppEnabledExtensionNames = exts.data();
    VK_OK_OR_THROW(vkCreateInstance(&ii, nullptr, &_inst));
    return true;
  }

  VkInstance getInstance() const override { return _inst; }

  bool createSurface(VkSurfaceKHR s) override {
    _surf = s;
    return s != VK_NULL_HANDLE;
  }

  bool pickPhysicalDevice() override {
    uint32_t cnt = 0;
    VK_OK_OR_THROW(vkEnumeratePhysicalDevices(_inst, &cnt, nullptr));
    std::vector<VkPhysicalDevice> devs(cnt);
    VK_OK_OR_THROW(vkEnumeratePhysicalDevices(_inst, &cnt, devs.data()));
    for (auto &d : devs) {
      uint32_t qn = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
      std::vector<VkQueueFamilyProperties> qp(qn);
      vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qp.data());
      for (uint32_t i = 0; i < qn; ++i) {
        VkBool32 ok = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(d, i, _surf, &ok);
        if ((qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && ok) {
          _physDev = d;
          _queueFam = i;
          return true;
        }
      }
    }
    return false;
  }

  bool createLogicalDevice() override {
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = _queueFam;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    const char *devExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = 1;
    di.ppEnabledExtensionNames = devExt;
    VK_OK_OR_THROW(vkCreateDevice(_physDev, &di, nullptr, &_dev));
    vkGetDeviceQueue(_dev, _queueFam, 0, &_queue);
    return true;
  }

  bool createSwapchain() override {
    VkSurfaceCapabilitiesKHR caps;
    VK_OK_OR_THROW(
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_physDev, _surf, &caps));
    uint32_t fcount = 0;
    VK_OK_OR_THROW(vkGetPhysicalDeviceSurfaceFormatsKHR(_physDev, _surf,
                                                        &fcount, nullptr));
    std::vector<VkSurfaceFormatKHR> fmts(fcount);
    VK_OK_OR_THROW(vkGetPhysicalDeviceSurfaceFormatsKHR(_physDev, _surf,
                                                        &fcount, fmts.data()));
    _fmt = fmts[0].format;
    _cs = fmts[0].colorSpace;
    _extent = (caps.currentExtent.width == UINT32_MAX) ? VkExtent2D{800, 600}
                                                       : caps.currentExtent;
    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = _surf;
    sci.minImageCount = caps.minImageCount + 1;
    sci.imageFormat = _fmt;
    sci.imageColorSpace = _cs;
    sci.imageExtent = _extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    VK_OK_OR_THROW(vkCreateSwapchainKHR(_dev, &sci, nullptr, &_swap));
    return true;
  }

  bool createFrameResources() override {
    // Images & Views
    uint32_t imgCount = 0;
    VK_OK_OR_THROW(vkGetSwapchainImagesKHR(_dev, _swap, &imgCount, nullptr));
    _imgs.resize(imgCount);
    VK_OK_OR_THROW(
        vkGetSwapchainImagesKHR(_dev, _swap, &imgCount, _imgs.data()));
    _views.resize(imgCount);
    for (uint32_t i = 0; i < imgCount; ++i) {
      VkImageViewCreateInfo iv{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
      iv.image = _imgs[i];
      iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
      iv.format = _fmt;
      iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      iv.subresourceRange.levelCount = 1;
      iv.subresourceRange.layerCount = 1;
      VK_OK_OR_THROW(vkCreateImageView(_dev, &iv, nullptr, &_views[i]));
    }

    // RenderPass
    VkAttachmentDescription att{};
    att.format = _fmt;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpci.attachmentCount = 1;
    rpci.pAttachments = &att;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &sp;
    VK_OK_OR_THROW(vkCreateRenderPass(_dev, &rpci, nullptr, &_rp));

    // DescriptorSetLayout (binding 0 = SSBO)
    VkDescriptorSetLayoutBinding bnd{};
    bnd.binding = 0;
    bnd.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bnd.descriptorCount = 1;
    bnd.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    bnd.pImmutableSamplers = nullptr;
    VkDescriptorSetLayoutCreateInfo dslci{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dslci.bindingCount = 1;
    dslci.pBindings = &bnd;
    VK_OK_OR_THROW(vkCreateDescriptorSetLayout(_dev, &dslci, nullptr, &_dsl));

    // PipelineLayout
    VkPipelineLayoutCreateInfo plci{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &_dsl;
    VK_OK_OR_THROW(vkCreatePipelineLayout(_dev, &plci, nullptr, &_layout));

    // Shader Modules
    auto vsp = readSPV("vert.spv");
    auto fsp = readSPV("frag.spv");
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};

    smci.codeSize = vsp.size();
    smci.pCode = reinterpret_cast<const uint32_t *>(vsp.data());
    VkShaderModule vm;
    VK_OK_OR_THROW(vkCreateShaderModule(_dev, &smci, nullptr, &vm));

    smci.codeSize = fsp.size();
    smci.pCode = reinterpret_cast<const uint32_t *>(fsp.data());
    VkShaderModule fm;
    VK_OK_OR_THROW(vkCreateShaderModule(_dev, &smci, nullptr, &fm));

    VkPipelineShaderStageCreateInfo sts[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_VERTEX_BIT, vm, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
         VK_SHADER_STAGE_FRAGMENT_BIT, fm, "main", nullptr}};

    // Pipeline States (vertex-input disabled)
    VkPipelineVertexInputStateCreateInfo vibi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vibi.vertexBindingDescriptionCount = 0;
    vibi.vertexAttributeDescriptionCount = 0;

    VkPipelineInputAssemblyStateCreateInfo iasci{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    iasci.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport vp{0,    0,   float(_extent.width), float(_extent.height),
                  0.0f, 1.0f};
    VkRect2D sc{{0, 0}, _extent};
    VkPipelineViewportStateCreateInfo vpsci{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vpsci.viewportCount = 1;
    vpsci.pViewports = &vp;
    vpsci.scissorCount = 1;
    vpsci.pScissors = &sc;

    VkPipelineRasterizationStateCreateInfo rsci{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rsci.polygonMode = VK_POLYGON_MODE_FILL;
    rsci.cullMode = VK_CULL_MODE_NONE;
    rsci.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rsci.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo mssci{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    mssci.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cbas{};
    cbas.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cbsci{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cbsci.attachmentCount = 1;
    cbsci.pAttachments = &cbas;

    VkGraphicsPipelineCreateInfo gpci{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpci.stageCount = 2;
    gpci.pStages = sts;
    gpci.pVertexInputState = &vibi;
    gpci.pInputAssemblyState = &iasci;
    gpci.pViewportState = &vpsci;
    gpci.pRasterizationState = &rsci;
    gpci.pMultisampleState = &mssci;
    gpci.pColorBlendState = &cbsci;
    gpci.layout = _layout;
    gpci.renderPass = _rp;
    gpci.subpass = 0;

    VK_OK_OR_THROW(vkCreateGraphicsPipelines(_dev, VK_NULL_HANDLE, 1, &gpci,
                                             nullptr, &_pipe));

    vkDestroyShaderModule(_dev, vm, nullptr);
    vkDestroyShaderModule(_dev, fm, nullptr);

    // Создаём SSBO с треугольником
    std::vector<float> verts = {-0.5f, -0.5f, 0.5f, -0.5f, 0.0f, 0.5f};
    _vCount = 3;
    VkDeviceSize bufSz = sizeof(float) * 2 * _vCount;

    // Buffer + Memory
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = bufSz;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_OK_OR_THROW(vkCreateBuffer(_dev, &bci, nullptr, &_vb));

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(_dev, _vb, &mr);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = pickMemoryType(
        mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_OK_OR_THROW(vkAllocateMemory(_dev, &mai, nullptr, &_vbMem));
    VK_OK_OR_THROW(vkBindBufferMemory(_dev, _vb, _vbMem, 0));

    // Fill data
    void *dat = nullptr;
    vkMapMemory(_dev, _vbMem, 0, bufSz, 0, &dat);
    std::memcpy(dat, verts.data(), (size_t)bufSz);
    vkUnmapMemory(_dev, _vbMem);

    // DescriptorPool
    VkDescriptorPoolSize dps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo dpci{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &dps;
    dpci.maxSets = 1;
    VK_OK_OR_THROW(vkCreateDescriptorPool(_dev, &dpci, nullptr, &_dp));

    // Allocate DS
    VkDescriptorSetAllocateInfo dsai{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = _dp;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &_dsl;
    VK_OK_OR_THROW(vkAllocateDescriptorSets(_dev, &dsai, &_ds));

    // Update DS
    VkDescriptorBufferInfo dbi{};
    dbi.buffer = _vb;
    dbi.offset = 0;
    dbi.range = bufSz;
    VkWriteDescriptorSet wds{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    wds.dstSet = _ds;
    wds.dstBinding = 0;
    wds.dstArrayElement = 0;
    wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wds.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(_dev, 1, &wds, 0, nullptr);

    // Framebuffers
    _fbs.resize(imgCount);
    for (uint32_t i = 0; i < imgCount; ++i) {
      VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
      fbi.renderPass = _rp;
      fbi.attachmentCount = 1;
      fbi.pAttachments = &_views[i];
      fbi.width = _extent.width;
      fbi.height = _extent.height;
      fbi.layers = 1;
      VK_OK_OR_THROW(vkCreateFramebuffer(_dev, &fbi, nullptr, &_fbs[i]));
    }

    // CommandPool + Buffers
    VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpci.queueFamilyIndex = _queueFam;
    VK_OK_OR_THROW(vkCreateCommandPool(_dev, &cpci, nullptr, &_cp));

    _cbs.resize(imgCount);
    VkCommandBufferAllocateInfo cbai{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = _cp;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = imgCount;
    VK_OK_OR_THROW(vkAllocateCommandBuffers(_dev, &cbai, _cbs.data()));

    for (uint32_t i = 0; i < imgCount; ++i) {
      VkCommandBufferBeginInfo bbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      VK_OK_OR_THROW(vkBeginCommandBuffer(_cbs[i], &bbi));
      VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
      rpbi.renderPass = _rp;
      rpbi.framebuffer = _fbs[i];
      rpbi.renderArea.extent = _extent;
      VkClearValue cv{{0, 0, 0, 1}};
      rpbi.clearValueCount = 1;
      rpbi.pClearValues = &cv;
      vkCmdBeginRenderPass(_cbs[i], &rpbi, VK_SUBPASS_CONTENTS_INLINE);
      vkCmdBindPipeline(_cbs[i], VK_PIPELINE_BIND_POINT_GRAPHICS, _pipe);
      vkCmdBindDescriptorSets(_cbs[i], VK_PIPELINE_BIND_POINT_GRAPHICS, _layout,
                              0, 1, &_ds, 0, nullptr);
      vkCmdDraw(_cbs[i], _vCount, 1, 0, 0);
      vkCmdEndRenderPass(_cbs[i]);
      VK_OK_OR_THROW(vkEndCommandBuffer(_cbs[i]));
    }

    return true;
  }

  void renderFrame() override {
    uint32_t idx;
    VK_OK_OR_THROW(vkAcquireNextImageKHR(_dev, _swap, UINT64_MAX,
                                         VK_NULL_HANDLE, VK_NULL_HANDLE, &idx));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &_cbs[idx];
    VK_OK_OR_THROW(vkQueueSubmit(_queue, 1, &si, VK_NULL_HANDLE));
    VK_OK_OR_THROW(vkQueueWaitIdle(_queue));
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.swapchainCount = 1;
    pi.pSwapchains = &_swap;
    pi.pImageIndices = &idx;
    VK_OK_OR_THROW(vkQueuePresentKHR(_queue, &pi));
  }

  ~VulkanGraphics() override {
    vkDeviceWaitIdle(_dev);
    vkDestroyDescriptorPool(_dev, _dp, nullptr);
    vkDestroyDescriptorSetLayout(_dev, _dsl, nullptr);
    vkDestroyBuffer(_dev, _vb, nullptr);
    vkFreeMemory(_dev, _vbMem, nullptr);
    for (auto &fb : _fbs)
      vkDestroyFramebuffer(_dev, fb, nullptr);
    vkDestroyPipeline(_dev, _pipe, nullptr);
    vkDestroyPipelineLayout(_dev, _layout, nullptr);
    vkDestroyRenderPass(_dev, _rp, nullptr);
    for (auto &v : _views)
      vkDestroyImageView(_dev, v, nullptr);
    vkDestroySwapchainKHR(_dev, _swap, nullptr);
    vkDestroyCommandPool(_dev, _cp, nullptr);
    vkDestroyDevice(_dev, nullptr);
    vkDestroySurfaceKHR(_inst, _surf, nullptr);
    vkDestroyInstance(_inst, nullptr);
  }
};

// Экспорт фабрик
extern "C" ISystemGraphics *CreateGraphicsDevice() {
  return new VulkanGraphics();
}
extern "C" void DestroyGraphicsDevice(ISystemGraphics *p) { delete p; }
