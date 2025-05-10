project('triangle', 'cpp', default_options: ['cpp_std=c++23'])
sdl = dependency('sdl3', required: true)
vul = dependency('vulkan', required: true)
vert = custom_target(
  'vert',
  input: 'shaders/vert.glsl',
  output: 'vert.spv',
  build_by_default: true,
  command: ['glslc', '-fshader-stage=vert', '@INPUT@', '-o', '@OUTPUT@'],
)
frag = custom_target(
  'frag',
  input: 'shaders/frag.glsl',
  output: 'frag.spv',
  build_by_default: true,
  command: ['glslc', '-fshader-stage=frag', '@INPUT@', '-o', '@OUTPUT@'],
)
exe = executable(
  'triangle',
  'src/main.cpp',
  dependencies: [sdl, vul],
  link_depends: [vert, frag],
)