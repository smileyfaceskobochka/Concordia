project('modular_triangle', 'cpp', default_options: ['cpp_std=c++23'])

yaml = dependency('yaml-cpp')
sdl3 = dependency('sdl3', required: true)
vulkan = dependency('vulkan', required: true)

vert_spv = custom_target('vert_spv',
  input: 'shaders/vert.glsl',
  output: 'vert.spv',
  command: ['glslc', '-fshader-stage=vert', '@INPUT@', '-o', '@OUTPUT@'],
  build_by_default: true
)

frag_spv = custom_target('frag_spv',
  input: 'shaders/frag.glsl',
  output: 'frag.spv',
  command: ['glslc', '-fshader-stage=frag', '@INPUT@', '-o', '@OUTPUT@'],
  build_by_default: true
)


spv_targets = [vert_spv, frag_spv]

# Подключаем поддиректории (после объявления spv_targets)
subdir('src')
subdir('plugins/SDL3Window')
subdir('plugins/VulkanGraphics')
