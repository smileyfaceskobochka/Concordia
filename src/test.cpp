#include <SDL3/SDL.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_oldnames.h> // Для совместимости со старыми названиями (может быть не обязательно)
#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>
#include <cstring>
#include <iostream>

int main() {
  int init = SDL_Init(SDL_INIT_VIDEO);
  const char *err = SDL_GetError();
  if (init != 0 && (strlen(err) > 0)) {
    std::cerr << "SDL_Init Error: " << err << std::endl;
    return 1;
  }

  SDL_Window *window =
      SDL_CreateWindow("Test SDL Window", 640, 480, SDL_WINDOW_RESIZABLE);
  if (window == nullptr) {
    std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
    SDL_Quit();
    return 1;
  }

  SDL_Surface *surface = SDL_GetWindowSurface(window);
  if (!surface) {
    std::cerr << "SDL_GetWindowSurface Error: " << SDL_GetError() << std::endl;
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }

  Uint32 redColor = SDL_MapSurfaceRGB(surface, 255, 0, 0);

  SDL_Rect rect;
  rect.x = 100;
  rect.y = 80;
  rect.w = 200;
  rect.h = 150;

  if (SDL_FillSurfaceRect(surface, &rect, redColor) != 0) {
    std::cerr << "SDL_FillSurfaceRect Error: " << SDL_GetError() << std::endl;
  }

  if (SDL_UpdateWindowSurface(window) != 0) {
    std::cerr << "SDL_UpdateWindowSurface Error: " << SDL_GetError()
              << std::endl;
  }

  SDL_Event event;
  bool running = true;
  while (running) {
    std::cout << "Event loop iteration\n";
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running = false;
      }
    }
    SDL_Delay(10);
  }

  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}