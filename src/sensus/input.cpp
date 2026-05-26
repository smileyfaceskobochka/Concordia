#include "input.h"

namespace Sensus {

int mouseButtonToSDL(MouseButton btn) {
  switch (btn) {
    case MouseButton::Left:   return SDL_BUTTON_LEFT;
    case MouseButton::Middle: return SDL_BUTTON_MIDDLE;
    case MouseButton::Right:  return SDL_BUTTON_RIGHT;
  }
  return 0;
}

void Input::processEvent(const SDL_Event &event) {
  if (event.type == SDL_EVENT_KEY_DOWN) {
    m_keys[event.key.key] = true;
  } else if (event.type == SDL_EVENT_KEY_UP) {
    m_keys[event.key.key] = false;
  } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
    m_mousePos = {event.motion.x, event.motion.y};
    if (m_captured) {
      m_mouseDelta.x += event.motion.xrel;
      m_mouseDelta.y += event.motion.yrel;
    }
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    if (event.button.button == SDL_BUTTON_LEFT)   m_mouseButtons[0] = true;
    if (event.button.button == SDL_BUTTON_MIDDLE) m_mouseButtons[1] = true;
    if (event.button.button == SDL_BUTTON_RIGHT)  m_mouseButtons[2] = true;
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
    if (event.button.button == SDL_BUTTON_LEFT)   m_mouseButtons[0] = false;
    if (event.button.button == SDL_BUTTON_MIDDLE) m_mouseButtons[1] = false;
    if (event.button.button == SDL_BUTTON_RIGHT)  m_mouseButtons[2] = false;
  }
}

void Input::newFrame() { m_mouseDelta = {0.0f, 0.0f}; }

bool Input::isKeyPressed(SDL_Keycode key) const {
  auto it = m_keys.find(key);
  if (it != m_keys.end()) {
    return it->second;
  }
  return false;
}

glm::vec2 Input::getMouseDelta() const { return m_mouseDelta; }

bool Input::isMousePressed(MouseButton btn) const {
  return m_mouseButtons[static_cast<int>(btn)];
}

void Input::setCapture(bool captured, SDL_Window *window) {
  m_captured = captured;
  SDL_SetWindowRelativeMouseMode(window, captured ? true : false);
}

} // namespace Sensus
