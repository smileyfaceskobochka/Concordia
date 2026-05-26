#include "schema.h"
#include "auxilia/toon.hpp"
#include <SDL3/SDL.h>
#include <cstring>
#include <string>

namespace Mundus {
namespace Schema {

// ── helpers ────────────────────────────────────────────────────────────────

static bool matchTag(toon_value *v, const char *prefix, size_t prefixLen) {
  return v && v->type == TOON_STRING && v->str_val &&
         strncmp(v->str_val, prefix, prefixLen) == 0;
}

static const char *typeName(Type t) {
  switch (t) {
  case String:    return "string";
  case Number:    return "number";
  case Bool:      return "bool";
  case Vec2:      return "@vec2";
  case Vec3:      return "@vec3";
  case Vec4:      return "@vec4";
  case Color:     return "@color";
  case Quat:      return "@quat";
  case Asset:     return "@asset(assets://)";
  case Entity:    return "@entity";
  case Primitive: return "@primitive";
  case Object:    return "object";
  case Array:     return "array";
  }
  return "unknown";
}

static bool checkType(toon_value *v, Type type) {
  if (!v)
    return false;
  switch (type) {
  case String:    return v->type == TOON_STRING;
  case Number:    return v->type == TOON_NUMBER;
  case Bool:      return v->type == TOON_BOOL;
  case Object:    return v->type == TOON_OBJECT;
  case Array:     return v->type == TOON_ARRAY;
  case Vec2:      return matchTag(v, "@vec2(", 6);
  case Vec3:      return matchTag(v, "@vec3(", 6);
  case Vec4:      return matchTag(v, "@vec4(", 6);
  case Color:     return matchTag(v, "@color(", 7);
  case Quat:      return matchTag(v, "@quat(", 6);
  case Asset:     return matchTag(v, "@asset(assets://", 16);
  case Entity:    return matchTag(v, "@entity(", 8);
  case Primitive: return matchTag(v, "@primitive(", 11);
  }
  return false;
}

// ── field validation ───────────────────────────────────────────────────────

static bool validateFields(const Field *fields, size_t fieldCount,
                           toon_value *obj, const char *context,
                           std::string &errors) {
  if (!obj || obj->type != TOON_OBJECT)
    return false;
  bool ok = true;
  for (size_t i = 0; i < fieldCount; ++i) {
    const Field &f = fields[i];
    toon_value *val = toon_obj_get(obj, f.name);
    std::string path = std::string(context) + "." + f.name;

    if (!val) {
      if (f.required) {
        errors += "  " + path + ": required field missing\n";
        ok = false;
      }
      continue;
    }

    if (!checkType(val, f.type)) {
      errors += "  " + path + ": expected " + typeName(f.type) + "\n";
      ok = false;
    }
  }
  return ok;
}

// ── scene schema (unchanged) ───────────────────────────────────────────────

static const Field transformFields[] = {
  {"pos",              Vec3,  false},
  {"rot",              Quat,  false},
  {"scale",            Vec3,  false},
  {"angular_velocity", Vec3,  false},
};

static const Field materialFields[] = {
  {"shader",             String, true},
  {"base_color",         Color,  false},
  {"roughness",          Number, false},
  {"metallic",           Number, false},
  {"albedo",             String, false},
  {"normal",             String, false},
  {"metallic_roughness", String, false},
  {"ao",                 String, false},
  {"emissive",           String, false},
};

static const Field entityFields[] = {
  {"id",        String,  true},
  {"name",      String,  false},
  {"visible",   Bool,    false},
  {"parent",    Entity,  false},
  {"transform", Object,  false},
  {"material",  Object,  false},
};

static const Field sceneFields[] = {
  {"light_dir",   Vec3,  false},
  {"light_color", Vec3,  false},
  {"entities",    Array, false},
};

// ── engine config schema ───────────────────────────────────────────────────

static const Field windowFields[] = {
  {"title",      String, false},
  {"width",      Number, false},
  {"height",     Number, false},
  {"vsync",      Bool,   false},
  {"fullscreen", Bool,   false},
  {"monitor",    Number, false},
};

static const Field rendererFields[] = {
  {"max_frames_in_flight", Number, false},
  {"debug_mode",           Bool,   false},
  {"enable_validation",    Bool,   false},
  {"preferred_gpu",        Number, false},
  {"exposure",             Number, false},
  {"gamma",                Number, false},
  {"clear_color",          Color,  false},
  {"max_bindless_textures",Number, false},
  {"depth_format",         String, false},
};

static const Field cameraFields[] = {
  {"fov",              Number, false},
  {"near",             Number, false},
  {"far",              Number, false},
  {"sensitivity",      Number, false},
  {"speed",            Number, false},
  {"default_position", Vec3,   false},
  {"default_yaw",      Number, false},
  {"min_pitch",        Number, false},
  {"max_pitch",        Number, false},
};

static const Field sceneCfgFields[] = {
  {"default_path", String, false},
  {"auto_save",    Bool,   false},
};

static const Field lightingFields[] = {
  {"default_direction", Vec3, false},
  {"default_color",     Vec3, false},
};

static const Field skyboxCfgFields[] = {
  {"default_scale", Vec3, false},
};

static const Field configFields[] = {
  {"window",   Object, false},
  {"renderer", Object, false},
  {"camera",   Object, false},
  {"scene",    Object, false},
  {"lighting", Object, false},
  {"skybox",   Object, false},
};

// ── asset manifest schema ──────────────────────────────────────────────────

static const Field manifestScanDirFields[] = {
  {"path",   String, true},
  {"is_hdr", Bool,   false},
};

static const Field manifestSkyboxFields[] = {
  {"scan_directories", Array,  false},
  {"face_names",       Array,  false},
  {"cross_layout",     Array,  false},
};

static const Field manifestDefaultMatFields[] = {
  {"shader",     String, false},
  {"base_color", Color,  false},
  {"roughness",  Number, false},
  {"metallic",   Number, false},
};

static const Field manifestFields[] = {
  {"preload_meshes",  Array,  false},
  {"default_skybox",  Asset,  false},
  {"skybox",          Object, false},
  {"default_material",Object, false},
};

// ── UI schema ──────────────────────────────────────────────────────────────

static const Field uiFontFields[] = {
  {"path",        String, true},
  {"size",        Number, true},
  {"glyph_offset_y", Number, false},
  {"merge_mode",  Bool,   false},
};

static const Field uiFields[] = {
  {"fonts",             Array,  false},
  {"frame_padding",     Number, false},
  {"item_spacing",      Number, false},
  {"window_rounding",   Number, false},
  {"frame_rounding",    Number, false},
  {"scrollbar_size",    Number, false},
  {"grab_min_size",     Number, false},
  {"stats_padding",     Number, false},
  {"inspector_width",   Number, false},
  {"asset_window_size", Vec2,   false},
  {"rename_buf_size",   Number, false},
  {"slider_speed_min",  Number, false},
  {"slider_speed_max",  Number, false},
  {"slider_sens_min",   Number, false},
  {"slider_sens_max",   Number, false},
  {"debug_modes",       Array,  false},
  {"descriptor_pool_sets",   Number, false},
  {"descriptor_pool_samplers",    Number, false},
  {"descriptor_pool_combined_image_samplers", Number, false},
};

// ── editor keys schema ─────────────────────────────────────────────────────

static const Field keyBindingFields[] = {
  {"action", String, true},
  {"key",    String, true},
  {"ctrl",   Bool,   false},
  {"shift",  Bool,   false},
  {"alt",    Bool,   false},
};

static const Field editorKeysFields[] = {
  {"bindings", Array, false},
  {"camera_forward",  String, false},
  {"camera_backward", String, false},
  {"camera_left",     String, false},
  {"camera_right",    String, false},
  {"camera_up",       String, false},
  {"camera_down",     String, false},
  {"capture_exit",    String, false},
};

// ── render pipelines schema ────────────────────────────────────────────────

static const Field pipelineFields[] = {
  {"name",                String, true},
  {"vertex_shader",       String, true},
  {"fragment_shader",     String, true},
  {"depth_test",          Bool,   false},
  {"depth_write",         Bool,   false},
  {"depth_compare",       String, false},
  {"cull_mode",           String, false},
  {"push_constant_size",  Number, false},
};

static const Field renderPipelinesFields[] = {
  {"pipelines", Array, false},
};

// ── public API ─────────────────────────────────────────────────────────────

bool validateEntity(toon_value *obj, const char *context, std::string &errors);
bool validateFields(const Field *fields, size_t fieldCount,
                    toon_value *obj, const char *context, std::string &errors);

bool validateEntity(toon_value *obj, const char *context, std::string &errors) {
  if (!validateFields(entityFields, 6, obj, context, errors))
    return false;
  bool ok = true;
  toon_value *tf = toon_obj_get(obj, "transform");
  if (tf)
    ok &= validateFields(transformFields, 4, tf,
                         (std::string(context) + ".transform").c_str(), errors);
  toon_value *mat = toon_obj_get(obj, "material");
  if (mat)
    ok &= validateFields(materialFields, 9, mat,
                         (std::string(context) + ".material").c_str(), errors);
  toon_value *mesh = toon_obj_get(obj, "mesh");
  if (mesh) {
    if (!checkType(mesh, Primitive) && !checkType(mesh, Asset)) {
      errors += std::string("  ") + context +
                ".mesh: expected @primitive or @asset(assets://)\n";
      ok = false;
    }
  }
  return ok;
}

bool validateScene(toon_value *sceneVal, std::string &errors) {
  if (!sceneVal || sceneVal->type != TOON_OBJECT)
    return false;
  bool ok = validateFields(sceneFields, 3, sceneVal, "scene", errors);
  toon_value *arr = toon_obj_get(sceneVal, "entities");
  if (arr && arr->type == TOON_ARRAY) {
    char ctx[64];
    for (size_t i = 0; i < arr->len; ++i) {
      snprintf(ctx, sizeof(ctx), "scene.entities[%zu]", i);
      ok &= validateEntity(&arr->arr[i], ctx, errors);
    }
  }
  return ok;
}

bool validateConfig(toon_value *cfgRoot, std::string &errors) {
  if (!cfgRoot || cfgRoot->type != TOON_OBJECT)
    return false;
  bool ok = validateFields(configFields, 6, cfgRoot, "config", errors);
  toon_value *w = toon_obj_get(cfgRoot, "window");
  if (w) ok &= validateFields(windowFields, 6, w, "config.window", errors);
  toon_value *r = toon_obj_get(cfgRoot, "renderer");
  if (r) ok &= validateFields(rendererFields, 9, r, "config.renderer", errors);
  toon_value *c = toon_obj_get(cfgRoot, "camera");
  if (c) ok &= validateFields(cameraFields, 9, c, "config.camera", errors);
  toon_value *sc = toon_obj_get(cfgRoot, "scene");
  if (sc) ok &= validateFields(sceneCfgFields, 2, sc, "config.scene", errors);
  toon_value *li = toon_obj_get(cfgRoot, "lighting");
  if (li) ok &= validateFields(lightingFields, 2, li, "config.lighting", errors);
  toon_value *sk = toon_obj_get(cfgRoot, "skybox");
  if (sk) ok &= validateFields(skyboxCfgFields, 1, sk, "config.skybox", errors);
  return ok;
}

bool validateManifest(toon_value *manifestVal, std::string &errors) {
  if (!manifestVal || manifestVal->type != TOON_OBJECT)
    return false;
  bool ok = validateFields(manifestFields, 4, manifestVal, "manifest", errors);

  // Validate each preload_mesh entry
  toon_value *arr = toon_obj_get(manifestVal, "preload_meshes");
  if (arr && arr->type == TOON_ARRAY) {
    for (size_t i = 0; i < arr->len; ++i) {
      toon_value *e = &arr->arr[i];
      if (!checkType(e, Primitive) && !checkType(e, Asset)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "manifest.preload_meshes[%zu]", i);
        errors += std::string("  ") + buf +
                  ": expected @primitive or @asset(assets://)\n";
        ok = false;
      }
    }
  }

  // Validate skybox sub-section
  toon_value *sky = toon_obj_get(manifestVal, "skybox");
  if (sky && sky->type == TOON_OBJECT) {
    ok &= validateFields(manifestSkyboxFields, 3, sky,
                         "manifest.skybox", errors);
    toon_value *dirs = toon_obj_get(sky, "scan_directories");
    if (dirs && dirs->type == TOON_ARRAY) {
      for (size_t i = 0; i < dirs->len; ++i) {
        char ctx[64];
        snprintf(ctx, sizeof(ctx), "manifest.skybox.scan_directories[%zu]", i);
        ok &= validateFields(manifestScanDirFields, 2, &dirs->arr[i], ctx, errors);
      }
    }
  }

  // Validate default_material
  toon_value *dm = toon_obj_get(manifestVal, "default_material");
  if (dm) {
    ok &= validateFields(manifestDefaultMatFields, 4, dm,
                         "manifest.default_material", errors);
  }

  return ok;
}

bool validateUI(toon_value *uiVal, std::string &errors) {
  if (!uiVal || uiVal->type != TOON_OBJECT)
    return false;
  bool ok = validateFields(uiFields, 19, uiVal, "ui", errors);

  // Validate fonts array
  toon_value *fonts = toon_obj_get(uiVal, "fonts");
  if (fonts && fonts->type == TOON_ARRAY) {
    for (size_t i = 0; i < fonts->len; ++i) {
      char ctx[64];
      snprintf(ctx, sizeof(ctx), "ui.fonts[%zu]", i);
      ok &= validateFields(uiFontFields, 4, &fonts->arr[i], ctx, errors);
    }
  }

  // Validate debug_modes array entries are strings
  toon_value *modes = toon_obj_get(uiVal, "debug_modes");
  if (modes && modes->type == TOON_ARRAY) {
    for (size_t i = 0; i < modes->len; ++i) {
      if (modes->arr[i].type != TOON_STRING) {
        char buf[64];
        snprintf(buf, sizeof(buf), "ui.debug_modes[%zu]", i);
        errors += std::string("  ") + buf + ": expected string\n";
        ok = false;
      }
    }
  }

  return ok;
}

bool validateEditorKeys(toon_value *keysVal, std::string &errors) {
  if (!keysVal || keysVal->type != TOON_OBJECT)
    return false;
  bool ok = validateFields(editorKeysFields, 8, keysVal, "editor_keys", errors);

  // Validate each binding
  toon_value *bindings = toon_obj_get(keysVal, "bindings");
  if (bindings && bindings->type == TOON_ARRAY) {
    for (size_t i = 0; i < bindings->len; ++i) {
      char ctx[64];
      snprintf(ctx, sizeof(ctx), "editor_keys.bindings[%zu]", i);
      ok &= validateFields(keyBindingFields, 5, &bindings->arr[i], ctx, errors);
    }
  }

  return ok;
}

bool validateRenderPipelines(toon_value *pipelinesVal, std::string &errors) {
  if (!pipelinesVal || pipelinesVal->type != TOON_OBJECT)
    return false;
  bool ok = validateFields(renderPipelinesFields, 1, pipelinesVal,
                           "render_pipelines", errors);

  toon_value *pips = toon_obj_get(pipelinesVal, "pipelines");
  if (pips && pips->type == TOON_ARRAY) {
    for (size_t i = 0; i < pips->len; ++i) {
      char ctx[64];
      snprintf(ctx, sizeof(ctx), "render_pipelines.pipelines[%zu]", i);
      ok &= validateFields(pipelineFields, 8, &pips->arr[i], ctx, errors);
    }
  }

  return ok;
}

} // namespace Schema
} // namespace Mundus
