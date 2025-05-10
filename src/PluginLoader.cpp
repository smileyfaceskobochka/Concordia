#include "PluginLoader.h"
#include "ISystemGraphics.h"
#include "ISystemWindow.h"

#include <iostream>

PluginLoader::~PluginLoader() {
  for (auto &[name, h] : _libs) {
#ifdef _WIN32
    FreeLibrary(h);
#else
    dlclose(h);
#endif
  }
}

bool PluginLoader::loadLibrary(const std::string &name,
                               const std::string &path) {
  LibHandle h = nullptr;
#ifdef _WIN32
  h = LoadLibraryA(path.c_str());
#else
  h = dlopen(path.c_str(), RTLD_NOW);
#endif
  if (!h) {
    std::cerr << "Failed to load " << path << "\n";
    return false;
  }
  _libs[name] = h;
  return true;
}

void *PluginLoader::getSymbol(const std::string &libName,
                              const std::string &sym) {
  auto it = _libs.find(libName);
  if (it == _libs.end())
    return nullptr;
#ifdef _WIN32
  return (void *)GetProcAddress(it->second, sym.c_str());
#else
  return dlsym(it->second, sym.c_str());
#endif
}

template <typename T>
T *PluginLoader::create(const std::string &libName,
                        const std::string &createFn) {
  using Fn = T *(*)();
  auto f = (Fn)getSymbol(libName, createFn);
  return f ? f() : nullptr;
}

template <typename T>
void PluginLoader::destroy(const std::string &libName,
                           const std::string &destroyFn, T *ptr) {
  using Fn = void (*)(T *);
  auto f = (Fn)getSymbol(libName, destroyFn);
  if (f)
    f(ptr);
}

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
