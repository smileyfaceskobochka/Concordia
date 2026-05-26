#include "scene_pick.h"
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Mundus {

static bool rayIntersectsAABB(glm::vec3 rayOrigin, glm::vec3 rayDir,
                               glm::vec3 aabbMin, glm::vec3 aabbMax) {
  float tMin = -1e30f, tMax = 1e30f;
  for (int axis = 0; axis < 3; ++axis) {
    float invD = 1.0f / rayDir[axis];
    float t0 = (aabbMin[axis] - rayOrigin[axis]) * invD;
    float t1 = (aabbMax[axis] - rayOrigin[axis]) * invD;
    if (invD < 0.0f) std::swap(t0, t1);
    tMin = std::max(tMin, t0);
    tMax = std::min(tMax, t1);
    if (tMax < tMin) return false;
  }
  return true;
}

int pickEntity(Scene &scene, Vista::Camera &camera,
               float mouseX, float mouseY,
               int viewportW, int viewportH) {
  auto &entities = scene.getEntities();
  if (entities.empty() || viewportW == 0 || viewportH == 0)
    return -1;

  float ndcX = (2.0f * mouseX / viewportW - 1.0f);
  float ndcY = (1.0f - 2.0f * mouseY / viewportH);

  glm::mat4 invProj = glm::inverse(camera.getProj());
  glm::mat4 invView = glm::inverse(camera.getView());

  glm::vec4 rayClip(ndcX, ndcY, -1.0f, 1.0f);
  glm::vec4 rayEye = invProj * rayClip;
  rayEye.z = -1.0f; rayEye.w = 0.0f;
  glm::vec4 rayWorld4 = invView * rayEye;
  glm::vec3 rayDir = glm::normalize(glm::vec3(rayWorld4));

  glm::vec3 rayOrigin = camera.getPosition();

  int closest = -1;
  float closestDist = 1e30f;
  for (int i = 0; i < static_cast<int>(entities.size()); ++i) {
    auto &ent = entities[i];
    if (!ent.mesh || !ent.visible) continue;

    glm::mat4 m = ent.globalTransform;
    glm::vec3 corners[8] = {
      glm::vec3(m * glm::vec4(ent.mesh->aabbMin.x, ent.mesh->aabbMin.y, ent.mesh->aabbMin.z, 1.0f)),
      glm::vec3(m * glm::vec4(ent.mesh->aabbMax.x, ent.mesh->aabbMin.y, ent.mesh->aabbMin.z, 1.0f)),
      glm::vec3(m * glm::vec4(ent.mesh->aabbMin.x, ent.mesh->aabbMax.y, ent.mesh->aabbMin.z, 1.0f)),
      glm::vec3(m * glm::vec4(ent.mesh->aabbMax.x, ent.mesh->aabbMax.y, ent.mesh->aabbMin.z, 1.0f)),
      glm::vec3(m * glm::vec4(ent.mesh->aabbMin.x, ent.mesh->aabbMin.y, ent.mesh->aabbMax.z, 1.0f)),
      glm::vec3(m * glm::vec4(ent.mesh->aabbMax.x, ent.mesh->aabbMin.y, ent.mesh->aabbMax.z, 1.0f)),
      glm::vec3(m * glm::vec4(ent.mesh->aabbMin.x, ent.mesh->aabbMax.y, ent.mesh->aabbMax.z, 1.0f)),
      glm::vec3(m * glm::vec4(ent.mesh->aabbMax.x, ent.mesh->aabbMax.y, ent.mesh->aabbMax.z, 1.0f)),
    };
    glm::vec3 wMin = corners[0], wMax = corners[0];
    for (int c = 1; c < 8; ++c) {
      wMin = glm::min(wMin, corners[c]);
      wMax = glm::max(wMax, corners[c]);
    }

    if (rayIntersectsAABB(rayOrigin, rayDir, wMin, wMax)) {
      float d = glm::distance(rayOrigin, (wMin + wMax) * 0.5f);
      if (d < closestDist) {
        closestDist = d;
        closest = i;
      }
    }
  }

  return closest;
}

} // namespace Mundus
