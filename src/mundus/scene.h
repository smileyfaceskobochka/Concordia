#pragma once

#include "entity.h"
#include "auxilia/ctoon.hpp"
#include <algorithm>
#include <memory>
#include <vector>

// ── Scene Loading Pipeline ─────────────────────────────────────────────
//
// 1. LOAD phase   (CPU-only, deterministic)
//    CTOON produces: entity tree, transforms, materials,
//                   mesh/file references, primitive tags.
//    NO GPU work. All fields parsed as references only.
//
// 2. RESOLVE phase (asset binding)
//    References converted to runtime handles:
//      "@primitive(cube)"        → createCubeMesh()
//      "@asset(models/foo.glb)"  → loadGLTF() or cache lookup
//    Pending state is valid; unresolved → null handle.
//
// 3. UPLOAD phase  (GPU backend)
//    Vertex buffers, textures, shaders bound.
//    Called by Engine after scene deserialization.
//
// RULE: Mesh/material/texture fields are references only.
//       GPU resolution MUST occur after deserialization.
// ────────────────────────────────────────────────────────────────────────

namespace Mundus {

class Scene {
public:
  Scene() = default;
  ~Scene() = default;

  int addEntity(const std::string &name, int parentIndex = -1) {
    Entity ent{};
    ent.name = name;
    ent.id = name;
    ent.parentIndex = parentIndex;
    m_entities.push_back(ent);
    int idx = static_cast<int>(m_entities.size()) - 1;
    if (parentIndex >= 0 && parentIndex < idx) {
      m_entities[parentIndex].children.push_back(idx);
    }
    return idx;
  }

  int duplicateEntity(int index) {
    if (index < 0 || index >= static_cast<int>(m_entities.size()))
      return -1;
    Entity ent = m_entities[index];
    ent.name = ent.name + "_copy";
    ent.parentIndex = -1;
    ent.children.clear();
    m_entities.push_back(ent);
    return static_cast<int>(m_entities.size()) - 1;
  }
 
  int findEntity(const std::string &name) const {
    for (size_t i = 0; i < m_entities.size(); ++i) {
      if (m_entities[i].name == name) return static_cast<int>(i);
    }
    return -1;
  }

  bool removeEntity(int index) {
    if (index < 0 || index >= static_cast<int>(m_entities.size()))
      return false;

    std::vector<bool> keep(m_entities.size(), true);
    std::function<void(int)> mark = [&](int idx) {
      keep[idx] = false;
      for (int child : m_entities[idx].children)
        mark(child);
    };
    mark(index);

    std::vector<int> newIdx(m_entities.size(), -1);
    std::vector<Entity> survivors;
    for (size_t i = 0; i < m_entities.size(); ++i) {
      if (keep[i]) {
        newIdx[i] = (int)survivors.size();
        survivors.push_back(std::move(m_entities[i]));
      }
    }

    for (auto &ent : survivors) {
      if (ent.parentIndex >= 0)
        ent.parentIndex = keep[ent.parentIndex] ? newIdx[ent.parentIndex] : -1;
      for (auto &child : ent.children)
        child = newIdx[child];
      auto it = std::remove_if(ent.children.begin(), ent.children.end(),
                                [](int c) { return c < 0; });
      ent.children.erase(it, ent.children.end());
    }

    m_entities = std::move(survivors);
    return true;
  }

  void setEntityVisible(int index, bool v);

  void clear() { m_entities.clear(); }

  std::vector<Entity> &getEntities() { return m_entities; }
  const std::vector<Entity> &getEntities() const { return m_entities; }

  void update(float deltaTime);

  bool save(const char *path) const;
  bool load(const char *path);

  int findEntityById(const std::string &id) const {
    for (size_t i = 0; i < m_entities.size(); ++i) {
      const auto &e = m_entities[i];
      if (e.id == id || (!e.id.empty() && e.name == id))
        return static_cast<int>(i);
    }
    return -1;
  }

  glm::vec3 globalLightDir = {-0.5f, -1.0f, -0.2f};
  glm::vec3 globalLightColor = {1.0f, 1.0f, 1.0f};

private:
  std::vector<Entity> m_entities;
};

} // namespace Mundus
