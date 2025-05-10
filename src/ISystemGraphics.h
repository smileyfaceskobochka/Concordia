#pragma once

#include <vector>
#include <vulkan/vulkan.h>

// Rendering subsystem interface
struct ISystemGraphics {
  virtual bool initInstance(const std::vector<const char *> &exts) = 0;

  virtual VkInstance getInstance() const = 0;

  virtual bool createSurface(VkSurfaceKHR surface) = 0;

  virtual bool pickPhysicalDevice() = 0;

  virtual bool createLogicalDevice() = 0;

  virtual bool createSwapchain() = 0;

  virtual bool createFrameResources() = 0;

  virtual void renderFrame() = 0;

  virtual ~ISystemGraphics() = default;
};
