#include "editor_keys.h"
#include "auxilia/toon.hpp"
#include "mundus/schema.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <unordered_map>

namespace Vigil {

static inline const char *obj_str(toon_value *obj, const char *key) {
  toon_value *v = toon_obj_get(obj, key);
  return v && v->type == TOON_STRING ? v->str_val : nullptr;
}
static inline double obj_num(toon_value *obj, const char *key, double def = 0.0) {
  toon_value *v = toon_obj_get(obj, key);
  return v && v->type == TOON_NUMBER ? v->num_val : def;
}
static inline bool obj_bool(toon_value *obj, const char *key, bool def = false) {
  toon_value *v = toon_obj_get(obj, key);
  return v && v->type == TOON_BOOL ? v->bool_val : def;
}

static SDL_Keycode parseKey(const std::string &name) {
  static const std::unordered_map<std::string, SDL_Keycode> table = {
    {"W", SDLK_W}, {"S", SDLK_S}, {"A", SDLK_A}, {"D", SDLK_D},
    {"SPACE", SDLK_SPACE}, {"LSHIFT", SDLK_LSHIFT}, {"RSHIFT", SDLK_RSHIFT},
    {"ESCAPE", SDLK_ESCAPE}, {"DELETE", SDLK_DELETE}, {"X", SDLK_X},
    {"H", SDLK_H}, {"GRAVE", SDLK_GRAVE},
    {"LCTRL", SDLK_LCTRL}, {"RCTRL", SDLK_RCTRL},
    {"LALT", SDLK_LALT}, {"RALT", SDLK_RALT},
  };
  auto it = table.find(name);
  if (it != table.end()) return it->second;
  if (name.size() == 1) return (SDL_Keycode)name[0];
  return SDLK_UNKNOWN;
}

EditorKeyConfig loadEditorKeyConfig(const std::string &path) {
  EditorKeyConfig cfg;
  Auxilia::toon_doc doc;
  if (!doc.load_file(path.c_str()))
    return cfg;

  std::string errors;
  if (!Mundus::Schema::validateEditorKeys(doc.get(), errors)) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "Editor keys schema violations:\n%s", errors.c_str());
  }

  {
    auto s = doc.get_string("camera_forward");
    if (s) cfg.cameraForward = parseKey(s);
  }
  {
    auto s = doc.get_string("camera_backward");
    if (s) cfg.cameraBackward = parseKey(s);
  }
  {
    auto s = doc.get_string("camera_left");
    if (s) cfg.cameraLeft = parseKey(s);
  }
  {
    auto s = doc.get_string("camera_right");
    if (s) cfg.cameraRight = parseKey(s);
  }
  {
    auto s = doc.get_string("camera_up");
    if (s) cfg.cameraUp = parseKey(s);
  }
  {
    auto s = doc.get_string("camera_down");
    if (s) cfg.cameraDown = parseKey(s);
  }
  {
    auto s = doc.get_string("capture_exit");
    if (s) cfg.captureExit = parseKey(s);
  }

  toon_value *arr = toon_obj_get(doc.get(), "bindings");
  if (arr && arr->type == TOON_ARRAY) {
    for (size_t i = 0; i < arr->len; ++i) {
      toon_value *e = &arr->arr[i];
      EditorKeyBinding b;
      const char *action = obj_str(e, "action");
      const char *key = obj_str(e, "key");
      if (!action || !key) continue;
      b.action = action;
      b.key = parseKey(key);
      b.ctrl = obj_bool(e, "ctrl", false);
      b.shift = obj_bool(e, "shift", false);
      b.alt = obj_bool(e, "alt", false);
      cfg.bindings.push_back(b);
    }
  }

  return cfg;
}

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

  // Load config once (static)
  static EditorKeyConfig cfg = loadEditorKeyConfig(
      std::string(CONCORDIA_ASSETS_DIR) + "/config/editor_keys.toon");

  // Process bindings
  for (auto &b : cfg.bindings) {
    if (k != b.key) continue;
    if (b.ctrl != ctrl) continue;
    if (b.shift != shift) continue;
    if (b.alt != alt) continue;

    if (b.action == "delete") {
      int sel = overlay.getSelectedEntity();
      if (sel >= 0) {
        scene.removeEntity(sel);
        overlay.setSelectedEntity(-1);
      }
      return true;
    }

    if (b.action == "toggle_visibility") {
      int sel = overlay.getSelectedEntity();
      if (sel >= 0 && sel < static_cast<int>(scene.getEntities().size()))
        scene.setEntityVisible(sel, !scene.getEntities()[sel].visible);
      return true;
    }

    if (b.action == "show_all") {
      for (auto &ent : scene.getEntities())
        ent.visible = true;
      return true;
    }

    if (b.action == "toggle_selection") {
      int current = overlay.getSelectedEntity();
      if (current >= 0)
        overlay.setSelectedEntity(-1);
      else if (!scene.getEntities().empty())
        overlay.setSelectedEntity(0);
      return true;
    }

    if (b.action == "focus_camera") {
      int sel = overlay.getSelectedEntity();
      if (sel >= 0 && sel < static_cast<int>(scene.getEntities().size())) {
        glm::vec3 target = scene.getEntities()[sel].transform.position;
        glm::vec3 eye = target + glm::vec3(2.0f, 1.0f, 2.0f);
        camera.lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
      }
      return true;
    }

    if (b.action == "duplicate") {
      int sel = overlay.getSelectedEntity();
      if (sel >= 0) {
        int dup = scene.duplicateEntity(sel);
        if (dup >= 0) overlay.setSelectedEntity(dup);
      }
      return true;
    }
  }

  return false;
}

} // namespace Vigil
