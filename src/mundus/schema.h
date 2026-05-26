#pragma once
#include <string>

struct toon_value;

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

bool validateScene(toon_value *sceneVal, std::string &errors);
bool validateConfig(toon_value *cfgRoot, std::string &errors);
bool validateManifest(toon_value *manifestVal, std::string &errors);
bool validateUI(toon_value *uiVal, std::string &errors);
bool validateEditorKeys(toon_value *keysVal, std::string &errors);
bool validateRenderPipelines(toon_value *pipelinesVal, std::string &errors);

} // namespace Schema
} // namespace Mundus
