#include "Engine.h"

#include <iostream>
#include <yaml-cpp/yaml.h>

bool Engine::init(const std::string &configPath) {
  // 1. Парсим YAML
  YAML::Node c = YAML::LoadFile(configPath);
  std::string pluginDir = c["PluginPath"].as<std::string>();
  std::string winName = c["WindowSystem"].as<std::string>();
  std::string gfxName = c["GraphicsDevice"].as<std::string>();

  _winName = winName;
  _gfxName = gfxName;

  // 2. Загружаем либы
  if (!_loader.loadLibrary(winName, pluginDir + "/" + winName + ".so"))
    return false;
  if (!_loader.loadLibrary(gfxName, pluginDir + "/" + gfxName + ".so"))
    return false;

  // 3. Создаем подсистемы
  _window = _loader.create<ISystemWindow>(winName, "CreateWindowSystem");
  _graphics = _loader.create<ISystemGraphics>(gfxName, "CreateGraphicsDevice");
  if (!_window || !_graphics)
    return false;

  // 4. Инициализируем окно и Vulkan
  if (!_window->init(800, 600, "Modular Triangle"))
    return false;
  auto exts = _window->getRequiredInstanceExtensions();
  if (!_graphics->initInstance(exts))
    return false;
  if (!_graphics->createSurface(
          _window->createSurface(_graphics->getInstance())))
    return false;
  if (!_graphics->pickPhysicalDevice())
    return false;
  if (!_graphics->createLogicalDevice())
    return false;
  if (!_graphics->createSwapchain())
    return false;
  if (!_graphics->createFrameResources())
    return false;

  return true;
}

void Engine::run() {
  while (_window->pollEvents()) {
    _graphics->renderFrame();
  }
}

void Engine::shutdown() {
  if (_graphics) {
    _loader.destroy<ISystemGraphics>(
      _gfxName,
      "DestroyGraphicsDevice",
      _graphics
    );
    _graphics = nullptr;
  }
  if (_window) {
    _loader.destroy<ISystemWindow>(
      _winName,
      "DestroyWindowSystem",
      _window
    );
    _window = nullptr;
  }
}