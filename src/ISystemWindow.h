#pragma once

#include <vector>
#include <vulkan/vulkan.h>

struct ISystemWindow {
  // Window creation
  virtual bool init(int width, int height, const char *title) = 0;

  virtual bool pollEvents() = 0;

  virtual std::vector<const char *> getRequiredInstanceExtensions() = 0;

  virtual VkSurfaceKHR createSurface(VkInstance instance) = 0;
  virtual ~ISystemWindow() = default;
};
