#include "ISystemWindow.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <iostream>

class SDL3Window : public ISystemWindow {
  SDL_Window *_win = nullptr;

public:
  bool init(int w, int h, const char *title) override {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      std::cerr << "SDL_Init failed\n";
      return false;
    }
    _win =
        SDL_CreateWindow(title, w, h, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    return _win != nullptr;
  }
  bool pollEvents() override {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT)
        return false;
    }
    return true;
  }
  std::vector<const char *> getRequiredInstanceExtensions() override {
    Uint32 cnt = 0;
    const char *const *names = SDL_Vulkan_GetInstanceExtensions(&cnt);
    return std::vector<const char *>(names, names + cnt);
  }
  VkSurfaceKHR createSurface(VkInstance inst) override {
    VkSurfaceKHR surf;
    if (!SDL_Vulkan_CreateSurface(_win, inst, nullptr, &surf)) {
      std::cerr << "SDL3Window: failed create surface\n";
      return VK_NULL_HANDLE;
    }
    return surf;
  }
  ~SDL3Window() override {
    if (_win)
      SDL_DestroyWindow(_win);
    SDL_Quit();
  }
};

extern "C" ISystemWindow *CreateWindowSystem() { return new SDL3Window(); }
extern "C" void DestroyWindowSystem(ISystemWindow *p) { delete p; }
