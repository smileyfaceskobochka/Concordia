#pragma once

#include <SDL3/SDL.h>
#include <string>

namespace Petra {

class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    SDL_Window* getHandle() const { return m_window; }
    void getPixelSize(int& width, int& height) const;

private:
    SDL_Window* m_window = nullptr;
};

} // namespace Petra
