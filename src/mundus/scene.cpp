#include "scene.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>

namespace Mundus {

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

// ── helpers for TOON serialization ─────────────────────────────────────────

static void set_str(toon_value *obj, const char *key, const char *val) {
  toon_value *s = toon_obj_set(obj, key);
  s->type = TOON_STRING;
  if (val) {
    s->str_val = (char *)malloc(strlen(val) + 1);
    strcpy(s->str_val, val);
  }
}

static void set_num(toon_value *obj, const char *key, double val) {
  toon_value *s = toon_obj_set(obj, key);
  s->type = TOON_NUMBER;
  s->num_val = val;
}

static void set_bool(toon_value *obj, const char *key, int val) {
  toon_value *s = toon_obj_set(obj, key);
  s->type = TOON_BOOL;
  s->bool_val = val;
}

static void set_obj(toon_value *parent, const char *key) {
  toon_value *s = toon_obj_set(parent, key);
  s->type = TOON_OBJECT;
}

static void set_vec3(toon_value *obj, const char *key, float a, float b, float c) {
  char buf[80];
  snprintf(buf, sizeof(buf), "@vec3(%g,%g,%g)", (double)a, (double)b, (double)c);
  set_str(obj, key, buf);
}

static void set_vec4(toon_value *obj, const char *key, float a, float b,
                     float c, float d) {
  char buf[80];
  snprintf(buf, sizeof(buf), "@vec4(%g,%g,%g,%g)", (double)a, (double)b,
           (double)c, (double)d);
  set_str(obj, key, buf);
}

static glm::vec3 get_vec3(toon_value *obj, const char *key, glm::vec3 fallback) {
  toon_value *v = toon_obj_get(obj, key);
  if (!v || v->type != TOON_STRING || !v->str_val)
    return fallback;
  float vals[3];
  if (sscanf(v->str_val, "@vec3(%f,%f,%f)", &vals[0], &vals[1], &vals[2]) == 3)
    return {vals[0], vals[1], vals[2]};
  SDL_Log("Scene: invalid vec3 format in '%s' — expected @vec3(x,y,z)", key);
  return fallback;
}

static glm::vec4 get_vec4(toon_value *obj, const char *key, glm::vec4 fallback) {
  toon_value *v = toon_obj_get(obj, key);
  if (!v || v->type != TOON_STRING || !v->str_val)
    return fallback;
  float vals[4];
  if (sscanf(v->str_val, "@vec4(%f,%f,%f,%f)", &vals[0], &vals[1], &vals[2],
             &vals[3]) == 4)
    return {vals[0], vals[1], vals[2], vals[3]};
  SDL_Log("Scene: invalid vec4 format in '%s' — expected @vec4(x,y,z,w)", key);
  return fallback;
}

// ── save ───────────────────────────────────────────────────────────────────

bool Scene::save(const char *path) const {
  Auxilia::toon_doc doc(TOON_OBJECT);
  toon_value *root = doc.get();

  toon_value *scene = toon_obj_set(root, "scene");
  scene->type = TOON_OBJECT;

  set_vec3(scene, "light_dir", globalLightDir.x, globalLightDir.y,
           globalLightDir.z);
  set_vec3(scene, "light_color", globalLightColor.x, globalLightColor.y,
           globalLightColor.z);

  toon_value *arr = toon_obj_set(scene, "entities");
  arr->type = TOON_ARRAY;

  auto r6 = [](double v) { return std::round(v * 1e6) / 1e6; };

  for (const auto &ent : m_entities) {
    toon_value *e = toon_array_push(arr);
    e->type = TOON_OBJECT;

    const char *eid = !ent.id.empty() ? ent.id.c_str() : ent.name.c_str();
    set_str(e, "id", eid);
    if (ent.name != ent.id)
      set_str(e, "name", ent.name.c_str());
    set_bool(e, "visible", ent.visible ? 1 : 0);

    // Parent as stable name/id reference
    if (ent.parentIndex >= 0 && ent.parentIndex < (int)m_entities.size()) {
      const Entity &p = m_entities[ent.parentIndex];
      const char *pid = !p.id.empty() ? p.id.c_str()
                      : !p.name.empty() ? p.name.c_str() : nullptr;
      if (pid)
        set_str(e, "parent", pid);
    }

    // Transform: write only non-default fields
    bool hasPos = ent.transform.position != glm::vec3(0.0f);
    bool hasRot = ent.transform.rotation != glm::vec3(0.0f);
    bool hasScale = ent.transform.scale != glm::vec3(1.0f);
    bool hasAngVel = ent.transform.angularVelocity != glm::vec3(0.0f);
    if (hasPos || hasRot || hasScale || hasAngVel) {
      toon_value *tf = toon_obj_set(e, "transform");
      tf->type = TOON_OBJECT;
      if (hasPos)
        set_vec3(tf, "pos", r6(ent.transform.position.x),
                 r6(ent.transform.position.y), r6(ent.transform.position.z));
      if (hasRot)
        set_vec3(tf, "rot", r6(ent.transform.rotation.x),
                 r6(ent.transform.rotation.y), r6(ent.transform.rotation.z));
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
        std::string ref = "@asset(" + ent.meshSource + ")";
        set_str(e, "mesh", ref.c_str());
      }
    }

    if (ent.material) {
      toon_value *m = toon_obj_set(e, "material");
      m->type = TOON_OBJECT;
      set_str(m, "shader", ent.material->shaderName.c_str());
      set_vec4(m, "base_color", ent.material->baseColor.x,
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
  Auxilia::toon_doc doc;
  if (!doc.load_file(path))
    return false;

  if (!doc.has("scene"))
    return false;

  {
    toon_value *root = doc.get();
    toon_value *sv = toon_obj_get(root, "scene");
    if (sv && sv->type == TOON_OBJECT) {
      globalLightDir = get_vec3(sv, "light_dir", globalLightDir);
      globalLightColor = get_vec3(sv, "light_color", globalLightColor);
    }
  }

  toon_value *root = doc.get();
  toon_value *sceneVal = toon_obj_get(root, "scene");
  if (!sceneVal || sceneVal->type != TOON_OBJECT)
    return false;
  toon_value *arr = toon_obj_get(sceneVal, "entities");
  if (!arr || arr->type != TOON_ARRAY)
    return true;

  // ── Pass 1: create all entities ───────────────────────────────────────
  struct LoadedEnt {
    Entity ent;
    std::string parentRef; // stored as parent id/name reference
  };
  std::vector<LoadedEnt> loaded;
  loaded.reserve(arr->len);

  for (size_t i = 0; i < arr->len; ++i) {
    toon_value *e = &arr->arr[i];
    if (e->type != TOON_OBJECT)
      continue;

    LoadedEnt le;

    toon_value *idv = toon_obj_get(e, "id");
    if (idv && idv->type == TOON_STRING && idv->str_val)
      le.ent.id = idv->str_val;

    toon_value *nv = toon_obj_get(e, "name");
    if (nv && nv->type == TOON_STRING && nv->str_val)
      le.ent.name = nv->str_val;

    if (le.ent.id.empty())
      le.ent.id = le.ent.name;
    if (le.ent.name.empty())
      le.ent.name = le.ent.id;

    toon_value *pv = toon_obj_get(e, "parent");
    if (pv && pv->type == TOON_STRING && pv->str_val)
      le.parentRef = pv->str_val;

    toon_value *vv = toon_obj_get(e, "visible");
    if (vv && vv->type == TOON_BOOL)
      le.ent.visible = vv->bool_val != 0;

    toon_value *tf = toon_obj_get(e, "transform");
    if (tf && tf->type == TOON_OBJECT) {
      le.ent.transform.position = get_vec3(tf, "pos", le.ent.transform.position);
      le.ent.transform.rotation = get_vec3(tf, "rot", le.ent.transform.rotation);
      le.ent.transform.scale = get_vec3(tf, "scale", le.ent.transform.scale);
      le.ent.transform.angularVelocity =
          get_vec3(tf, "angular_velocity", le.ent.transform.angularVelocity);
    }

    toon_value *mesh = toon_obj_get(e, "mesh");
    if (mesh && mesh->type == TOON_STRING && mesh->str_val) {
      const char *s = mesh->str_val;
      size_t len = strlen(s);
      if (len > 11 && strncmp(s, "@primitive(", 11) == 0 && s[len - 1] == ')') {
        le.ent.meshSource = s;
      } else if (len > 8 && strncmp(s, "@asset(", 7) == 0 && s[len - 1] == ')') {
        std::string inner(s + 7, len - 8);
        // Handle both @asset(path) and @asset("path") (legacy)
        if (inner.size() >= 2 && inner.front() == '"' && inner.back() == '"')
          le.ent.meshSource = inner.substr(1, inner.size() - 2);
        else
          le.ent.meshSource = inner;
      }
    }

    toon_value *mat = toon_obj_get(e, "material");
    if (mat && mat->type == TOON_OBJECT) {
      auto m = std::make_shared<Forma::Material>();

      toon_value *sh = toon_obj_get(mat, "shader");
      if (sh && sh->type == TOON_STRING && sh->str_val)
        m->shaderName = sh->str_val;

      m->baseColor = get_vec4(mat, "base_color", m->baseColor);

      toon_value *rgh = toon_obj_get(mat, "roughness");
      if (rgh && rgh->type == TOON_NUMBER)
        m->roughness = (float)rgh->num_val;

      toon_value *met = toon_obj_get(mat, "metallic");
      if (met && met->type == TOON_NUMBER)
        m->metallic = (float)met->num_val;

      auto loadTex = [&](const char *key, std::string &dst) {
        toon_value *v = toon_obj_get(mat, key);
        if (v && v->type == TOON_STRING && v->str_val)
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
