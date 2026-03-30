#include <SDL3/SDL_log.h>
#include <stdexcept>
#include <vulkan/vulkan.h>

// Shared macro for internal checking
#define VK_CHECK(call)                                                         \
  do {                                                                         \
    VkResult _r = (call);                                                      \
    if (_r != VK_SUCCESS) {                                                    \
      SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,                               \
                   "Vulkan error %d at " __FILE__ ":%d", _r, __LINE__);        \
      throw std::runtime_error("Vulkan call failed");                          \
    }                                                                          \
  } while (0)
