#include "input.h"

namespace Sensus {

void Input::processEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN) {
        m_keys[event.key.key] = true;
    } else if (event.type == SDL_EVENT_KEY_UP) {
        m_keys[event.key.key] = false;
    } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
        if (m_captured) {
            m_mouseDelta.x += event.motion.xrel;
            m_mouseDelta.y += event.motion.yrel;
        }
    }
}

void Input::newFrame() {
    m_mouseDelta = {0.0f, 0.0f};
}

bool Input::isKeyPressed(SDL_Keycode key) const {
    auto it = m_keys.find(key);
    if (it != m_keys.end()) {
        return it->second;
    }
    return false;
}

glm::vec2 Input::getMouseDelta() const {
    return m_mouseDelta;
}

void Input::setCapture(bool captured, SDL_Window* window) {
    m_captured = captured;
    SDL_SetWindowRelativeMouseMode(window, captured ? true : false);
}

} // namespace Sensus
