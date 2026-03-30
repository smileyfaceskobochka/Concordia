cmake_minimum_required(VERSION 3.20)

# ============================================================
# Build System Policy — Prefer Clang, fall back to system
# ============================================================
# 💡 Future Me note: Consider cmrc (https://github.com/vector-of-bool/cmrc) 
# for embedding shaders/textures into the executable, leading to a single 
# self-contained binary like Godot.

if(NOT DEFINED CMAKE_CXX_COMPILER)
    find_program(CLANG_CXX_EXECUTABLE NAMES clang++ clang++-15 clang++-16 clang++-17)
    find_program(CLANG_C_EXECUTABLE   NAMES clang   clang-15   clang-16   clang-17)
    
    if(CLANG_CXX_EXECUTABLE AND CLANG_C_EXECUTABLE)
        set(CMAKE_CXX_COMPILER "${CLANG_CXX_EXECUTABLE}" CACHE FILEPATH "C++ compiler" FORCE)
        set(CMAKE_C_COMPILER   "${CLANG_C_EXECUTABLE}"   CACHE FILEPATH "C compiler"   FORCE)
        message(STATUS "Concordia Build System: Clang located and prioritized — ${CLANG_CXX_EXECUTABLE}")
    else()
        message(STATUS "Concordia Build System: Clang not found — falling back to system defaults")
    endif()
endif()

project(Concordia LANGUAGES CXX C)

# ============================================================
# C++ Standard
# ============================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Compiler Flags
# ============================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Concordia: Compiler — Clang ${CMAKE_CXX_COMPILER_VERSION}")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter        # ImGui/cgltf headers are noisy
        -Wno-missing-field-initializers
        -fcolor-diagnostics          # Keep colors through Ninja
        -fansi-escape-codes
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
        message(STATUS "Concordia: ASan + UBSan enabled (Debug)")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "Concordia: Compiler — GCC ${CMAKE_CXX_COMPILER_VERSION} (Clang preferred)")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        -Wno-missing-field-initializers
        -fdiagnostics-color=always
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
elseif(MSVC)
    message(STATUS "Concordia: Compiler — MSVC (Clang-cl strongly preferred for performance)")
    add_compile_options(/W3 /WX- /utf-8 /MP) # /MP for multi-processor builds
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /Oi /Ot /Gy)
    endif()
endif()

# ============================================================
# Dependencies
# ============================================================
find_package(Vulkan REQUIRED)
add_subdirectory(third_party/SDL)
add_subdirectory(third_party/vk-bootstrap)

# ============================================================
# Asset Path Macro
# ============================================================
add_compile_definitions(CONCORDIA_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# ============================================================
# Shader Compilation
# ============================================================
file(GLOB SHADER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/*.glsl")
set(SHADER_SPVS "")

foreach(SHADER_SRC ${SHADER_SOURCES})
    get_filename_component(SHADER_NAME    ${SHADER_SRC} NAME)
    get_filename_component(SHADER_NAME_WE ${SHADER_SRC} NAME_WE)

    set(SHADER_SPV "${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/compiled/${SHADER_NAME_WE}.spv")

    set(SHADER_STAGE "")
    if(SHADER_NAME MATCHES "vert")
        set(SHADER_STAGE "vertex")
    elseif(SHADER_NAME MATCHES "frag")
        set(SHADER_STAGE "fragment")
    elseif(SHADER_NAME MATCHES "comp")
        set(SHADER_STAGE "compute")
    elseif(SHADER_NAME MATCHES "geom")
        set(SHADER_STAGE "geometry")
    endif()

    if(SHADER_STAGE)
        add_custom_command(
            OUTPUT  ${SHADER_SPV}
            COMMAND ${CMAKE_COMMAND} -E make_directory
                    ${CMAKE_CURRENT_SOURCE_DIR}/assets/shaders/compiled
            COMMAND glslc -fshader-stage=${SHADER_STAGE} ${SHADER_SRC} -o ${SHADER_SPV}
            DEPENDS ${SHADER_SRC}
            COMMENT "Compiling shader: ${SHADER_NAME} → ${SHADER_NAME_WE}.spv"
        )
        list(APPEND SHADER_SPVS ${SHADER_SPV})
    endif()
endforeach()

add_custom_target(shaders DEPENDS ${SHADER_SPVS})

# ============================================================
# Engine Sources
# ============================================================
add_executable(Concordia
    src/main.cpp

    # Modules
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

    # Third-party C
    third_party/fast_obj/fast_obj.c

    # ImGui core
    third_party/imgui/imgui.cpp
    # third_party/imgui/imgui_demo.cpp
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
    third_party/imgui/backends
    third_party/vma/include
    third_party/glm
    third_party/fast_obj
    third_party/cgltf
    third_party/stb
)

target_link_libraries(Concordia PRIVATE
    SDL3::SDL3
    Vulkan::Vulkan
    vk-bootstrap
)

add_dependencies(Concordia shaders)

# ============================================================
# Post-Build: Copy Assets
# ============================================================
add_custom_command(
    TARGET Concordia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:Concordia>/assets
    COMMENT "Copying assets to build directory"
)

# ============================================================
# Run Target: cmake --build --preset run
# ============================================================
add_custom_target(run
    COMMAND $<TARGET_FILE:Concordia>
    DEPENDS Concordia shaders
    WORKING_DIRECTORY $<TARGET_FILE_DIR:Concordia>
    COMMENT "Running Concordia..."
)