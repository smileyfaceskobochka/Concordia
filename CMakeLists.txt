cmake_minimum_required(VERSION 3.20)
project(Concordia LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

add_subdirectory(third_party/SDL)
add_subdirectory(third_party/vk-bootstrap)

add_compile_definitions(CONCORDIA_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# Shader Compilation Automation
file(GLOB SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*.glsl")
set(SHADER_SPVS "")

foreach(SHADER_SRC ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_SRC} NAME)
    get_filename_component(SHADER_NAME_WE ${SHADER_SRC} NAME_WE)
    set(SHADER_SPV "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/compiled/${SHADER_NAME_WE}.spv")
    
    # Determine shader stage based on filename
    set(SHADER_STAGE "")
    if(SHADER_NAME MATCHES "vert")
        set(SHADER_STAGE "vertex")
    elseif(SHADER_NAME MATCHES "frag")
        set(SHADER_STAGE "fragment")
    endif()

    if(SHADER_STAGE)
        add_custom_command(
            OUTPUT ${SHADER_SPV}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/compiled
            COMMAND glslc -fshader-stage=${SHADER_STAGE} ${SHADER_SRC} -o ${SHADER_SPV}
            DEPENDS ${SHADER_SRC}
            COMMENT "Compiling shader: ${SHADER_NAME}"
        )
        list(APPEND SHADER_SPVS ${SHADER_SPV})
    endif()
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_SPVS})

add_executable(Concordia
    src/main.cpp
    src/nucleus/engine.cpp
    src/petra/window.cpp
    src/render/context.cpp
    src/memoria/allocator.cpp
    src/memoria/texture.cpp
    src/memoria/sampler.cpp
    src/memoria/asset_manager.cpp
    src/forma/mesh.cpp
    src/vista/camera.cpp
    src/sensus/input.cpp
    src/vigil/overlay.cpp
    src/lumen/pipeline.cpp
    src/lumen/shader_registry.cpp
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

target_include_directories(Concordia PRIVATE 
    src
    third_party/imgui
    third_party/vma/include
    third_party/glm
    third_party/fast_obj
    third_party/stb
)

# Copy assets directory to where the executable is (optional if using absolute paths in dev)
add_custom_command(
    TARGET Concordia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:Concordia>/assets
)

add_dependencies(Concordia shaders)
target_link_libraries(Concordia PRIVATE SDL3::SDL3 Vulkan::Vulkan vk-bootstrap)

# Add custom target so we can easily run the application: `cmake --build --preset run`
add_custom_target(run
    COMMAND $<TARGET_FILE:Concordia>
    DEPENDS Concordia shaders
    WORKING_DIRECTORY $<TARGET_FILE_DIR:Concordia>
)
