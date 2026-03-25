cmake_minimum_required(VERSION 3.20)
project(Concordia LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

add_subdirectory(third_party/SDL)
add_subdirectory(third_party/vk-bootstrap)

# add_subdirectory(third_party/SDL_image EXCLUDE_FROM_ALL) # Removed SDL_image

add_compile_definitions(CONCORDIA_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/compiled/vert.spv
    COMMAND glslc -fshader-stage=vert ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vert.glsl -o ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/compiled/vert.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/vert.glsl
    COMMENT "Compiling vertex shader"
)

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/compiled/frag.spv
    COMMAND glslc -fshader-stage=frag ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/frag.glsl -o ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/compiled/frag.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/frag.glsl
    COMMENT "Compiling fragment shader"
)

add_custom_target(shaders DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/compiled/vert.spv ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/compiled/frag.spv)

add_executable(triangle
    src/main.cpp
    src/nucleus/engine.cpp
    src/petra/window.cpp
    src/render/context.cpp
    src/memoria/allocator.cpp
    src/memoria/texture.cpp
    src/memoria/sampler.cpp
    src/forma/mesh.cpp
    src/vista/camera.cpp
    src/vigil/overlay.cpp
    src/lumen/pipeline.cpp
    src/mundus/scene.cpp
    third_party/fast_obj/fast_obj.c
    # ImGui core
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    # ImGui backends
    third_party/imgui/backends/imgui_impl_sdl3.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(triangle PRIVATE 
    src
    third_party/imgui
    third_party/vma/include
    third_party/glm
    third_party/fast_obj
    third_party/stb
)

# Copy assets directory to where the executable is (optional if using absolute paths in dev)
add_custom_command(
    TARGET triangle POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:triangle>/assets
)

add_dependencies(triangle shaders)
target_link_libraries(triangle PRIVATE SDL3::SDL3 Vulkan::Vulkan vk-bootstrap)

# Add custom target so we can easily run the application: `cmake --build --preset run`
add_custom_target(run
    COMMAND $<TARGET_FILE:triangle>
    DEPENDS triangle shaders
    WORKING_DIRECTORY $<TARGET_FILE_DIR:triangle>
)
