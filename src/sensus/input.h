#pragma once

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <unordered_map>

namespace Sensus {

class Input {
public:
    Input() = default;
    ~Input() = default;

    // Call this per SDL event in the main loop
    void processEvent(const SDL_Event& event);
    
    // Call this once per frame before processing events to reset deltas
    void newFrame();

    // State queries
    bool isKeyPressed(SDL_Keycode key) const;
    glm::vec2 getMouseDelta() const;

    // Mouse capture state
    void setCapture(bool captured, SDL_Window* window);
    bool isCaptured() const { return m_captured; }

private:
    std::unordered_map<SDL_Keycode, bool> m_keys;
    glm::vec2 m_mouseDelta{0.0f, 0.0f};
    bool m_captured = false;
};

} // namespace Sensus
