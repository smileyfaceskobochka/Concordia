#pragma once

#include "ISystemGraphics.h"
#include "ISystemWindow.h"
#include "PluginLoader.h"

#include <memory>
#include <string>

class Engine {
  PluginLoader _loader;
  ISystemWindow *_window = nullptr;
  ISystemGraphics *_graphics = nullptr;
  std::string _winName;
  std::string _gfxName;

public:
  bool init(const std::string &configPath);
  void run();
  void shutdown();
};