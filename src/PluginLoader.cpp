#include "PluginLoader.h"
#include "ISystemGraphics.h"
#include "ISystemWindow.h"

#include <iostream>

#ifndef _WIN32
#include <dlfcn.h>
#endif

PluginLoader::~PluginLoader() {
  for (auto &[name, handle] : _libs) {
#ifdef _WIN32
    FreeLibrary(handle);
#else
    dlclose(handle);
#endif
  }
}

bool PluginLoader::loadLibrary(const std::string &name,
                               const std::string &path) {
  LibHandle handle = nullptr;
#ifdef _WIN32
  handle = LoadLibraryA(path.c_str());
  if (!handle) {
    std::cerr << "Failed to load " << path << std::endl;
    return false;
  }
#else
  handle = dlopen(path.c_str(), RTLD_NOW);
  if (!handle) {
    std::cerr << "Failed to load " << path << std::endl;
    std::cerr << "dlerror: " << dlerror() << std::endl;
    return false;
  }
#endif
  _libs[name] = handle;
  return true;
}

void *PluginLoader::getSymbol(const std::string &libName,
                              const std::string &symbol) {
  auto it = _libs.find(libName);
  if (it == _libs.end())
    return nullptr;
#ifdef _WIN32
  return (void *)GetProcAddress(it->second, symbol.c_str());
#else
  return dlsym(it->second, symbol.c_str());
#endif
}

template <typename T>
T *PluginLoader::create(const std::string &libName,
                        const std::string &createFn) {
  using CreateFunc = T *(*)();
  auto fn = reinterpret_cast<CreateFunc>(getSymbol(libName, createFn));
  if (!fn) {
    std::cerr << "Failed to find symbol: " << createFn << " in " << libName
              << std::endl;
    return nullptr;
  }
  return fn();
}

template <typename T>
void PluginLoader::destroy(const std::string &libName,
                           const std::string &destroyFn, T *ptr) {
  using DestroyFunc = void (*)(T *);
  auto fn = reinterpret_cast<DestroyFunc>(getSymbol(libName, destroyFn));
  if (fn)
    fn(ptr);
}

// Explicit template instantiations
template ISystemWindow *
PluginLoader::create<ISystemWindow>(const std::string &, const std::string &);
template void PluginLoader::destroy<ISystemWindow>(const std::string &,
                                                   const std::string &,
                                                   ISystemWindow *);

template ISystemGraphics *
PluginLoader::create<ISystemGraphics>(const std::string &, const std::string &);
template void PluginLoader::destroy<ISystemGraphics>(const std::string &,
                                                     const std::string &,
                                                     ISystemGraphics *);
