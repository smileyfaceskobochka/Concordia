#include "scene.h"
#include "schema.h"
#include "ctoon_helpers.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Mundus {

using namespace CtoonHelpers;

glm::mat4 Transform::getLocalMatrix() const {
  glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
  m = glm::rotate(m, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
  m = glm::rotate(m, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
  m = glm::rotate(m, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
  m = glm::scale(m, scale);
  return m;
}

void Scene::update(float deltaTime) {
  for (auto &ent : m_entities) {
    ent.transform.rotation += ent.transform.angularVelocity * deltaTime;
  }

  std::function<void(int, glm::mat4, bool)> computeHierarchy =
      [&](int index, glm::mat4 parentGlobal, bool parentVisible) {
        auto &ent = m_entities[index];
        ent.globalTransform = parentGlobal * ent.transform.getLocalMatrix();
        ent.effectiveVisible = ent.visible && parentVisible;

        for (int childIdx : ent.children) {
          computeHierarchy(childIdx, ent.globalTransform, ent.effectiveVisible);
        }
      };

  for (size_t i = 0; i < m_entities.size(); ++i) {
    if (m_entities[i].parentIndex == -1) {
      computeHierarchy(static_cast<int>(i), glm::mat4(1.0f), true);
    }
  }
}

void Scene::setEntityVisible(int index, bool v) {
  if (index < 0 || index >= static_cast<int>(m_entities.size()))
    return;
  m_entities[index].visible = v;
  for (int childIdx : m_entities[index].children) {
    setEntityVisible(childIdx, v);
  }
}

// ── save ───────────────────────────────────────────────────────────────────

bool Scene::save(const char *path) const {
  Auxilia::ctoon_doc doc(CTOON_OBJECT);
  ctoon_value *root = doc.get();

  ctoon_value *scene = ctoon_obj_set(root, "scene");
  scene->type = CTOON_OBJECT;

  set_vec3(scene, "light_dir", globalLightDir.x, globalLightDir.y,
           globalLightDir.z);
  set_vec3(scene, "light_color", globalLightColor.x, globalLightColor.y,
           globalLightColor.z);

  ctoon_value *arr = ctoon_obj_set(scene, "entities");
  arr->type = CTOON_ARRAY;

  auto r6 = [](double v) { return std::round(v * 1e6) / 1e6; };

  for (const auto &ent : m_entities) {
    ctoon_value *e = ctoon_array_push(arr);
    e->type = CTOON_OBJECT;

    const char *eid = !ent.id.empty() ? ent.id.c_str() : ent.name.c_str();
    set_str(e, "id", eid);
    if (ent.name != ent.id)
      set_str(e, "name", ent.name.c_str());
    set_bool(e, "visible", ent.visible ? 1 : 0);

    // Parent as stable entity reference
    if (ent.parentIndex >= 0 && ent.parentIndex < (int)m_entities.size()) {
      const Entity &p = m_entities[ent.parentIndex];
      const char *pid = !p.id.empty() ? p.id.c_str()
                      : !p.name.empty() ? p.name.c_str() : nullptr;
      if (pid) {
        std::string ref = std::string("@entity(") + pid + ")";
        set_str(e, "parent", ref.c_str());
      }
    }

    // Transform: write only non-default fields
    bool hasPos = ent.transform.position != glm::vec3(0.0f);
    bool hasRot = ent.transform.rotation != glm::vec3(0.0f);
    bool hasScale = ent.transform.scale != glm::vec3(1.0f);
    bool hasAngVel = ent.transform.angularVelocity != glm::vec3(0.0f);
    if (hasPos || hasRot || hasScale || hasAngVel) {
      ctoon_value *tf = ctoon_obj_set(e, "transform");
      tf->type = CTOON_OBJECT;
      if (hasPos)
        set_vec3(tf, "pos", r6(ent.transform.position.x),
                 r6(ent.transform.position.y), r6(ent.transform.position.z));
      if (hasRot) {
        glm::quat q(glm::vec3(ent.transform.rotation.x,
                              ent.transform.rotation.y,
                              ent.transform.rotation.z));
        set_quat(tf, "rot", r6(q.x), r6(q.y), r6(q.z), r6(q.w));
      }
      if (hasScale)
        set_vec3(tf, "scale", r6(ent.transform.scale.x),
                 r6(ent.transform.scale.y), r6(ent.transform.scale.z));
      if (hasAngVel)
        set_vec3(tf, "angular_velocity", r6(ent.transform.angularVelocity.x),
                 r6(ent.transform.angularVelocity.y),
                 r6(ent.transform.angularVelocity.z));
    }

    // Mesh reference: flat @primitive tag or @asset tagged string
    if (!ent.meshSource.empty()) {
      if (ent.meshSource.compare(0, 11, "@primitive(") == 0) {
        set_str(e, "mesh", ent.meshSource.c_str());
      } else {
        std::string ref = "@asset(assets://" + ent.meshSource + ")";
        set_str(e, "mesh", ref.c_str());
      }
    }

    if (ent.material) {
      ctoon_value *m = ctoon_obj_set(e, "material");
      m->type = CTOON_OBJECT;
      set_str(m, "shader", ent.material->shaderName.c_str());
      set_color(m, "base_color", ent.material->baseColor.x,
                ent.material->baseColor.y, ent.material->baseColor.z,
                ent.material->baseColor.w);
      set_num(m, "roughness", r6(ent.material->roughness));
      set_num(m, "metallic", r6(ent.material->metallic));
      auto saveTex = [&](const char *key, const std::string &src) {
        if (!src.empty()) set_str(m, key, src.c_str());
      };
      saveTex("albedo", ent.material->albedoSource);
      saveTex("normal", ent.material->normalSource);
      saveTex("metallic_roughness", ent.material->metallicRoughnessSource);
      saveTex("ao", ent.material->aoSource);
      saveTex("emissive", ent.material->emissiveSource);
    }
  }

  return doc.save_file(path);
}

// ── load ───────────────────────────────────────────────────────────────────

bool Scene::load(const char *path) {
  Auxilia::ctoon_doc doc;
  if (!doc.load_file(path))
    return false;

  if (!doc.has("scene"))
    return false;

  {
    ctoon_value *root = doc.get();
    ctoon_value *sv = ctoon_obj_get(root, "scene");
    if (sv && sv->type == CTOON_OBJECT) {
      std::string schemaErrors;
      if (!Schema::validateScene(sv, schemaErrors)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Scene '%s' schema violations:\n%s", path,
                    schemaErrors.c_str());
      }
      globalLightDir = get_vec3(sv, "light_dir", glm::vec3(0.0f));
      globalLightColor = get_vec3(sv, "light_color", glm::vec3(0.0f));
    }
  }

  ctoon_value *root = doc.get();
  ctoon_value *sceneVal = ctoon_obj_get(root, "scene");
  if (!sceneVal || sceneVal->type != CTOON_OBJECT)
    return false;
  ctoon_value *arr = ctoon_obj_get(sceneVal, "entities");
  if (!arr || arr->type != CTOON_ARRAY)
    return true;

  // ── Pass 1: create all entities ───────────────────────────────────────
  struct LoadedEnt {
    Entity ent;
    std::string parentRef; // stored as parent id/name reference
  };
  std::vector<LoadedEnt> loaded;
  loaded.reserve(arr->len);

  for (size_t i = 0; i < arr->len; ++i) {
    ctoon_value *e = &arr->arr[i];
    if (e->type != CTOON_OBJECT)
      continue;

    LoadedEnt le;

    ctoon_value *idv = ctoon_obj_get(e, "id");
    if (idv && idv->type == CTOON_STRING && idv->str_val)
      le.ent.id = idv->str_val;

    ctoon_value *nv = ctoon_obj_get(e, "name");
    if (nv && nv->type == CTOON_STRING && nv->str_val)
      le.ent.name = nv->str_val;

    if (le.ent.id.empty())
      le.ent.id = le.ent.name;
    if (le.ent.name.empty())
      le.ent.name = le.ent.id;

    ctoon_value *pv = ctoon_obj_get(e, "parent");
    if (pv && pv->type == CTOON_STRING && pv->str_val) {
      const char *ps = pv->str_val;
      size_t plen = strlen(ps);
      if (plen > 8 && strncmp(ps, "@entity(", 8) == 0 && ps[plen - 1] == ')')
        le.parentRef = std::string(ps + 8, plen - 9);
    }

    ctoon_value *vv = ctoon_obj_get(e, "visible");
    if (vv && vv->type == CTOON_BOOL)
      le.ent.visible = vv->bool_val != 0;

    ctoon_value *tf = ctoon_obj_get(e, "transform");
    if (tf && tf->type == CTOON_OBJECT) {
      le.ent.transform.position = get_vec3(tf, "pos", le.ent.transform.position);
      {
        glm::quat q = get_quat(tf, "rot", glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        le.ent.transform.rotation = glm::eulerAngles(q);
      }
      le.ent.transform.scale = get_vec3(tf, "scale", le.ent.transform.scale);
      le.ent.transform.angularVelocity =
          get_vec3(tf, "angular_velocity", le.ent.transform.angularVelocity);
    }

    ctoon_value *mesh = ctoon_obj_get(e, "mesh");
    if (mesh && mesh->type == CTOON_STRING && mesh->str_val) {
      const char *s = mesh->str_val;
      size_t len = strlen(s);
      if (len > 11 && strncmp(s, "@primitive(", 11) == 0 && s[len - 1] == ')') {
        le.ent.meshSource = s;
      } else if (len > 17 && strncmp(s, "@asset(assets://", 16) == 0 &&
                 s[len - 1] == ')') {
        le.ent.meshSource = std::string(s + 16, len - 17);
      }
    }

    ctoon_value *mat = ctoon_obj_get(e, "material");
    if (mat && mat->type == CTOON_OBJECT) {
      auto m = std::make_shared<Forma::Material>();

      ctoon_value *sh = ctoon_obj_get(mat, "shader");
      if (sh && sh->type == CTOON_STRING && sh->str_val)
        m->shaderName = sh->str_val;

      m->baseColor = get_color(mat, "base_color", m->baseColor);

      ctoon_value *rgh = ctoon_obj_get(mat, "roughness");
      if (rgh && rgh->type == CTOON_NUMBER)
        m->roughness = (float)rgh->num_val;

      ctoon_value *met = ctoon_obj_get(mat, "metallic");
      if (met && met->type == CTOON_NUMBER)
        m->metallic = (float)met->num_val;

      auto loadTex = [&](const char *key, std::string &dst) {
        ctoon_value *v = ctoon_obj_get(mat, key);
        if (v && v->type == CTOON_STRING && v->str_val)
          dst = v->str_val;
      };
      loadTex("albedo", m->albedoSource);
      loadTex("normal", m->normalSource);
      loadTex("metallic_roughness", m->metallicRoughnessSource);
      loadTex("ao", m->aoSource);
      loadTex("emissive", m->emissiveSource);

      le.ent.material = m;
    }

    loaded.push_back(le);
  }

  // ── Pass 2: populate scene & resolve parent references ────────────────
  m_entities.clear();
  m_entities.reserve(loaded.size());

  for (auto &le : loaded)
    m_entities.push_back(le.ent);

  for (size_t i = 1; i < m_entities.size(); ++i) {
    auto &ent = m_entities[i];
    if (!loaded[i].parentRef.empty()) {
      int pi = findEntityById(loaded[i].parentRef);
      if (pi >= 0 && pi < (int)i) {
        ent.parentIndex = pi;
        m_entities[pi].children.push_back((int)i);
      }
    }
  }

  SDL_Log("Scene: loaded %zu entities from %s", m_entities.size(), path);
  return true;
}

} // namespace Mundus
