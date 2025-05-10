#pragma once

#include <string>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
using LibHandle = HMODULE;
#else
#include <dlfcn.h>
using LibHandle = void *;
#endif

class PluginLoader {
  std::unordered_map<std::string, LibHandle> _libs;

public:
  ~PluginLoader();

  bool loadLibrary(const std::string &name, const std::string &path);

  void *getSymbol(const std::string &libName, const std::string &sym);

  template <typename T>
  T *create(const std::string &libName, const std::string &createFn);

  template <typename T>
  void destroy(const std::string &libName, const std::string &destroyFn,
               T *ptr);
};