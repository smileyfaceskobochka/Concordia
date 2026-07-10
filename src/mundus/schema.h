#pragma once
#include <string>

struct ctoon_value;

namespace Mundus {
namespace Schema {

enum Type {
  String,
  Number,
  Bool,
  Vec2,
  Vec3,
  Vec4,
  Color,
  Quat,
  Asset,
  Entity,
  Primitive,
  Object,
  Array,
};

struct Field {
  const char *name;
  Type type;
  bool required;
};

bool validateScene(ctoon_value *sceneVal, std::string &errors);
bool validateConfig(ctoon_value *cfgRoot, std::string &errors);
bool validateManifest(ctoon_value *manifestVal, std::string &errors);
bool validateUI(ctoon_value *uiVal, std::string &errors);
bool validateEditorKeys(ctoon_value *keysVal, std::string &errors);
bool validateRenderPipelines(ctoon_value *pipelinesVal, std::string &errors);
bool validateShader(ctoon_value *shaderVal, std::string &errors);

} // namespace Schema
} // namespace Mundus
