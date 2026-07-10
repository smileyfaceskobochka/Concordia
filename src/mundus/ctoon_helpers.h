#pragma once

#include "auxilia/ctoon.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdio>
#include <cmath>
#include <cstring>

namespace Mundus {
namespace CtoonHelpers {

inline void set_str(ctoon_value *obj, const char *key, const char *val) {
  ctoon_value *s = ctoon_obj_set(obj, key);
  s->type = CTOON_STRING;
  if (val) {
    s->str_val = (char *)malloc(strlen(val) + 1);
    strcpy(s->str_val, val);
  }
}

inline void set_num(ctoon_value *obj, const char *key, double val) {
  ctoon_value *s = ctoon_obj_set(obj, key);
  s->type = CTOON_NUMBER;
  s->num_val = val;
}

inline void set_bool(ctoon_value *obj, const char *key, int val) {
  ctoon_value *s = ctoon_obj_set(obj, key);
  s->type = CTOON_BOOL;
  s->bool_val = val;
}

inline void set_obj(ctoon_value *parent, const char *key) {
  ctoon_value *s = ctoon_obj_set(parent, key);
  s->type = CTOON_OBJECT;
}

inline void set_vec3(ctoon_value *obj, const char *key, float a, float b, float c) {
  char buf[80];
  snprintf(buf, sizeof(buf), "@vec3(%g,%g,%g)", (double)a, (double)b, (double)c);
  set_str(obj, key, buf);
}

inline void set_vec4(ctoon_value *obj, const char *key, float a, float b,
                     float c, float d) {
  char buf[80];
  snprintf(buf, sizeof(buf), "@vec4(%g,%g,%g,%g)", (double)a, (double)b,
           (double)c, (double)d);
  set_str(obj, key, buf);
}

inline glm::vec3 get_vec3(ctoon_value *obj, const char *key, glm::vec3 fallback) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  if (!v || v->type != CTOON_STRING || !v->str_val) return fallback;
  float vals[3];
  if (sscanf(v->str_val, "@vec3(%f,%f,%f)", &vals[0], &vals[1], &vals[2]) == 3)
    return {vals[0], vals[1], vals[2]};
  return fallback;
}

inline glm::vec4 get_vec4(ctoon_value *obj, const char *key, glm::vec4 fallback) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  if (!v || v->type != CTOON_STRING || !v->str_val) return fallback;
  float vals[4];
  if (sscanf(v->str_val, "@vec4(%f,%f,%f,%f)", &vals[0], &vals[1], &vals[2],
             &vals[3]) == 4)
    return {vals[0], vals[1], vals[2], vals[3]};
  return fallback;
}

inline void set_quat(ctoon_value *obj, const char *key, float x, float y,
                     float z, float w) {
  char buf[96];
  snprintf(buf, sizeof(buf), "@quat(%g,%g,%g,%g)", (double)x, (double)y,
           (double)z, (double)w);
  set_str(obj, key, buf);
}

inline glm::quat get_quat(ctoon_value *obj, const char *key, glm::quat fallback) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  if (!v || v->type != CTOON_STRING || !v->str_val) return fallback;
  float vals[4];
  if (sscanf(v->str_val, "@quat(%f,%f,%f,%f)", &vals[0], &vals[1], &vals[2],
             &vals[3]) == 4)
    return glm::quat(vals[3], vals[0], vals[1], vals[2]);
  return fallback;
}

inline void set_color(ctoon_value *obj, const char *key, float r, float g,
                      float b, float a) {
  char buf[96];
  snprintf(buf, sizeof(buf), "@color(%g,%g,%g,%g)", (double)r, (double)g,
           (double)b, (double)a);
  set_str(obj, key, buf);
}

inline glm::vec4 get_color(ctoon_value *obj, const char *key, glm::vec4 fallback) {
  ctoon_value *v = ctoon_obj_get(obj, key);
  if (!v || v->type != CTOON_STRING || !v->str_val) return fallback;
  float vals[4];
  if (sscanf(v->str_val, "@color(%f,%f,%f,%f)", &vals[0], &vals[1], &vals[2],
             &vals[3]) == 4)
    return {vals[0], vals[1], vals[2], vals[3]};
  return fallback;
}

} // namespace CtoonHelpers
} // namespace Mundus
