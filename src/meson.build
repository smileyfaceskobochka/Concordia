libloader = static_library('PluginLoader', ['PluginLoader.cpp'])

executable(
  'triangle_engine',
  ['main.cpp', 'Engine.cpp'],
  dependencies : [ yaml ],
  link_with    : libloader,
  link_depends : spv_targets
)
