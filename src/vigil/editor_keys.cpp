#include "editor_keys.h"
#include "auxilia/ctoon.hpp"
#include <cstdio>
#include <cstring>
#include <unordered_map>

#ifndef CONCORDIA_ASSETS_DIR
#define CONCORDIA_ASSETS_DIR "assets"
#endif

// ── helpers ──────────────────────────────────────────────────────────────

static inline const char *obj_str(ctoon_value *obj, const char *key) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  return v && v->type == CTOON_STRING ? v->str_val : nullptr;
}
static inline double obj_num(ctoon_value *obj, const char *key, double def) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  return v && v->type == CTOON_NUMBER ? v->num_val : def;
}
static inline bool obj_bool(ctoon_value *obj, const char *key, bool def) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  return v && v->type == CTOON_BOOL ? v->bool_val : def;
}

static const std::unordered_map<std::string, SDL_Keycode> keyMap = {
  {"A", SDLK_A}, {"B", SDLK_B}, {"C", SDLK_C}, {"D", SDLK_D},
  {"E", SDLK_E}, {"F", SDLK_F}, {"G", SDLK_G}, {"H", SDLK_H},
  {"I", SDLK_I}, {"J", SDLK_J}, {"K", SDLK_K}, {"L", SDLK_L},
  {"M", SDLK_M}, {"N", SDLK_N}, {"O", SDLK_O}, {"P", SDLK_P},
  {"Q", SDLK_Q}, {"R", SDLK_R}, {"S", SDLK_S}, {"T", SDLK_T},
  {"U", SDLK_U}, {"V", SDLK_V}, {"W", SDLK_W}, {"X", SDLK_X},
  {"Y", SDLK_Y}, {"Z", SDLK_Z},
  {"SPACE", SDLK_SPACE}, {"ESCAPE", SDLK_ESCAPE},
  {"DELETE", SDLK_DELETE}, {"GRAVE", SDLK_GRAVE},
  {"LSHIFT", SDLK_LSHIFT}, {"RSHIFT", SDLK_RSHIFT},
};

namespace Vigil {

EditorKeyConfig loadEditorKeyConfig(const std::string &path) {
  EditorKeyConfig cfg;
  Auxilia::ctoon_doc doc;
  if (!doc.load_file(path.c_str())) return cfg;

  auto readKey = [&](const char *key_name, SDL_Keycode fallback) {
    const char *s = doc.get_string(key_name);
    if (!s) return fallback;
    auto it = keyMap.find(s);
    return it != keyMap.end() ? it->second : fallback;
  };

  cfg.cameraForward = readKey("camera_forward", SDLK_W);
  cfg.cameraBackward = readKey("camera_backward", SDLK_S);
  cfg.cameraLeft = readKey("camera_left", SDLK_A);
  cfg.cameraRight = readKey("camera_right", SDLK_D);
  cfg.cameraUp = readKey("camera_up", SDLK_SPACE);
  cfg.cameraDown = readKey("camera_down", SDLK_LSHIFT);
  cfg.captureExit = readKey("capture_exit", SDLK_ESCAPE);

  ctoon_value *root = doc.get();
  ctoon_value *barr = root ? ctoon_obj_get(root, "bindings") : nullptr;
  if (!barr || barr->type != CTOON_ARRAY) return cfg;

  for (size_t i = 0; i < barr->len; ++i) {
    ctoon_value *b = &barr->arr[i];
    if (b->type != CTOON_OBJECT) continue;
    const char *action = obj_str(b, "action");
    const char *key_str = obj_str(b, "key");
    if (!action || !key_str) continue;

    EditorKeyBinding kb;
    kb.action = action;
    auto it = keyMap.find(key_str);
    if (it == keyMap.end()) continue;
    kb.key = it->second;
    kb.ctrl = obj_bool(b, "ctrl", false);
    kb.shift = obj_bool(b, "shift", false);
    kb.alt = obj_bool(b, "alt", false);
    cfg.bindings.push_back(kb);
  }

  return cfg;
}

bool processEditorKeys(const SDL_Event &ev, bool imguiCapturesKeyboard,
                       bool imguiCapturesMouse, bool inputCaptured,
                       Vigil::Overlay &overlay,
                       flecs::world &ecs,
                       Vista::Camera &camera) {
  if (inputCaptured) return false;
  if (ev.type != SDL_EVENT_KEY_DOWN) return false;
  if (imguiCapturesKeyboard) return false;

  SDL_Keycode key = ev.key.key;
  bool ctrl = (ev.key.mod & SDL_KMOD_CTRL) != 0;
  bool shift = (ev.key.mod & SDL_KMOD_SHIFT) != 0;
  bool alt = (ev.key.mod & SDL_KMOD_ALT) != 0;

  static EditorKeyConfig config;
  [[maybe_unused]] static bool loaded = [&]() {
    std::string cfgPath = std::string(CONCORDIA_ASSETS_DIR) + "/config/editor_keys.toon";
    config = loadEditorKeyConfig(cfgPath);
    return true;
  }();

  for (auto &b : config.bindings) {
    if (b.key != key) continue;
    if (b.ctrl != ctrl) continue;
    if (b.shift != shift) continue;
    if (b.alt != alt) continue;

    if (b.action == "delete") {
      flecs::entity sel = overlay.getSelectedEntity();
      if (sel.is_alive()) {
        std::function<void(flecs::entity)> destroyTree;
        destroyTree = [&](flecs::entity ent) {
          ent.children([&](flecs::entity child) { destroyTree(child); });
          ent.destruct();
        };
        destroyTree(sel);
        overlay.setSelectedEntity(flecs::entity());
      }
      return true;
    }

    if (b.action == "toggle_visibility") {
      flecs::entity sel = overlay.getSelectedEntity();
      if (sel.is_alive()) {
        auto &v = sel.get_mut<Mundus::Visibility>();
        v.visible = !v.visible;
      }
      return true;
    }

    if (b.action == "show_all") {
      ecs.each([](flecs::entity e, Mundus::Visibility &v) {
        (void)e;
        v.visible = true;
      });
      return true;
    }

    if (b.action == "toggle_selection") {
      flecs::entity sel = overlay.getSelectedEntity();
      if (sel.is_alive())
        overlay.setSelectedEntity(flecs::entity());
      else {
        ecs.query_builder<>().with<Mundus::Name>().build()
          .each([&](flecs::iter &it, size_t row) {
            overlay.setSelectedEntity(it.entity(row));
          });
      }
      return true;
    }

    if (b.action == "focus_camera") {
      flecs::entity sel = overlay.getSelectedEntity();
      if (sel.is_alive()) {
        const Mundus::GlobalTransform *gt = sel.try_get<Mundus::GlobalTransform>();
        if (gt) {
          glm::vec3 target(gt->value[3][0], gt->value[3][1], gt->value[3][2]);
          glm::vec3 eye = target + glm::vec3(2.0f, 1.0f, 2.0f);
          camera.lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
        }
      }
      return true;
    }

    if (b.action == "duplicate") {
      flecs::entity sel = overlay.getSelectedEntity();
      if (sel.is_alive()) {
        const Mundus::Name *n = sel.try_get<Mundus::Name>();
        if (n) {
          flecs::entity dup = ecs.entity((n->value + "_copy").c_str())
            .set<Mundus::Name>({n->value + "_copy"});
          const Mundus::Transform *t = sel.try_get<Mundus::Transform>();
          if (t) dup.set<Mundus::Transform>(*t);
          const Mundus::Visibility *v = sel.try_get<Mundus::Visibility>();
          if (v) dup.set<Mundus::Visibility>(*v);
          const Mundus::MeshSource *ms = sel.try_get<Mundus::MeshSource>();
          if (ms) dup.set<Mundus::MeshSource>(*ms);
          const Mundus::MeshAssetRef *mar = sel.try_get<Mundus::MeshAssetRef>();
          if (mar) dup.set<Mundus::MeshAssetRef>(*mar);
          const Mundus::MaterialRef *mr = sel.try_get<Mundus::MaterialRef>();
          if (mr) dup.set<Mundus::MaterialRef>(*mr);
          overlay.setSelectedEntity(dup);
        }
      }
      return true;
    }
  }

  return false;
}

} // namespace Vigil
