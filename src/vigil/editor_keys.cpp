#include "editor_keys.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Vigil {

bool processEditorKeys(const SDL_Event &ev, bool imguiCapturesKeyboard,
                       bool imguiCapturesMouse, bool inputCaptured,
                       Vigil::Overlay &overlay,
                       Mundus::Scene &scene, Vista::Camera &camera) {
  if (ev.type != SDL_EVENT_KEY_DOWN) return false;
  if (imguiCapturesKeyboard || inputCaptured) return false;

  bool ctrl = (ev.key.mod & SDL_KMOD_CTRL) != 0;
  bool shift = (ev.key.mod & SDL_KMOD_SHIFT) != 0;
  bool alt = (ev.key.mod & SDL_KMOD_ALT) != 0;
  SDL_Keycode k = ev.key.key;

  if ((k == SDLK_DELETE || k == SDLK_X) && !ctrl) {
    int sel = overlay.getSelectedEntity();
    if (sel >= 0) {
      scene.removeEntity(sel);
      overlay.setSelectedEntity(-1);
    }
    return true;
  }

  if (k == SDLK_H && !alt) {
    int sel = overlay.getSelectedEntity();
    if (sel >= 0 && sel < static_cast<int>(scene.getEntities().size()))
      scene.setEntityVisible(sel, !scene.getEntities()[sel].visible);
    return true;
  }

  if (k == SDLK_H && alt) {
    for (auto &ent : scene.getEntities())
      ent.visible = true;
    return true;
  }

  if (k == SDLK_A && !ctrl) {
    int current = overlay.getSelectedEntity();
    if (current >= 0)
      overlay.setSelectedEntity(-1);
    else if (!scene.getEntities().empty())
      overlay.setSelectedEntity(0);
    return true;
  }

  if (k == SDLK_GRAVE) {
    int sel = overlay.getSelectedEntity();
    if (sel >= 0 && sel < static_cast<int>(scene.getEntities().size())) {
      glm::vec3 target = scene.getEntities()[sel].transform.position;
      glm::vec3 eye = target + glm::vec3(2.0f, 1.0f, 2.0f);
      camera.lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    return true;
  }

  if (k == SDLK_D && shift) {
    int sel = overlay.getSelectedEntity();
    if (sel >= 0) {
      int dup = scene.duplicateEntity(sel);
      if (dup >= 0) overlay.setSelectedEntity(dup);
    }
    return true;
  }

  return false;
}

} // namespace Vigil
