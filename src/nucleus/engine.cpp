#define GLM_ENABLE_EXPERIMENTAL
#include "engine.h"
#include "forma/material.h"
#include "forma/mesh.h"
#include "lumen/pipeline.h"
#include "lumen/shader_registry.h"
#include "mundus/schema.h"
#include "mundus/ctoon_helpers.h"
#include "vista/camera.h"
#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <render/vk_check.h>
#include <stdexcept>
#include <string>
#include <vk_mem_alloc.h>
#include <algorithm>
#include <cmath>
#include "vigil/editor_keys.h"

struct PushConstants {
  glm::mat4 model;
  uint32_t debugMode;
};

namespace Nucleus {

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

// ── ECS bridge helpers ──────────────────────────────────────────────────

static void registerEcsComponents(flecs::world &ecs) {
  ecs.component<Mundus::Transform>();
  ecs.component<Mundus::Name>();
  ecs.component<Mundus::Id>();
  ecs.component<Mundus::GlobalTransform>();
  ecs.component<Mundus::EffectiveVisibility>();
  ecs.component<Mundus::MeshSource>();
  ecs.component<Mundus::ModelSource>();
  ecs.component<Mundus::MeshAssetRef>();
  ecs.component<Mundus::MaterialRef>();
  ecs.component<Mundus::Visibility>();
  ecs.component<Mundus::LightDir>();
  ecs.component<Mundus::LightColor>();
  ecs.component<Mundus::GltfInternalNode>();
  ecs.component<Mundus::GltfDefaultTransform>();
  ecs.component<Mundus::GltfDefaultMaterial>();
}

// ── ECS-native CTOON save ────────────────────────────────────────────────

static bool isMaterialModified(const std::shared_ptr<Forma::Material> &current, const std::shared_ptr<Forma::Material> &defaults) {
  if (!current && !defaults) return false;
  if (!current || !defaults) return true;
  if (current == defaults) return false;
  if (current->shaderName != defaults->shaderName) return true;
  if (current->baseColor != defaults->baseColor) return true;
  if (std::abs(current->roughness - defaults->roughness) > 1e-4f) return true;
  if (std::abs(current->metallic - defaults->metallic) > 1e-4f) return true;
  if (current->albedoSource != defaults->albedoSource) return true;
  if (current->normalSource != defaults->normalSource) return true;
  if (current->metallicRoughnessSource != defaults->metallicRoughnessSource) return true;
  if (current->aoSource != defaults->aoSource) return true;
  if (current->emissiveSource != defaults->emissiveSource) return true;
  return false;
}

static int countOverrides(flecs::entity current) {
  int count = 0;
  current.children([&](flecs::entity child) {
    if (child.has<Mundus::GltfInternalNode>()) {
      const Mundus::Name *nm = child.try_get<Mundus::Name>();
      if (nm && !nm->value.empty()) {
        const Mundus::Transform *t = child.try_get<Mundus::Transform>();
        const Mundus::GltfDefaultTransform *dt = child.try_get<Mundus::GltfDefaultTransform>();
        if (t && dt) {
          if (glm::distance(t->position, dt->position) > 1e-4f) count++;
          if (glm::distance(t->rotation, dt->rotation) > 1e-4f) count++;
          if (glm::distance(t->scale, dt->scale) > 1e-4f) count++;
        }
        const Mundus::MaterialRef *mr = child.try_get<Mundus::MaterialRef>();
        const Mundus::GltfDefaultMaterial *dm = child.try_get<Mundus::GltfDefaultMaterial>();
        if (mr && mr->value && dm && dm->value) {
          if (isMaterialModified(mr->value, dm->value)) count++;
        }
        const Mundus::Visibility *v = child.try_get<Mundus::Visibility>();
        if (v && !v->visible) count++;
      }
      count += countOverrides(child);
    }
  });
  return count;
}

static void collectOverrides(flecs::entity root, flecs::entity current, ctoon_value *overridesObj) {
  current.children([&](flecs::entity child) {
    if (child.has<Mundus::GltfInternalNode>()) {
      const Mundus::Name *nm = child.try_get<Mundus::Name>();
      if (nm && !nm->value.empty()) {
        std::string prefix = nm->value + ".";

        const Mundus::Transform *t = child.try_get<Mundus::Transform>();
        const Mundus::GltfDefaultTransform *dt = child.try_get<Mundus::GltfDefaultTransform>();
        if (t && dt) {
          if (glm::distance(t->position, dt->position) > 1e-4f) {
            std::string k = prefix + "transform.pos";
            Mundus::CtoonHelpers::set_vec3(overridesObj, k.c_str(), t->position.x, t->position.y, t->position.z);
          }
          if (glm::distance(t->rotation, dt->rotation) > 1e-4f) {
            std::string k = prefix + "transform.rot";
            glm::quat q(t->rotation);
            Mundus::CtoonHelpers::set_quat(overridesObj, k.c_str(), q.x, q.y, q.z, q.w);
          }
          if (glm::distance(t->scale, dt->scale) > 1e-4f) {
            std::string k = prefix + "transform.scale";
            Mundus::CtoonHelpers::set_vec3(overridesObj, k.c_str(), t->scale.x, t->scale.y, t->scale.z);
          }
        }

        const Mundus::MaterialRef *mr = child.try_get<Mundus::MaterialRef>();
        const Mundus::GltfDefaultMaterial *dm = child.try_get<Mundus::GltfDefaultMaterial>();
        if (mr && mr->value && dm && dm->value) {
          if (isMaterialModified(mr->value, dm->value)) {
            auto m = mr->value;
            auto d = dm->value;
            if (m->shaderName != d->shaderName) {
              std::string k = prefix + "material.shader";
              if (m->shaderName.find('/') != std::string::npos) {
                std::string ref = "@asset(assets://" + m->shaderName + ")";
                Mundus::CtoonHelpers::set_str(overridesObj, k.c_str(), ref.c_str());
              } else {
                Mundus::CtoonHelpers::set_str(overridesObj, k.c_str(), m->shaderName.c_str());
              }
            }
            if (m->baseColor != d->baseColor) {
              std::string k = prefix + "material.base_color";
              Mundus::CtoonHelpers::set_color(overridesObj, k.c_str(), m->baseColor.x, m->baseColor.y, m->baseColor.z, m->baseColor.w);
            }
            if (std::abs(m->roughness - d->roughness) > 1e-4f) {
              std::string k = prefix + "material.roughness";
              Mundus::CtoonHelpers::set_num(overridesObj, k.c_str(), m->roughness);
            }
            if (std::abs(m->metallic - d->metallic) > 1e-4f) {
              std::string k = prefix + "material.metallic";
              Mundus::CtoonHelpers::set_num(overridesObj, k.c_str(), m->metallic);
            }
            if (m->albedoSource != d->albedoSource) {
              std::string k = prefix + "material.albedo";
              Mundus::CtoonHelpers::set_str(overridesObj, k.c_str(), m->albedoSource.c_str());
            }
            if (m->normalSource != d->normalSource) {
              std::string k = prefix + "material.normal";
              Mundus::CtoonHelpers::set_str(overridesObj, k.c_str(), m->normalSource.c_str());
            }
            if (m->metallicRoughnessSource != d->metallicRoughnessSource) {
              std::string k = prefix + "material.metallic_roughness";
              Mundus::CtoonHelpers::set_str(overridesObj, k.c_str(), m->metallicRoughnessSource.c_str());
            }
            if (m->aoSource != d->aoSource) {
              std::string k = prefix + "material.ao";
              Mundus::CtoonHelpers::set_str(overridesObj, k.c_str(), m->aoSource.c_str());
            }
            if (m->emissiveSource != d->emissiveSource) {
              std::string k = prefix + "material.emissive";
              Mundus::CtoonHelpers::set_str(overridesObj, k.c_str(), m->emissiveSource.c_str());
            }
          }
        }

        const Mundus::Visibility *v = child.try_get<Mundus::Visibility>();
        if (v && !v->visible) {
          std::string k = prefix + "visible";
          Mundus::CtoonHelpers::set_bool(overridesObj, k.c_str(), 0);
        }
      }
      collectOverrides(root, child, overridesObj);
    }
  });
}

// ── ECS-native CTOON save ────────────────────────────────────────────────

bool Engine::saveEcsCtoon(const char *path) const {
  Auxilia::ctoon_doc doc(CTOON_OBJECT);
  ctoon_value *root = doc.get();

  ctoon_value *scene = ctoon_obj_set(root, "scene");
  scene->type = CTOON_OBJECT;

  glm::vec3 lightDir = m_ecs.get<Mundus::LightDir>().value;
  glm::vec3 lightColor = m_ecs.get<Mundus::LightColor>().value;
  Mundus::CtoonHelpers::set_vec3(scene, "light_dir",
    lightDir.x, lightDir.y, lightDir.z);
  Mundus::CtoonHelpers::set_vec3(scene, "light_color",
    lightColor.x, lightColor.y, lightColor.z);

  ctoon_value *arr = ctoon_obj_set(scene, "entities");
  arr->type = CTOON_ARRAY;

  auto r6 = [](double v) { return std::round(v * 1e6) / 1e6; };

  // Query all entities that are NOT skybox and NOT internal GLTF nodes
  auto q = m_ecs.query_builder<>()
    .with<Mundus::Name>()
    .without<Mundus::GltfInternalNode>()
    .build();

  q.each([&](flecs::iter &it, size_t row) {
    flecs::entity e = it.entity(row);
    const char *eName = e.name();
    if (!eName || (m_ecsSkybox && e == m_ecsSkybox)) return;

    ctoon_value *ev = ctoon_array_push(arr);
    ev->type = CTOON_OBJECT;

    const Mundus::Id *id = e.try_get<Mundus::Id>();
    const char *eid = (id && !id->value.empty()) ? id->value.c_str() : eName;
    Mundus::CtoonHelpers::set_str(ev, "id", eid);
    if (strcmp(eName, eid) != 0)
      Mundus::CtoonHelpers::set_str(ev, "name", eName);

    const Mundus::Visibility *vis = e.try_get<Mundus::Visibility>();
    if (vis) Mundus::CtoonHelpers::set_bool(ev, "visible", vis->visible ? 1 : 0);

    // Parent reference (skip internal parents)
    flecs::entity parent = e.parent();
    if (parent && !parent.has<Mundus::GltfInternalNode>()) {
      const char *pName = parent.name();
      if (pName) {
        std::string ref = std::string("@entity(") + pName + ")";
        Mundus::CtoonHelpers::set_str(ev, "parent", ref.c_str());
      }
    }

    // Transform
    const Mundus::Transform *t = e.try_get<Mundus::Transform>();
    if (t) {
      bool hasPos = t->position != glm::vec3(0.0f);
      bool hasRot = t->rotation != glm::vec3(0.0f);
      bool hasScale = t->scale != glm::vec3(1.0f);
      bool hasAngVel = t->angularVelocity != glm::vec3(0.0f);
      if (hasPos || hasRot || hasScale || hasAngVel) {
        ctoon_value *tf = ctoon_obj_set(ev, "transform");
        tf->type = CTOON_OBJECT;
        if (hasPos)
          Mundus::CtoonHelpers::set_vec3(tf, "pos", r6(t->position.x),
                                         r6(t->position.y), r6(t->position.z));
        if (hasRot) {
          glm::quat q(glm::vec3(t->rotation.x, t->rotation.y, t->rotation.z));
          Mundus::CtoonHelpers::set_quat(tf, "rot", r6(q.x), r6(q.y), r6(q.z), r6(q.w));
        }
        if (hasScale)
          Mundus::CtoonHelpers::set_vec3(tf, "scale", r6(t->scale.x),
                                         r6(t->scale.y), r6(t->scale.z));
        if (hasAngVel)
          Mundus::CtoonHelpers::set_vec3(tf, "angular_velocity",
                                         r6(t->angularVelocity.x),
                                         r6(t->angularVelocity.y),
                                         r6(t->angularVelocity.z));
      }
    }

    // Model vs Mesh reference
    const Mundus::ModelSource *modelSrc = e.try_get<Mundus::ModelSource>();
    if (modelSrc && !modelSrc->value.empty()) {
      std::string ref = "@asset(assets://" + modelSrc->value + ")";
      Mundus::CtoonHelpers::set_str(ev, "model", ref.c_str());

      // Serialize overrides if they exist
      if (countOverrides(e) > 0) {
        ctoon_value *overridesObj = ctoon_obj_set(ev, "overrides");
        overridesObj->type = CTOON_OBJECT;
        collectOverrides(e, e, overridesObj);
      }
    } else {
      // Mesh reference
      const Mundus::MeshSource *ms = e.try_get<Mundus::MeshSource>();
      if (ms && !ms->value.empty()) {
        if (ms->value.compare(0, 11, "@primitive(") == 0) {
          Mundus::CtoonHelpers::set_str(ev, "mesh", ms->value.c_str());
        } else {
          std::string ref = "@asset(assets://" + ms->value + ")";
          Mundus::CtoonHelpers::set_str(ev, "mesh", ref.c_str());
        }
      }

      // Material
      const Mundus::MaterialRef *mr = e.try_get<Mundus::MaterialRef>();
      if (mr && mr->value) {
        ctoon_value *m = ctoon_obj_set(ev, "material");
        m->type = CTOON_OBJECT;
        Mundus::CtoonHelpers::set_str(m, "shader", mr->value->shaderName.c_str());
        Mundus::CtoonHelpers::set_color(m, "base_color",
          mr->value->baseColor.x, mr->value->baseColor.y,
          mr->value->baseColor.z, mr->value->baseColor.w);
        Mundus::CtoonHelpers::set_num(m, "roughness", r6(mr->value->roughness));
        Mundus::CtoonHelpers::set_num(m, "metallic", r6(mr->value->metallic));
        auto saveTex = [&](const char *key, const std::string &src) {
          if (!src.empty()) Mundus::CtoonHelpers::set_str(m, key, src.c_str());
        };
        saveTex("albedo", mr->value->albedoSource);
        saveTex("normal", mr->value->normalSource);
        saveTex("metallic_roughness", mr->value->metallicRoughnessSource);
        saveTex("ao", mr->value->aoSource);
        saveTex("emissive", mr->value->emissiveSource);
      }
    }
  });

  return doc.save_file(path);
}

static std::string stripAssetPrefix(const std::string &s) {
  if (s.compare(0, 16, "@asset(assets://") == 0 && s.back() == ')') {
    return s.substr(16, s.size() - 17);
  }
  if (s.compare(0, 9, "assets://") == 0) {
    return s.substr(9);
  }
  return s;
}

static flecs::entity findEntityByNameOrId(flecs::world &ecs, const std::string &nameOrId) {
  if (nameOrId.empty()) return flecs::entity();

  // 1. Try direct lookup (in case it is a path or at the root)
  flecs::entity e = ecs.lookup(nameOrId.c_str());
  if (e) return e;

  // 2. Search by Mundus::Id
  ecs.query_builder<Mundus::Id>().build()
    .each([&](flecs::entity ent, Mundus::Id &id) {
      if (!e && id.value == nameOrId) {
        e = ent;
      }
    });
  if (e) return e;

  // 3. Search by Mundus::Name
  ecs.query_builder<Mundus::Name>().build()
    .each([&](flecs::entity ent, Mundus::Name &n) {
      if (!e && n.value == nameOrId) {
        e = ent;
      }
    });
  return e;
}

static flecs::entity findDescendantByName(flecs::entity parent, const std::string &name) {
  flecs::entity found;
  parent.children([&](flecs::entity child) {
    if (found) return;
    const Mundus::Name *nm = child.try_get<Mundus::Name>();
    if (nm && nm->value == name) {
      found = child;
      return;
    }
    found = findDescendantByName(child, name);
  });
  return found;
}

// ── ECS-native CTOON load ────────────────────────────────────────────────

bool Engine::loadEcsCtoon(const char *path) {
  Auxilia::ctoon_doc doc;
  if (!doc.load_file(path))
    return false;
  if (!doc.has("scene"))
    return false;

  ctoon_value *root = doc.get();
  ctoon_value *sceneVal = ctoon_obj_get(root, "scene");
  if (!sceneVal || sceneVal->type != CTOON_OBJECT)
    return false;

  {
    std::string schemaErrors;
    if (!Mundus::Schema::validateScene(sceneVal, schemaErrors)) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "Scene '%s' schema violations:\n%s", path,
                  schemaErrors.c_str());
    }
    m_ecs.set<Mundus::LightDir>({Mundus::CtoonHelpers::get_vec3(sceneVal, "light_dir", glm::vec3(0.0f))});
    m_ecs.set<Mundus::LightColor>({Mundus::CtoonHelpers::get_vec3(sceneVal, "light_color", glm::vec3(0.0f))});
  }

  ctoon_value *arr = ctoon_obj_get(sceneVal, "entities");
  if (!arr || arr->type != CTOON_ARRAY)
    return true;

  // Track parent relationships for second pass
  struct PendingParent {
    std::string childName;
    std::string parentName;
  };
  std::vector<PendingParent> pendingParents;

  bool wasDeferred = m_ecs.is_deferred();
  if (wasDeferred) {
    m_ecs.defer_suspend();
  }

  for (size_t i = 0; i < arr->len; ++i) {
    ctoon_value *e = &arr->arr[i];
    if (e->type != CTOON_OBJECT) continue;

    // Read identity
    std::string id, name;
    ctoon_value *idv = ctoon_obj_get(e, "id");
    if (idv && idv->type == CTOON_STRING && idv->str_val)
      id = idv->str_val;
    ctoon_value *nv = ctoon_obj_get(e, "name");
    if (nv && nv->type == CTOON_STRING && nv->str_val)
      name = nv->str_val;
    if (id.empty()) id = name;
    if (name.empty()) name = id;

    flecs::entity ent = findEntityByNameOrId(m_ecs, name);
    if (!ent) {
      ent = m_ecs.entity(name.c_str())
        .set<Mundus::Name>({name})
        .set<Mundus::Id>({id});
    } else {
      ent.set<Mundus::Name>({name})
         .set<Mundus::Id>({id});
    }

    // Visibility
    ctoon_value *vv = ctoon_obj_get(e, "visible");
    ent.set<Mundus::Visibility>({(vv && vv->type == CTOON_BOOL) ? (vv->bool_val != 0) : true});
    ent.set<Mundus::EffectiveVisibility>({true});

    // Transform
    Mundus::Transform tf;
    ctoon_value *tfv = ctoon_obj_get(e, "transform");
    if (tfv && tfv->type == CTOON_OBJECT) {
      tf.position = Mundus::CtoonHelpers::get_vec3(tfv, "pos", tf.position);
      {
        glm::quat q = Mundus::CtoonHelpers::get_quat(tfv, "rot", glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        tf.rotation = glm::eulerAngles(q);
      }
      tf.scale = Mundus::CtoonHelpers::get_vec3(tfv, "scale", tf.scale);
      tf.angularVelocity = Mundus::CtoonHelpers::get_vec3(tfv, "angular_velocity", tf.angularVelocity);
    }
    ent.set<Mundus::Transform>(tf);
    ent.set<Mundus::GlobalTransform>({tf.getLocalMatrix()});

    // Model source & hierarchy loading
    ctoon_value *modelVal = ctoon_obj_get(e, "model");
    std::string modelPath;
    if (modelVal && modelVal->type == CTOON_STRING && modelVal->str_val) {
      const char *s = modelVal->str_val;
      size_t len = strlen(s);
      if (len > 17 && strncmp(s, "@asset(assets://", 16) == 0 && s[len - 1] == ')') {
        modelPath = std::string(s + 16, len - 17);
      }
    }
    if (!modelPath.empty()) {
      ent.set<Mundus::ModelSource>({modelPath});
      std::string fullPath = std::string(CONCORDIA_ASSETS_DIR) + "/" + modelPath;
      m_assetManager->loadGLTF(fullPath, m_ecs, ent.id());
    }

    // Overrides
    ctoon_value *overridesVal = ctoon_obj_get(e, "overrides");
    if (overridesVal && overridesVal->type == CTOON_OBJECT) {
      for (size_t k = 0; k < overridesVal->len; ++k) {
        const char *key = overridesVal->pairs[k].key;
        ctoon_value *val = &overridesVal->pairs[k].val;
        if (!key) continue;

        std::string sKey(key);
        size_t dot1 = sKey.find('.');
        if (dot1 == std::string::npos) continue;
        std::string childName = sKey.substr(0, dot1);
        std::string rest = sKey.substr(dot1 + 1);

        size_t dot2 = rest.find('.');
        std::string section = rest;
        std::string property = "";
        if (dot2 != std::string::npos) {
          section = rest.substr(0, dot2);
          property = rest.substr(dot2 + 1);
        }

        flecs::entity child = findDescendantByName(ent, childName);
        if (!child) {
          SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                      "loadEcsCtoon: override target '%s' not found under '%s'",
                      childName.c_str(), ent.name().c_str());
          continue;
        }

        if (section == "material") {
          const Mundus::MaterialRef *m = child.try_get<Mundus::MaterialRef>();
          std::shared_ptr<Forma::Material> mat;
          if (m && m->value) {
            mat = std::make_shared<Forma::Material>(*m->value);
          } else {
            mat = std::make_shared<Forma::Material>();
          }

          if (property == "shader") {
            if (val->type == CTOON_STRING && val->str_val) {
              mat->shaderName = stripAssetPrefix(val->str_val);
            }
          } else if (property == "base_color") {
            mat->baseColor = Mundus::CtoonHelpers::get_color(overridesVal, key, mat->baseColor);
          } else if (property == "roughness") {
            if (val->type == CTOON_NUMBER) mat->roughness = (float)val->num_val;
          } else if (property == "metallic") {
            if (val->type == CTOON_NUMBER) mat->metallic = (float)val->num_val;
          } else if (property == "albedo") {
            if (val->type == CTOON_STRING && val->str_val) mat->albedoSource = val->str_val;
          } else if (property == "normal") {
            if (val->type == CTOON_STRING && val->str_val) mat->normalSource = val->str_val;
          } else if (property == "metallic_roughness") {
            if (val->type == CTOON_STRING && val->str_val) mat->metallicRoughnessSource = val->str_val;
          } else if (property == "ao") {
            if (val->type == CTOON_STRING && val->str_val) mat->aoSource = val->str_val;
          } else if (property == "emissive") {
            if (val->type == CTOON_STRING && val->str_val) mat->emissiveSource = val->str_val;
          }

          child.set<Mundus::MaterialRef>({mat});
        }
        else if (section == "transform") {
          const Mundus::Transform *currTf = child.try_get<Mundus::Transform>();
          Mundus::Transform tfc = currTf ? *currTf : Mundus::Transform{};
          if (property == "pos") {
            tfc.position = Mundus::CtoonHelpers::get_vec3(overridesVal, key, tfc.position);
          } else if (property == "rot") {
            glm::quat q = Mundus::CtoonHelpers::get_quat(overridesVal, key, glm::quat(1,0,0,0));
            tfc.rotation = glm::eulerAngles(q);
          } else if (property == "scale") {
            tfc.scale = Mundus::CtoonHelpers::get_vec3(overridesVal, key, tfc.scale);
          }
          child.set<Mundus::Transform>(tfc);
          child.set<Mundus::GlobalTransform>({tfc.getLocalMatrix()});
        }
        else if (section == "visible") {
          bool vis = (val->type == CTOON_BOOL) ? (val->bool_val != 0) : true;
          child.set<Mundus::Visibility>({vis});
        }
      }
    }

    // Mesh source
    ctoon_value *meshVal = ctoon_obj_get(e, "mesh");
    std::string meshSource;
    if (meshVal && meshVal->type == CTOON_STRING && meshVal->str_val) {
      const char *s = meshVal->str_val;
      size_t len = strlen(s);
      if (len > 11 && strncmp(s, "@primitive(", 11) == 0 && s[len - 1] == ')') {
        meshSource = s;
      } else if (len > 17 && strncmp(s, "@asset(assets://", 16) == 0 &&
                 s[len - 1] == ')') {
        meshSource = std::string(s + 16, len - 17);
        // If the path is absolute (starts with /), strip CONCORDIA_ASSETS_DIR
        if (!meshSource.empty() && meshSource[0] == '/') {
          std::string ap = std::string(CONCORDIA_ASSETS_DIR) + "/";
          if (meshSource.compare(0, ap.size(), ap) == 0)
            meshSource = meshSource.substr(ap.size());
        }
      }
    }
    if (!meshSource.empty())
      ent.set<Mundus::MeshSource>({meshSource});

    // Material
    ctoon_value *matVal = ctoon_obj_get(e, "material");
    if (matVal && matVal->type == CTOON_OBJECT) {
      auto m = std::make_shared<Forma::Material>();
      ctoon_value *sh = ctoon_obj_get(matVal, "shader");
      if (sh && sh->type == CTOON_STRING && sh->str_val)
        m->shaderName = sh->str_val;
      m->baseColor = Mundus::CtoonHelpers::get_color(matVal, "base_color", m->baseColor);
      ctoon_value *rgh = ctoon_obj_get(matVal, "roughness");
      if (rgh && rgh->type == CTOON_NUMBER)
        m->roughness = (float)rgh->num_val;
      ctoon_value *met = ctoon_obj_get(matVal, "metallic");
      if (met && met->type == CTOON_NUMBER)
        m->metallic = (float)met->num_val;
      auto loadTex = [&](const char *key, std::string &dst) {
        ctoon_value *v = ctoon_obj_get(matVal, key);
        if (v && v->type == CTOON_STRING && v->str_val)
          dst = v->str_val;
      };
      loadTex("albedo", m->albedoSource);
      loadTex("normal", m->normalSource);
      loadTex("metallic_roughness", m->metallicRoughnessSource);
      loadTex("ao", m->aoSource);
      loadTex("emissive", m->emissiveSource);
      ent.set<Mundus::MaterialRef>({m});
    }

    // Default material for meshed entities without explicit material
    if (!meshSource.empty() && !ent.has<Mundus::MaterialRef>()) {
      auto m = std::make_shared<Forma::Material>();
      m->shaderName = "pbr";
      ent.set<Mundus::MaterialRef>({m});
    }

    // Parent reference (deferred to second pass)
    ctoon_value *pv = ctoon_obj_get(e, "parent");
    if (pv && pv->type == CTOON_STRING && pv->str_val) {
      const char *ps = pv->str_val;
      size_t plen = strlen(ps);
      if (plen > 8 && strncmp(ps, "@entity(", 8) == 0 && ps[plen - 1] == ')')
        pendingParents.push_back({name, std::string(ps + 8, plen - 9)});
    }
  }

  // Pass 2: parent-child relationships
  for (auto &pp : pendingParents) {
    flecs::entity child = findEntityByNameOrId(m_ecs, pp.childName);
    flecs::entity parent = findEntityByNameOrId(m_ecs, pp.parentName);
    if (child && parent) {
      if (!child.has(flecs::ChildOf, parent)) {
        child.remove(flecs::ChildOf, flecs::Wildcard);
        child.add(flecs::ChildOf, parent);
      }
    }
  }

  if (wasDeferred) {
    m_ecs.defer_resume();
  }

  SDL_Log("Engine: loaded %zu entities via ECS from %s", arr->len, path);
  return true;
}

Engine::Engine() {
  m_config.load_file((std::string(CONCORDIA_ASSETS_DIR) + "/config/engine.toon").c_str());

  {
    std::string schemaErrors;
    if (!Mundus::Schema::validateConfig(m_config.get(), schemaErrors)) {
      SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                  "Engine config schema violations:\n%s",
                  schemaErrors.c_str());
    }
  }

  const char *title = m_config.get_string("window.title");
  int win_w = (int)m_config.get_number("window.width");
  int win_h = (int)m_config.get_number("window.height");

  m_ecs = flecs::world();
  registerEcsComponents(m_ecs);
  m_ecs.set<Mundus::LightDir>({m_lightDefaultDir});
  m_ecs.set<Mundus::LightColor>({m_lightDefaultColor});

  m_window = std::make_unique<Petra::Window>(title, win_w, win_h);
  m_renderCtx = std::make_unique<Render::Context>(*m_window);
  m_allocator = std::make_unique<Memoria::Allocator>(
      m_renderCtx->getInstance(), m_renderCtx->getPhysicalDevice(),
      m_renderCtx->getDevice());
  m_overlay = std::make_unique<Vigil::Overlay>(*m_window, *m_renderCtx);
  m_input = std::make_unique<Sensus::Input>();
  float fov = (float)m_config.get_number("camera.fov");
  float nearP = (float)m_config.get_number("camera.near");
  float farP = (float)m_config.get_number("camera.far");

  m_camera = std::make_unique<Vista::Camera>();
  m_camera->setPerspective(fov, (float)win_w / (float)win_h, nearP, farP);
  m_camera->mouseSensitivity = (float)m_config.get_number("camera.sensitivity");
  m_camera->moveSpeed = (float)m_config.get_number("camera.speed");
  {
    auto pos = m_config.get_string("camera.default_position");
    if (pos) {
      glm::vec3 v;
      if (sscanf(pos, "@vec3(%f,%f,%f)", &v.x, &v.y, &v.z) == 3)
        m_camera->setPosition(v);
    }
    float yaw = (float)m_config.get_number("camera.default_yaw", -90.0f);
    m_camera->setYaw(yaw);
    float minP = (float)m_config.get_number("camera.min_pitch", -89.0f);
    float maxP = (float)m_config.get_number("camera.max_pitch", 89.0f);
    m_camera->setPitchClamp(minP, maxP);
  }

  m_exposure = (float)m_config.get_number("renderer.exposure", 1.0f);
  m_gamma = (float)m_config.get_number("renderer.gamma", 2.2f);

  {
    auto cc = m_config.get_string("renderer.clear_color");
    if (cc) {
      glm::vec4 v;
      if (sscanf(cc, "@color(%f,%f,%f,%f)", &v.x, &v.y, &v.z, &v.w) == 4)
        m_clearColor = v;
    }
  }

  m_maxBindlessTextures =
      (uint32_t)m_config.get_number("renderer.max_bindless_textures", 1024);

  {
    auto sp = m_config.get_string("scene.default_path");
    if (sp) {
      const char *p = sp;
      // Strip "@asset(assets://" prefix and trailing ")"
      if (strncmp(p, "assets://", 9) == 0)
        m_scenePath = std::string(CONCORDIA_ASSETS_DIR) + "/" + (p + 9);
      else
        m_scenePath = p;
    }
  }
  if (m_scenePath.empty())
    m_scenePath = std::string(CONCORDIA_ASSETS_DIR) + "/scenes/default.toon";
  m_autoSave = m_config.get_bool("scene.auto_save", true);

  {
    auto ld = m_config.get_string("lighting.default_direction");
    if (ld && sscanf(ld, "@vec3(%f,%f,%f)",
                     &m_lightDefaultDir.x, &m_lightDefaultDir.y,
                     &m_lightDefaultDir.z) != 3) {
      m_lightDefaultDir = {-0.5f, -1.0f, -0.2f};
    }
    auto lc = m_config.get_string("lighting.default_color");
    if (lc && sscanf(lc, "@vec3(%f,%f,%f)",
                     &m_lightDefaultColor.x, &m_lightDefaultColor.y,
                     &m_lightDefaultColor.z) != 3) {
      m_lightDefaultColor = {1.0f, 1.0f, 1.0f};
    }
  }

  {
    auto ss = m_config.get_string("skybox.default_scale");
    if (ss && sscanf(ss, "@vec3(%f,%f,%f)",
                     &m_skyboxDefaultScale.x, &m_skyboxDefaultScale.y,
                     &m_skyboxDefaultScale.z) != 3) {
      m_skyboxDefaultScale = {10.0f, 10.0f, 10.0f};
    }
  }

  m_debugMode = m_config.get_bool("renderer.debug_mode") ? 1 : 0;

  SDL_Log("Engine: Initializing depth buffer...");
  m_renderCtx->initDepthBuffer(m_allocator->getVma());

  m_perfFreq = SDL_GetPerformanceFrequency();
  m_startCount = SDL_GetPerformanceCounter();

  SDL_Log("Engine: Initializing commands...");
  initCommands();

  SDL_Log("Engine: Initializing asset manager...");
  m_assetManager = std::make_unique<Memoria::AssetManager>(
      *m_allocator, m_renderCtx->getDevice(), m_renderCtx->getGraphicsQueue(),
      m_cmdPool);

  // Load asset manifest (preloads meshes, sets defaults)
  {
    std::string manifestPath =
        std::string(CONCORDIA_ASSETS_DIR) + "/config/assets.toon";
    m_assetManager->loadManifest(manifestPath.c_str(), m_ecs);
  }

  m_descriptors = std::make_unique<Lumen::DescriptorManager>(
      m_renderCtx->getDevice(), *m_allocator);

  SDL_Log("Engine: Initializing descriptors...");
  initDescriptors();

  SDL_Log("Engine: Generating IBL maps...");
  {
    std::string skyboxPath = m_assetManager->getManifest().defaultSkybox;
    if (skyboxPath.empty())
      skyboxPath = "images/skybox/cubemap/Cubemap_Sky_01-512x512.png";
    std::string fullPath =
        std::string(CONCORDIA_ASSETS_DIR) + "/" + skyboxPath;
    m_iblMaps = Lumen::IBLGenerator::generate(
        *m_allocator, m_renderCtx->getDevice(),
        m_renderCtx->getGraphicsQueue(), m_cmdPool, fullPath);
  }
  if (m_iblMaps.irradianceMap) {
    m_descriptors->updateIBL(m_iblMaps.irradianceMap->view,
                              m_iblMaps.prefilterMap->view,
                              m_iblMaps.brdfLUT->view);
  }

  SDL_Log("Engine: Initializing shader registry...");
  initPipeline();
  SDL_Log("Engine: Initializing framebuffers...");
  initFramebuffers();
  SDL_Log("Engine: Initializing sync...");
  initSync();
  SDL_Log("Engine: Initializing mesh...");
  initMesh();

  // Final bindless update after ALL initial assets are loaded
  updateBindlessDescriptorSet();

  // Load saved scene if available
  if (FILE *f = fopen(m_scenePath.c_str(), "r")) {
    fclose(f);
    SDL_Log("Engine: About to load scene...");
    loadEcsCtoon(m_scenePath.c_str());
    SDL_Log("Engine: Loaded scene from %s", m_scenePath.c_str());
    SDL_Log("Engine: Starting resolveSceneMeshes...");
    resolveSceneMeshes();
    SDL_Log("Engine: resolveSceneMeshes done.");
  } else {
    SDL_Log("Engine: No saved scene found, using test scene");
  }

  SDL_Log("Engine: Constructor finished.");
}

Engine::~Engine() {
  if (m_autoSave)
    saveEcsCtoon(m_scenePath.c_str());

  VkDevice device = m_renderCtx->getDevice();
  if (device) {
    vkDeviceWaitIdle(device);

    // Explicitly release assets before destroying allocator
    m_skyboxTexture.reset();

    if (m_allocator) {
      if (m_iblMaps.irradianceMap) {
        m_iblMaps.irradianceMap->destroy(*m_allocator, device);
        m_iblMaps.irradianceMap.reset();
      }
      if (m_iblMaps.prefilterMap) {
        m_iblMaps.prefilterMap->destroy(*m_allocator, device);
        m_iblMaps.prefilterMap.reset();
      }
      if (m_iblMaps.brdfLUT) {
        m_iblMaps.brdfLUT->destroy(*m_allocator, device);
        m_iblMaps.brdfLUT.reset();
      }

      // Cleanup material parameters Vulkan resources
      auto q = m_ecs.query_builder<>()
        .with<Mundus::MaterialRef>()
        .build();
      q.each([&](flecs::entity e) {
        auto *mr = e.try_get<Mundus::MaterialRef>();
        if (mr && mr->value) {
          cleanupMaterialResources(mr->value);
        }
      });

      m_descriptors.reset(); // destroys descriptor pool, layouts, UBO
      m_renderCtx->cleanupDepthBuffer(m_allocator->getVma());
    }

    if (m_assetManager) {
      m_assetManager.reset();
    }

    for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
      vkDestroyFence(device, m_inFlight[i], nullptr);
      vkDestroySemaphore(device, m_renderDone[i], nullptr);
      vkDestroySemaphore(device, m_imageAvail[i], nullptr);
    }

    vkDestroyCommandPool(device, m_cmdPool, nullptr);

    if (m_shaderRegistry) {
      m_shaderRegistry->destroy(device);
    }

    m_sampler.destroy(device);

    cleanupSwapchainResources();
  }
}

std::vector<std::string> Engine::getShaderNames() const {
  return m_shaderRegistry ? m_shaderRegistry->getPipelineNames()
                          : std::vector<std::string>();
}

static inline const char *obj_str(ctoon_value *obj, const char *key) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  return v && v->type == CTOON_STRING ? v->str_val : nullptr;
}
static inline double obj_num(ctoon_value *obj, const char *key, double def = 0.0) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  return v && v->type == CTOON_NUMBER ? v->num_val : def;
}
static inline bool obj_bool(ctoon_value *obj, const char *key, bool def = false) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  return v && v->type == CTOON_BOOL ? v->bool_val : def;
}

static VkCompareOp parseCompareOp(const std::string &s) {
  if (s == "LESS") return VK_COMPARE_OP_LESS;
  if (s == "ALWAYS") return VK_COMPARE_OP_ALWAYS;
  if (s == "EQUAL") return VK_COMPARE_OP_EQUAL;
  if (s == "NEVER") return VK_COMPARE_OP_NEVER;
  if (s == "GREATER") return VK_COMPARE_OP_GREATER;
  if (s == "NOT_EQUAL") return VK_COMPARE_OP_NOT_EQUAL;
  return VK_COMPARE_OP_LESS;
}

void Engine::initPipeline() {
  m_shaderRegistry = std::make_unique<Lumen::ShaderRegistry>();
}

static std::string resolveAssetPath(const std::string &s) {
  if (s.compare(0, 16, "@asset(assets://") == 0 && s.back() == ')')
    return std::string(CONCORDIA_ASSETS_DIR) + "/" + s.substr(16, s.size() - 17);
  else if (s.compare(0, 9, "assets://") == 0)
    return std::string(CONCORDIA_ASSETS_DIR) + "/" + s.substr(9);
  return s;
}

void Engine::initMaterialResources(std::shared_ptr<Forma::Material> mat) {
  if (!mat) return;

  // Default to pbr if no shader name
  std::string path = mat->shaderName;
  if (path.empty() || path == "pbr") {
    path = "assets://shaders/pbr.toon";
  } else if (path == "skybox") {
    path = "assets://shaders/skybox.toon";
  } else if (path == "skybox_hdri") {
    path = "assets://shaders/skybox_hdri.toon";
  } else if (path == "pbr_toon") {
    path = "assets://shaders/pbr_toon.toon";
  } else if (path == "transparent") {
    path = "assets://shaders/transparent.toon";
  }

  std::string resolved = resolveAssetPath(path);
  if (!mat->shaderManifestPath.empty() && mat->shaderManifestPath != resolved) {
    cleanupMaterialResources(mat);
  }

  if (mat->paramBuffer != VK_NULL_HANDLE) return; // already initialized

  mat->shaderManifestPath = resolved;

  // Load the shader asset
  std::shared_ptr<Mundus::ShaderAsset> shader = m_assetManager->loadShader(resolved);
  if (!shader) {
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "initMaterialResources: Failed to load shader %s", resolved.c_str());
    return;
  }

  // Set default param values
  updateMaterialPipelineDefaults(mat.get());

  VkDevice device = m_renderCtx->getDevice();
  uint32_t size = shader->paramSize;
  if (size == 0) size = 16; // non-zero size required

  // Create UBO Buffer
  VmaAllocation alloc;
  m_allocator->createBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                            VMA_MEMORY_USAGE_AUTO, mat->paramBuffer, alloc,
                            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
  mat->paramAllocation = alloc;

  // Map buffer to paramData
  void *mapped = nullptr;
  vmaMapMemory(m_allocator->getVma(), alloc, &mapped);
  if (mapped && !mat->paramData.empty()) {
    std::memcpy(mapped, mat->paramData.data(), mat->paramData.size());
  }
  vmaUnmapMemory(m_allocator->getVma(), alloc);

  // Create Descriptor Pool for the material
  VkDescriptorPoolSize poolSize{};
  poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSize.descriptorCount = 1;

  VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolInfo.maxSets = 1;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = &poolSize;
  VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &mat->descriptorPool));

  // Allocate Descriptor Set
  VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.descriptorPool = mat->descriptorPool;
  allocInfo.descriptorSetCount = 1;
  VkDescriptorSetLayout layout = m_descriptors->getMaterialParamLayout();
  allocInfo.pSetLayouts = &layout;
  VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &mat->descriptorSet));

  // Write descriptor set
  VkDescriptorBufferInfo bufInfo{};
  bufInfo.buffer = mat->paramBuffer;
  bufInfo.offset = 0;
  bufInfo.range = size;

  VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  write.dstSet = mat->descriptorSet;
  write.dstBinding = 0;
  write.dstArrayElement = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  write.pBufferInfo = &bufInfo;

  vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void Engine::cleanupMaterialResources(std::shared_ptr<Forma::Material> mat) {
  if (!mat) return;
  VkDevice device = m_renderCtx->getDevice();
  if (device == VK_NULL_HANDLE) return;

  if (mat->paramBuffer != VK_NULL_HANDLE) {
    m_allocator->destroyBuffer(mat->paramBuffer, (VmaAllocation)mat->paramAllocation);
    mat->paramBuffer = VK_NULL_HANDLE;
    mat->paramAllocation = nullptr;
  }
  if (mat->descriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, mat->descriptorPool, nullptr);
    mat->descriptorPool = VK_NULL_HANDLE;
    mat->descriptorSet = VK_NULL_HANDLE;
  }
}

void Engine::updateMaterialPipelineDefaults(Forma::Material *material) {
  if (!material || material->shaderManifestPath.empty()) return;
  auto shader = m_assetManager->loadShader(material->shaderManifestPath);
  if (!shader) return;

  if (material->paramData.size() < shader->paramSize) {
    material->paramData.resize(shader->paramSize, 0);
  }

  for (auto &m : shader->paramMembers) {
    if (m.name == "baseColor" || m.name == "base_color" || m.name == "albedo") {
      glm::vec4 color = material->baseColor;
      if (m.size == sizeof(glm::vec4)) {
        std::memcpy(material->paramData.data() + m.offset, &color, sizeof(glm::vec4));
      }
    } else if (m.name == "roughness") {
      float roughness = material->roughness;
      std::memcpy(material->paramData.data() + m.offset, &roughness, sizeof(float));
    } else if (m.name == "metallic") {
      float metallic = material->metallic;
      std::memcpy(material->paramData.data() + m.offset, &metallic, sizeof(float));
    } else if (m.name == "albedoMap" || m.name == "albedo_tex" || m.name == "albedo" || (m.type == "sampler2D" && m.name.find("albedo") != std::string::npos)) {
      uint32_t idx = material->albedoIdx;
      std::memcpy(material->paramData.data() + m.offset, &idx, sizeof(uint32_t));
    } else if (m.name == "normalMap" || m.name == "normal_tex" || m.name == "normal" || (m.type == "sampler2D" && m.name.find("normal") != std::string::npos)) {
      uint32_t idx = material->normalIdx;
      std::memcpy(material->paramData.data() + m.offset, &idx, sizeof(uint32_t));
    } else if (m.name == "metallicRoughnessMap" || m.name == "mrMap" || m.name == "metallic_roughness" || (m.type == "sampler2D" && (m.name.find("metallic") != std::string::npos || m.name.find("roughness") != std::string::npos))) {
      uint32_t idx = material->metallicRoughnessIdx;
      std::memcpy(material->paramData.data() + m.offset, &idx, sizeof(uint32_t));
    } else if (m.name == "aoMap" || m.name == "ao_tex" || m.name == "ao" || (m.type == "sampler2D" && m.name.find("ao") != std::string::npos)) {
      uint32_t idx = material->aoIdx;
      std::memcpy(material->paramData.data() + m.offset, &idx, sizeof(uint32_t));
    } else if (m.name == "emissiveMap" || m.name == "emissive_tex" || m.name == "emissive" || (m.type == "sampler2D" && m.name.find("emissive") != std::string::npos)) {
      uint32_t idx = material->emissiveIdx;
      std::memcpy(material->paramData.data() + m.offset, &idx, sizeof(uint32_t));
    } else {
      auto it = shader->paramDefaults.find(m.name);
      if (it != shader->paramDefaults.end()) {
        std::string valStr = it->second;
        if (m.type == "float") {
          float f = std::stof(valStr);
          std::memcpy(material->paramData.data() + m.offset, &f, sizeof(float));
        } else if (m.type == "vec4") {
          glm::vec4 v(1.0f);
          int parsed = std::sscanf(valStr.c_str(), "%f,%f,%f,%f", &v.x, &v.y, &v.z, &v.w);
          if (parsed < 4) {
            std::sscanf(valStr.c_str(), "@vec4(%f,%f,%f,%f)", &v.x, &v.y, &v.z, &v.w);
          }
          std::memcpy(material->paramData.data() + m.offset, &v, sizeof(glm::vec4));
        } else if (m.type == "vec3") {
          glm::vec3 v(1.0f);
          int parsed = std::sscanf(valStr.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z);
          if (parsed < 3) {
            std::sscanf(valStr.c_str(), "@vec3(%f,%f,%f)", &v.x, &v.y, &v.z);
          }
          std::memcpy(material->paramData.data() + m.offset, &v, sizeof(glm::vec3));
        } else if (m.type == "int" || m.type == "uint" || m.type == "sampler2D") {
          uint32_t u = std::stoul(valStr);
          std::memcpy(material->paramData.data() + m.offset, &u, sizeof(uint32_t));
        }
      }
    }
  }
}
void Engine::initDescriptors() {
  VkDevice device = m_renderCtx->getDevice();
  m_sampler.init(device);

  SDL_Log("Engine: Descriptors: Loading skybox textures...");
  auto skyboxAssets = m_assetManager->scanSkyboxes();
  for (auto &sa : skyboxAssets) {
    std::shared_ptr<Memoria::TextureAsset> tex;
    if (sa.isHDR) {
      tex = m_assetManager->loadHDR(sa.path);
    } else {
      tex = m_assetManager->loadCubemapFromCross(sa.path);
    }
    std::string pipelineName = sa.isHDR ? "skybox_hdri" : "skybox";
    m_skyboxOptions.push_back({sa.name, pipelineName, tex, sa.path});
  }

  m_skyboxTexture = m_skyboxOptions[0].texture;
  m_selectedSkybox = 0;

  // Collect initial texture views for bindless
  auto &textures = m_assetManager->getLoadedTextures();
  std::vector<VkImageView> initialViews;
  for (auto &t : textures)
    initialViews.push_back(t->view);

  m_descriptors->init(m_sampler.getSampler(), m_maxBindlessTextures,
                      initialViews);

  // Write initial skybox descriptor
  m_descriptors->updateSkybox(m_skyboxTexture->view);

  SDL_Log("Engine: Descriptors: Done.");
}

void Engine::updateBindlessDescriptorSet() {
  auto &textures = m_assetManager->getLoadedTextures();
  m_lastLoadedTextureCount = textures.size();
  std::vector<VkImageView> views;
  for (auto &t : textures)
    views.push_back(t->view);
  m_descriptors->updateBindless(views);
}

void Engine::updateIBLDescriptors() {
  if (!m_iblMaps.irradianceMap || !m_iblMaps.prefilterMap ||
      !m_iblMaps.brdfLUT) {
    SDL_Log("Engine: updateIBLDescriptors: IBL maps not ready, skipping.");
    return;
  }
  m_descriptors->updateIBL(m_iblMaps.irradianceMap->view,
                            m_iblMaps.prefilterMap->view,
                            m_iblMaps.brdfLUT->view);
  SDL_Log("Engine: IBL descriptors updated.");
}

void Engine::updateSkyboxDescriptor() {
  m_descriptors->updateSkybox(m_skyboxTexture->view);
  SDL_Log("Engine: Skybox descriptor updated.");
}

void Engine::cleanupSwapchainResources() {
  VkDevice device = m_renderCtx->getDevice();
  for (auto fb : m_framebuffers)
    vkDestroyFramebuffer(device, fb, nullptr);
  m_framebuffers.clear();
}

void Engine::initFramebuffers() {
  const auto &swapViews = m_renderCtx->getSwapchainImageViews();
  auto swapExtent = m_renderCtx->getSwapchainExtent();
  VkDevice device = m_renderCtx->getDevice();
  VkRenderPass renderPass = m_renderCtx->getRenderPass();

  m_framebuffers.resize(swapViews.size());
  for (size_t i = 0; i < swapViews.size(); ++i) {
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = renderPass;
    fci.attachmentCount = 2;
    VkImageView attachments[] = {swapViews[i],
                                 m_renderCtx->getDepthImageView()};
    fci.pAttachments = attachments;
    fci.width = swapExtent.width;
    fci.height = swapExtent.height;
    fci.layers = 1;
    VK_CHECK(vkCreateFramebuffer(device, &fci, nullptr, &m_framebuffers[i]));
  }
}

void Engine::initCommands() {
  VkDevice device = m_renderCtx->getDevice();
  VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpci.queueFamilyIndex = m_renderCtx->getGraphicsQueueFamily();
  cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  VK_CHECK(vkCreateCommandPool(device, &cpci, nullptr, &m_cmdPool));

  m_cmdBufs.resize(MAX_FRAMES_IN_FLIGHT);
  VkCommandBufferAllocateInfo ai{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ai.commandPool = m_cmdPool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
  VK_CHECK(vkAllocateCommandBuffers(device, &ai, m_cmdBufs.data()));
}

void Engine::initSync() {
  VkDevice device = m_renderCtx->getDevice();
  m_imageAvail.resize(MAX_FRAMES_IN_FLIGHT);
  m_renderDone.resize(MAX_FRAMES_IN_FLIGHT);
  m_inFlight.resize(MAX_FRAMES_IN_FLIGHT);

  VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    VK_CHECK(vkCreateSemaphore(device, &sci, nullptr, &m_imageAvail[i]));
    VK_CHECK(vkCreateSemaphore(device, &sci, nullptr, &m_renderDone[i]));
    VK_CHECK(vkCreateFence(device, &fci, nullptr, &m_inFlight[i]));
  }
}

void Engine::recreateSwapchain() {
  int w = 0, h = 0;
  m_window->getPixelSize(w, h);
  while (w == 0 || h == 0) {
    m_window->getPixelSize(w, h);
    SDL_WaitEvent(nullptr);
  }

  vkDeviceWaitIdle(m_renderCtx->getDevice());

  cleanupSwapchainResources();
  m_renderCtx->cleanupDepthBuffer(m_allocator->getVma());
  m_renderCtx->recreateSwapchain(*m_window);
  m_renderCtx->initDepthBuffer(m_allocator->getVma());

  auto extent = m_renderCtx->getSwapchainExtent();
  m_camera->setPerspective(
      (float)m_config.get_number("camera.fov", 45.0),
      static_cast<float>(extent.width) / static_cast<float>(extent.height),
      (float)m_config.get_number("camera.near", 0.1),
      (float)m_config.get_number("camera.far", 100.0));
  initFramebuffers();
}

void Engine::drawFrame() {
  if (m_needsResize) {
    SDL_Log("Engine: drawFrame: needsResize");
    recreateSwapchain();
    m_needsResize = false;
    return;
  }

  // Check if new textures were loaded since the last frame and update bindless descriptors
  if (m_assetManager->getLoadedTextures().size() != m_lastLoadedTextureCount) {
    updateBindlessDescriptorSet();
  }

  VkDevice device = m_renderCtx->getDevice();
  VkSwapchainKHR swapchain = m_renderCtx->getSwapchain();
  VkQueue graphicsQueue = m_renderCtx->getGraphicsQueue();

  VkFence fence = m_inFlight[m_frameIndex];
  VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));

  uint32_t imageIdx;
  VkResult r = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                     m_imageAvail[m_frameIndex], VK_NULL_HANDLE,
                                     &imageIdx);
  if (r == VK_ERROR_OUT_OF_DATE_KHR) {
    m_needsResize = true;
    return;
  }
  if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
    throw std::runtime_error("vkAcquireNextImageKHR failed");

  VK_CHECK(vkResetFences(device, 1, &fence));

  VkCommandBuffer cb = m_cmdBufs[m_frameIndex];
  VK_CHECK(vkResetCommandBuffer(cb, 0));

  // Calculate stats
  static uint64_t lastCount = SDL_GetPerformanceCounter();
  uint64_t currentCount = SDL_GetPerformanceCounter();
  float frameTimeMs =
      (float)(currentCount - lastCount) * 1000.0f / (float)m_perfFreq;
  lastCount = currentCount;

  // Camera update — DEBUG LOG
  static int dbgFrame = 0;
  dbgFrame++;
  if (m_input->isCaptured()) {
    SDL_Log("DBG [%d] drawFrame: capture active, processing input", dbgFrame);
    bool fw = m_input->isKeyPressed(SDLK_W);
    bool bw = m_input->isKeyPressed(SDLK_S);
    bool lf = m_input->isKeyPressed(SDLK_A);
    bool rt = m_input->isKeyPressed(SDLK_D);
    bool up = m_input->isKeyPressed(SDLK_SPACE);
    bool dn = m_input->isKeyPressed(SDLK_LSHIFT) ||
              m_input->isKeyPressed(SDLK_RSHIFT);

    m_camera->processKeyboard(fw, bw, lf, rt, up, dn, frameTimeMs / 1000.0f);
    m_camera->processMouse(m_input->getMouseDelta());
  }

  // Run ECS systems
  SDL_Log("DBG [%d] drawFrame: before TransformSystem", dbgFrame);
  Mundus::TransformSystem::update(m_ecs, frameTimeMs / 1000.0f);
  SDL_Log("DBG [%d] drawFrame: after TransformSystem, before VisibilitySystem", dbgFrame);
  Mundus::VisibilitySystem::update(m_ecs);
  SDL_Log("DBG [%d] drawFrame: after VisibilitySystem", dbgFrame);

  Vigil::DebugStats stats{};
  stats.fps = 1000.0f / (frameTimeMs > 1e-6f ? frameTimeMs : 1.0f);
  stats.frameTime = frameTimeMs;
  SDL_Log("DBG [%d] drawFrame: before stats query", dbgFrame);
  {
    // Stats from ECS
    auto sq = m_ecs.query_builder<>()
      .with<Mundus::EffectiveVisibility>()
      .with<Mundus::MeshAssetRef>()
      .build();
    stats.drawCalls = 0;
    stats.vertexCount = 0;
    sq.each([&](flecs::iter &it, size_t row) {
      flecs::entity e = it.entity(row);
      auto *ev = e.try_get<Mundus::EffectiveVisibility>();
      auto *mr = e.try_get<Mundus::MeshAssetRef>();
      if (!ev || !mr || !ev->visible || !mr->value) return;
      stats.drawCalls++;
      stats.vertexCount += mr->value->vertexCount;
    });
  }
  SDL_Log("DBG [%d] drawFrame: after stats query", dbgFrame);
  stats.cameraPos = m_camera->getPosition();
  stats.cameraFront = m_camera->getFront();
  stats.cameraSpeed = &m_camera->moveSpeed;
  stats.cameraSens = &m_camera->mouseSensitivity;
  bool wantCapture = m_input->isCaptured();
  stats.captureMouse = &wantCapture;

  // UI overlay
  m_overlay->beginFrame();
  std::vector<const char *> skyboxNamePtrs;
  skyboxNamePtrs.reserve(m_skyboxOptions.size());
  for (auto &opt : m_skyboxOptions)
    skyboxNamePtrs.push_back(opt.name.c_str());
  auto shaderNames = getShaderNames();
  std::vector<const char *> shaderNamePtrs;
  shaderNamePtrs.reserve(shaderNames.size());
  for (auto &sn : shaderNames)
    shaderNamePtrs.push_back(sn.c_str());
  m_overlay->drawUI(*m_renderCtx, stats, m_ecs, *m_assetManager,
                    m_sampler.getSampler(), &m_debugMode,
                    &m_selectedSkybox, static_cast<uint32_t>(m_skyboxOptions.size()),
                    skyboxNamePtrs.data(),
                    static_cast<uint32_t>(shaderNamePtrs.size()),
                    shaderNamePtrs.data());

  // Sync selected entity's material changes (if edited in UI) to GPU
  flecs::entity selEnt = m_overlay->getSelectedEntity();
  if (selEnt.is_alive()) {
    auto *matRef = selEnt.try_get<Mundus::MaterialRef>();
    if (matRef && matRef->value) {
      syncMaterialIndices(matRef->value);
    }
  }

  if (wantCapture != m_input->isCaptured()) {
    SDL_Log("DBG [%d] drawFrame: setCapture(%d)", dbgFrame, wantCapture);
    m_input->setCapture(wantCapture, m_window->getHandle());
  }

  // Viewport click selection (only when NOT hovering any ImGui window and NOT in viewing mode)
  if (!ImGui::GetIO().WantCaptureMouse &&
      !m_input->isCaptured() &&
      m_input->isMousePressed(Sensus::MouseButton::Left)) {
    pickEntityAtMouse();
  }

  // Handle focus camera request from inspector context menu
  if (stats.hasFocusTarget) {
    glm::vec3 target = stats.focusTarget;
    glm::vec3 eye = target + glm::vec3(2.0f, 1.0f, 2.0f);
    m_camera->lookAt(eye, target, glm::vec3(0.0f, 1.0f, 0.0f));
    stats.hasFocusTarget = false;
  }

  // Update skybox if selection changed
  if (m_selectedSkybox < m_skyboxOptions.size()) {
    auto &opt = m_skyboxOptions[m_selectedSkybox];
    if (m_skyboxTexture != opt.texture) {
      SDL_Log("DBG [%d] drawFrame: starting skybox update", dbgFrame);
      m_skyboxTexture = opt.texture;
      // Update skybox material through ECS
      if (m_ecsSkybox) {
        auto mat = std::make_shared<Forma::Material>();
        mat->shaderName = opt.pipelineName;
        mat->albedo = m_skyboxTexture;
        m_ecsSkybox.set<Mundus::MaterialRef>({mat});
        syncMaterialIndices(mat);
      }
      updateSkyboxDescriptor();

      // Regenerate IBL maps for the new skybox
      VkQueue gfxQueue = m_renderCtx->getGraphicsQueue();
      if (m_iblMaps.irradianceMap)
        m_iblMaps.irradianceMap->destroy(*m_allocator, m_renderCtx->getDevice());
      if (m_iblMaps.prefilterMap)
        m_iblMaps.prefilterMap->destroy(*m_allocator, m_renderCtx->getDevice());
      if (m_iblMaps.brdfLUT)
        m_iblMaps.brdfLUT->destroy(*m_allocator, m_renderCtx->getDevice());

      if (opt.pipelineName == "skybox") {
        m_iblMaps = Lumen::IBLGenerator::generate(
            *m_allocator, m_renderCtx->getDevice(), gfxQueue, m_cmdPool,
            opt.sourcePath);
      } else {
        m_iblMaps = Lumen::IBLGenerator::generateFromHDR(
            *m_allocator, m_renderCtx->getDevice(), gfxQueue, m_cmdPool,
            opt.sourcePath);
      }
      updateIBLDescriptors();
      SDL_Log("DBG [%d] drawFrame: skybox update done", dbgFrame);
    }
  }

  SDL_Log("DBG [%d] drawFrame: before UBO update", dbgFrame);
  // Update Global UBO
  GlobalUBO ubo{};
  ubo.lightDir = glm::vec4(m_ecs.get<Mundus::LightDir>().value, 0.0f);
  ubo.viewPos = glm::vec4(m_camera->getPosition(), 1.0f);
  ubo.lightColor = glm::vec4(m_ecs.get<Mundus::LightColor>().value, 1.0f);
  ubo.view = m_camera->getView();
  ubo.proj = m_camera->getProj();
  ubo.exposure = m_exposure;
   ubo.gamma = m_gamma;
   memcpy(m_descriptors->getUBOMapped(), &ubo, sizeof(GlobalUBO));
 
   VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  VK_CHECK(vkBeginCommandBuffer(cb, &bi));

  auto swapExtent = m_renderCtx->getSwapchainExtent();

  VkClearValue clearValues[2];
  clearValues[0].color = {{m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a}};
  clearValues[1].depthStencil = {1.0f, 0};

  VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  rpbi.renderPass = m_renderCtx->getRenderPass();
  rpbi.framebuffer = m_framebuffers[imageIdx];
  rpbi.renderArea.extent = swapExtent;
  rpbi.clearValueCount = 2;
   rpbi.pClearValues = clearValues;
 
   vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport vp{};
  vp.width = static_cast<float>(swapExtent.width);
  vp.height = static_cast<float>(swapExtent.height);
  vp.minDepth = 0.0f;
  vp.maxDepth = 1.0f;
  vkCmdSetViewport(cb, 0, 1, &vp);

  VkRect2D scissor{{0, 0}, swapExtent};
  vkCmdSetScissor(cb, 0, 1, &scissor);

  Lumen::Pipeline *lastPipeline = nullptr;
  VkDeviceSize offset = 0;

  struct RenderItem {
    flecs::entity entity;
    const Mundus::GlobalTransform* globalTransform;
    std::shared_ptr<Memoria::MeshAsset> mesh;
    std::shared_ptr<Forma::Material> material;
    std::shared_ptr<Mundus::ShaderAsset> shader;
  };

  std::vector<RenderItem> opaqueItems;
  std::vector<RenderItem> transparentItems;

  // Render using ECS query
  SDL_Log("DBG [%d] drawFrame: building render query", dbgFrame);
  auto renderQuery = m_ecs.query_builder<>()
    .with<Mundus::GlobalTransform>()
    .with<Mundus::MeshAssetRef>()
    .with<Mundus::MaterialRef>()
    .with<Mundus::EffectiveVisibility>()
    .build();

  renderQuery.each([&](flecs::iter &it, size_t row) {
    flecs::entity e = it.entity(row);
    auto *gt = e.try_get<Mundus::GlobalTransform>();
    auto *mr = e.try_get<Mundus::MeshAssetRef>();
    auto *mat = e.try_get<Mundus::MaterialRef>();
    auto *ev = e.try_get<Mundus::EffectiveVisibility>();
    if (!gt || !mr || !mat || !ev) return;
    if (!ev->visible) return;
    auto &mesh = mr->value;
    auto &material = mat->value;
    if (!mesh || !material) return;

    if (material->shaderManifestPath.empty()) {
      initMaterialResources(material);
    }

    auto shader = m_assetManager->loadShader(material->shaderManifestPath);
    if (!shader) {
      SDL_Log("Engine: drawFrame: skipping entity (failed to load shader %s)",
              material->shaderManifestPath.c_str());
      return;
    }

    RenderItem item{e, gt, mesh, material, shader};
    if (shader->blendEnable) {
      transparentItems.push_back(item);
    } else {
      opaqueItems.push_back(item);
    }
  });

  auto renderItems = [&](const std::vector<RenderItem> &items) {
    for (const auto &item : items) {
      Lumen::PipelineKey key{};
      key.vertexShaderPath = item.shader->vertPath;
      key.fragmentShaderPath = item.shader->fragPath;
      key.cullMode = item.shader->cullMode;
      key.frontFace = item.shader->frontFace;
      key.depthTest = item.shader->depthTest;
      key.depthWriteEnable = item.shader->depthWrite;
      key.depthCompareOp = item.shader->depthCompareOp;
      key.blendEnable = item.shader->blendEnable;

      std::vector<VkDescriptorSetLayout> layouts = {
        m_descriptors->getGlobalLayout(),
        m_descriptors->getMaterialLayout(),
        m_descriptors->getMaterialParamLayout()
      };

      auto drawWithKey = [&](const Lumen::PipelineKey &drawKey) {
        auto pipeline = m_shaderRegistry->getOrCreatePipeline(*m_renderCtx, drawKey, layouts);
        if (!pipeline) {
          SDL_Log("Engine: drawFrame: skipping entity (failed to create/cache pipeline for %s)",
                  item.material->shaderManifestPath.c_str());
          return;
        }

        if (pipeline.get() != lastPipeline) {
          pipeline->bind(cb);
          vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline->getLayout(), 0, 1,
                                  &m_descriptors->getGlobalSet(), 0, nullptr);
          vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline->getLayout(), 1, 1,
                                  &m_descriptors->getBindlessSet(), 0, nullptr);
          lastPipeline = pipeline.get();
        }

        if (item.material->descriptorSet != VK_NULL_HANDLE) {
          vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  pipeline->getLayout(), 2, 1,
                                  &item.material->descriptorSet, 0, nullptr);
        }

        PushConstants pc{};
        if (item.material->shaderName == "skybox" ||
            item.material->shaderName == "skybox_hdri") {
          pc.model = glm::translate(glm::mat4(1.0f), m_camera->getPosition());
        } else {
          pc.model = item.globalTransform->value;
        }
        pc.debugMode = m_debugMode;

        vkCmdPushConstants(cb, pipeline->getLayout(),
                          VK_SHADER_STAGE_VERTEX_BIT |
                              VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, sizeof(PushConstants), &pc);
        vkCmdBindVertexBuffers(cb, 0, 1, &item.mesh->vertexBuffer, &offset);
        if (item.mesh->indexBuffer) {
          vkCmdBindIndexBuffer(cb, item.mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
          vkCmdDrawIndexed(cb, item.mesh->indexCount, 1, 0, 0, 0);
        } else {
          vkCmdDraw(cb, item.mesh->vertexCount, 1, 0, 0);
        }
      };

      if (key.blendEnable && key.cullMode == VK_CULL_MODE_NONE) {
        Lumen::PipelineKey backKey = key;
        backKey.cullMode = VK_CULL_MODE_FRONT_BIT; // Draws back faces
        drawWithKey(backKey);

        Lumen::PipelineKey frontKey = key;
        frontKey.cullMode = VK_CULL_MODE_BACK_BIT; // Draws front faces
        drawWithKey(frontKey);
      } else {
        drawWithKey(key);
      }
    }
  };

  renderItems(opaqueItems);

  // Sort transparent items back-to-front relative to camera position to avoid blending artifacts
  glm::vec3 cameraPos = m_camera->getPosition();
  std::sort(transparentItems.begin(), transparentItems.end(),
            [cameraPos](const RenderItem &a, const RenderItem &b) {
              glm::vec3 posA(a.globalTransform->value[3]);
              glm::vec3 posB(b.globalTransform->value[3]);
              glm::vec3 diffA = cameraPos - posA;
              glm::vec3 diffB = cameraPos - posB;
              return glm::dot(diffA, diffA) > glm::dot(diffB, diffB);
            });

  renderItems(transparentItems);

  SDL_Log("DBG [%d] drawFrame: before overlay", dbgFrame);
  m_overlay->endFrameAndRecord(cb);
  SDL_Log("DBG [%d] drawFrame: after overlay, before submit", dbgFrame);

  vkCmdEndRenderPass(cb);
  VK_CHECK(vkEndCommandBuffer(cb));

  VkPipelineStageFlags waitStage =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &m_imageAvail[m_frameIndex];
  si.pWaitDstStageMask = &waitStage;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cb;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &m_renderDone[m_frameIndex];
  VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &si, fence));

  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &m_renderDone[m_frameIndex];
  pi.swapchainCount = 1;
  pi.pSwapchains = &swapchain;
  pi.pImageIndices = &imageIdx;
  r = vkQueuePresentKHR(graphicsQueue, &pi);
  if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
    m_needsResize = true;
  else if (r != VK_SUCCESS)
    throw std::runtime_error("vkQueuePresentKHR failed");

  m_frameIndex = (m_frameIndex + 1) % MAX_FRAMES_IN_FLIGHT;
}

void Engine::initMesh() {
  SDL_Log("Engine: initMesh: setting up initial materials...");
  SDL_Log("Engine: initMesh: initial materials setup done.");

  // Create Skybox Entity (ECS only)
  auto mat = std::make_shared<Forma::Material>();
  mat->shaderName = m_skyboxOptions[m_selectedSkybox].pipelineName;
  mat->albedo = m_skyboxTexture;
  syncMaterialIndices(mat);

  m_ecsSkybox = m_ecs.entity("Skybox")
    .set<Mundus::Name>({"Skybox"})
    .set<Mundus::Transform>({glm::vec3(0.0f), glm::vec3(0.0f), m_skyboxDefaultScale, glm::vec3(0.0f)})
    .set<Mundus::Visibility>({true})
    .set<Mundus::GlobalTransform>({glm::mat4(1.0f)})
    .set<Mundus::EffectiveVisibility>({true})
    .set<Mundus::MeshSource>({"@primitive(cube)"})
    .set<Mundus::MeshAssetRef>({m_assetManager->getMesh("@primitive(cube)")})
    .set<Mundus::MaterialRef>({mat});

  // Ensure all preloaded meshes have their material descriptors setup
  // (iterate ECS for any entities already created by loadManifest → loadGLTF)
  auto q = m_ecs.query_builder<>()
    .with<Mundus::MaterialRef>()
    .build();
  q.each([&](flecs::iter &it, size_t row) {
    flecs::entity e = it.entity(row);
    auto &mr = e.get_mut<Mundus::MaterialRef>();
    if (mr.value && mr.value->shaderName == "pbr")
      syncMaterialIndices(mr.value);
  });
}

void Engine::syncMaterialIndices(std::shared_ptr<Forma::Material> mat) {
  if (!mat)
    return;
  auto resolve = [&](std::shared_ptr<Memoria::TextureAsset> &tex,
                     const std::string &source, bool srgb,
                     std::shared_ptr<Memoria::TextureAsset> fallback) {
    if (!tex && !source.empty())
      tex = m_assetManager->resolveTexture(source, srgb);
    if (!tex)
      tex = fallback;
  };
  resolve(mat->albedo, mat->albedoSource, true,
          m_assetManager->getDefaultWhite());
  resolve(mat->normal, mat->normalSource, false,
          m_assetManager->getDefaultNormal());
  resolve(mat->metallicRoughness, mat->metallicRoughnessSource, false,
          m_assetManager->getDefaultWhiteLinear());
  resolve(mat->ao, mat->aoSource, false,
          m_assetManager->getDefaultWhiteLinear());
  resolve(mat->emissive, mat->emissiveSource, true,
          m_assetManager->getDefaultBlack());
  mat->albedoIdx = mat->albedo->textureId;
  mat->normalIdx = mat->normal->textureId;
  mat->metallicRoughnessIdx = mat->metallicRoughness->textureId;
  mat->aoIdx = mat->ao->textureId;
  mat->emissiveIdx = mat->emissive->textureId;

  // Initialize dynamic resources
  initMaterialResources(mat);

  // Upload parameter data to GPU uniform buffer
  if (mat->paramBuffer != VK_NULL_HANDLE && !mat->paramData.empty()) {
    updateMaterialPipelineDefaults(mat.get());
    void *mapped = nullptr;
    vmaMapMemory(m_allocator->getVma(), (VmaAllocation)mat->paramAllocation, &mapped);
    if (mapped) {
      std::memcpy(mapped, mat->paramData.data(), mat->paramData.size());
    }
    vmaUnmapMemory(m_allocator->getVma(), (VmaAllocation)mat->paramAllocation);
  }
}

void Engine::resolveSceneMeshes() {
  auto q = m_ecs.query_builder<>()
    .with<Mundus::MeshSource>()
    .build();

  std::vector<flecs::entity> entities;
  q.each([&](flecs::entity e) {
    entities.push_back(e);
  });

  // First pass: ensure every queried entity has MeshAssetRef & MaterialRef
  // so the second pass can call get_mut() without causing table moves.
  // Only set if missing — don't overwrite existing values (e.g. from manifest).
  for (auto e : entities) {
    if (!e.has<Mundus::MeshAssetRef>())
      e.set<Mundus::MeshAssetRef>({nullptr});
    if (!e.has<Mundus::MaterialRef>())
      e.set<Mundus::MaterialRef>({nullptr});
  }

  // Second pass: resolve mesh sources
  for (auto e : entities) {
    auto &ms = e.get_mut<Mundus::MeshSource>();
    auto &mar = e.get_mut<Mundus::MeshAssetRef>();
    auto &matRef = e.get_mut<Mundus::MaterialRef>();

    if (!ms.value.empty() && !mar.value) {
      mar.value = m_assetManager->getMesh(ms.value);
    }
    if (matRef.value) {
      syncMaterialIndices(matRef.value);
    }
  }

  updateBindlessDescriptorSet();
}

void Engine::pickEntityAtMouse() {
  float mx, my;
  SDL_GetMouseState(&mx, &my);
  int vpW, vpH;
  SDL_GetWindowSize(m_window->getHandle(), &vpW, &vpH);

  float ndcX = (2.0f * mx / vpW - 1.0f);
  float ndcY = (1.0f - 2.0f * my / vpH);
  if (vpW == 0 || vpH == 0) return;

  glm::mat4 invProj = glm::inverse(m_camera->getProj());
  glm::mat4 invView = glm::inverse(m_camera->getView());
  glm::vec4 rayClip(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 rayEye = invProj * rayClip;
  rayEye.z = -1.0f; rayEye.w = 0.0f;
  glm::vec4 rayWorld4 = invView * rayEye;
  glm::vec3 rayDir = glm::normalize(glm::vec3(rayWorld4));
  glm::vec3 rayOrigin = m_camera->getPosition();

  flecs::entity closest;
  float closestDist = 1e30f;

  auto q = m_ecs.query_builder<>()
    .with<Mundus::GlobalTransform>()
    .with<Mundus::MeshAssetRef>()
    .with<Mundus::EffectiveVisibility>()
    .build();
  q.each([&](flecs::iter &it, size_t row) {
    flecs::entity e = it.entity(row);
    const auto *gt = e.try_get<Mundus::GlobalTransform>();
    const auto *mr = e.try_get<Mundus::MeshAssetRef>();
    const auto *ev = e.try_get<Mundus::EffectiveVisibility>();
    if (!gt || !mr || !ev || !ev->visible || !mr->value) return;

    glm::mat4 m = gt->value;
    auto &aabbMin = mr->value->aabbMin;
    auto &aabbMax = mr->value->aabbMax;
    glm::vec3 corners[8] = {
      glm::vec3(m * glm::vec4(aabbMin.x, aabbMin.y, aabbMin.z, 1.0f)),
      glm::vec3(m * glm::vec4(aabbMax.x, aabbMin.y, aabbMin.z, 1.0f)),
      glm::vec3(m * glm::vec4(aabbMin.x, aabbMax.y, aabbMin.z, 1.0f)),
      glm::vec3(m * glm::vec4(aabbMax.x, aabbMax.y, aabbMin.z, 1.0f)),
      glm::vec3(m * glm::vec4(aabbMin.x, aabbMin.y, aabbMax.z, 1.0f)),
      glm::vec3(m * glm::vec4(aabbMax.x, aabbMin.y, aabbMax.z, 1.0f)),
      glm::vec3(m * glm::vec4(aabbMin.x, aabbMax.y, aabbMax.z, 1.0f)),
      glm::vec3(m * glm::vec4(aabbMax.x, aabbMax.y, aabbMax.z, 1.0f)),
    };
    glm::vec3 wMin = corners[0], wMax = corners[0];
    for (int c = 1; c < 8; ++c) {
      wMin = glm::min(wMin, corners[c]);
      wMax = glm::max(wMax, corners[c]);
    }

    auto rayIntersectsAABB = [](glm::vec3 ro, glm::vec3 rd,
                                 glm::vec3 amin, glm::vec3 amax) {
      float tMin = -1e30f, tMax = 1e30f;
      for (int axis = 0; axis < 3; ++axis) {
        float invD = 1.0f / rd[axis];
        float t0 = (amin[axis] - ro[axis]) * invD;
        float t1 = (amax[axis] - ro[axis]) * invD;
        if (invD < 0.0f) std::swap(t0, t1);
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
        if (tMax < tMin) return false;
      }
      return true;
    };

    if (rayIntersectsAABB(rayOrigin, rayDir, wMin, wMax)) {
      float d = glm::distance(rayOrigin, (wMin + wMax) * 0.5f);
      if (d < closestDist) {
        closestDist = d;
        closest = e;
      }
    }
  });

  if (closest)
    m_overlay->setSelectedEntity(closest);
  else
    m_overlay->setSelectedEntity(flecs::entity());
}

void Engine::run() {
  SDL_Log("Engine: run() started.");
  SDL_Event ev;
  while (m_isRunning) {
    m_input->newFrame();

    while (SDL_PollEvent(&ev)) {
      ImGui_ImplSDL3_ProcessEvent(&ev);

      bool imguiCapturesKeyboard = ImGui::GetIO().WantCaptureKeyboard;
      bool imguiCapturesMouse = ImGui::GetIO().WantCaptureMouse;

      if (ev.type == SDL_EVENT_QUIT)
        m_isRunning = false;

      if (ev.type == SDL_EVENT_WINDOW_RESIZED ||
          ev.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        m_needsResize = true;
      }

      // If we are captured, or ImGui doesn't want the event, give it to Sensus
      if (m_input->isCaptured()) {
        if (ev.type == SDL_EVENT_KEY_DOWN && ev.key.key == SDLK_ESCAPE) {
          m_input->setCapture(false, m_window->getHandle());
        } else if (ev.type == SDL_EVENT_MOUSE_WHEEL) {
          m_camera->moveSpeed *= std::pow(1.15f, ev.wheel.y);
          m_camera->moveSpeed = std::clamp(m_camera->moveSpeed, 0.1f, 100.0f);
        } else {
          m_input->processEvent(ev);
        }
      } else {
        // Send to Sensus only if ImGui isn't using it
        bool isKeyboardEvent =
            (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP);
        bool isMouseEvent = (ev.type == SDL_EVENT_MOUSE_MOTION ||
                             ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                             ev.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                             ev.type == SDL_EVENT_MOUSE_WHEEL);

        if ((isKeyboardEvent && !imguiCapturesKeyboard) ||
            (isMouseEvent && !imguiCapturesMouse)) {
          m_input->processEvent(ev);
        }
      }

      // Blender-style keyboard shortcuts
      Vigil::processEditorKeys(ev, imguiCapturesKeyboard,
                               imguiCapturesMouse, m_input->isCaptured(),
                               *m_overlay, m_ecs, *m_camera);
    }
    drawFrame();
  }
}

} // namespace Nucleus
