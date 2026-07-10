set_project("Concordia")
set_version("0.1.0")
set_xmakever("2.8.8")
add_rules("mode.debug", "mode.release")

set_languages("cxx23")

if is_plat("linux") then
    set_toolchains("clang", {default = true})
    add_cxflags("-Wall", "-Wextra", "-Wpedantic",
                "-Wno-unused-parameter", "-Wno-missing-field-initializers",
                "-fcolor-diagnostics", "-fansi-escape-codes")
    if is_mode("debug") then
        add_cxflags("-fsanitize=address,undefined", "-fno-omit-frame-pointer")
        add_ldflags("-fsanitize=address,undefined")
    end
end

local SUBMODULE_DIR = path.join(os.projectdir(), ".xmake_submodules")
local NPROC = tostring(os.cpuinfo().ncpu or 4)

target("Concordia")
    set_kind("binary")

    add_files("src/**.cpp")
    add_files("third_party/imgui/*.cpp")
    add_files("third_party/imgui/backends/imgui_impl_sdl3.cpp")
    add_files("third_party/imgui/backends/imgui_impl_vulkan.cpp")
    add_files("third_party/vk-bootstrap/src/VkBootstrap.cpp")
    add_files("third_party/flecs/distr/flecs.c")

    add_includedirs("src")
    add_includedirs("third_party/imgui")
    add_includedirs("third_party/imgui/backends")
    add_includedirs("third_party/vma/include")
    add_includedirs("third_party/glm")
    add_includedirs("third_party/cgltf")
    add_includedirs("third_party/stb")
    add_includedirs("third_party/Vulkan-Headers/include")
    add_includedirs("third_party/vk-bootstrap/src")
    add_includedirs("third_party/ctoon")
    add_includedirs("third_party/flecs/distr")

    add_defines([[CONCORDIA_ASSETS_DIR="$(projectdir)/assets"]])

    local sdl_build_dir = path.join(SUBMODULE_DIR, "SDL3")
    local loader_build_dir = path.join(SUBMODULE_DIR, "Vulkan-Loader")
    local loader_lib_dir = path.join(loader_build_dir, "loader")

    add_linkdirs(sdl_build_dir)
    add_linkdirs(loader_lib_dir)
    add_links("SDL3", "vulkan")

    if is_plat("linux") then
        add_syslinks("dl", "pthread", "m", "rt")
        add_syslinks("X11", "Xext", "xcb", "Xfixes", "Xcursor", "Xi", "Xrandr")
        add_syslinks("wayland-client", "wayland-egl", "wayland-cursor")
        add_syslinks("xkbcommon")
        add_rpathdirs("$ORIGIN")
    end

    before_build(function(target)
        -- SDL3 via CMake
        if not os.isdir(sdl_build_dir) then
            os.mkdir(sdl_build_dir)
            os.execv("cmake", {
                "-S", path.join(os.projectdir(), "third_party/SDL"),
                "-B", sdl_build_dir, "-G", "Ninja",
                "-DSDL_SHARED=OFF",
                "-DSDL_STATIC=ON",
                "-DSDL_TEST=OFF",
                "-DSDL_X11=ON",
                "-DSDL_WAYLAND=ON",
                "-DSDL_PULSEAUDIO=OFF",
            })
        end
        os.execv("cmake", {"--build", sdl_build_dir, "--target", "SDL3-static",
                           "--", "-j" .. NPROC})

        -- Vulkan-Loader via CMake
        local mock_dir = path.join(SUBMODULE_DIR, "VulkanHeadersMock")
        os.mkdir(mock_dir)
        io.writefile(path.join(mock_dir, "VulkanHeadersConfig.cmake"),
            [[set(VulkanHeaders_FOUND TRUE)
if(NOT TARGET Vulkan::Headers)
  add_library(Vulkan-Headers INTERFACE)
  target_include_directories(Vulkan-Headers INTERFACE "${VULKAN_HEADERS_INSTALL_DIR}/include")
  add_library(Vulkan::Headers ALIAS Vulkan-Headers)
endif()]])
        io.writefile(path.join(mock_dir, "VulkanHeadersConfigVersion.cmake"),
            [[set(PACKAGE_VERSION "1.4.347")
set(PACKAGE_VERSION_COMPATIBLE TRUE)
set(PACKAGE_VERSION_EXACT TRUE)]])

        if not os.isdir(loader_build_dir) then
            os.mkdir(loader_build_dir)
            os.execv("cmake", {
                "-S", path.join(os.projectdir(), "third_party/Vulkan-Loader"),
                "-B", loader_build_dir, "-G", "Ninja",
                "-DVULKAN_HEADERS_INSTALL_DIR=" .. path.join(os.projectdir(), "third_party/Vulkan-Headers"),
                "-DVulkanHeaders_DIR=" .. mock_dir,
                "-DBUILD_TESTS=OFF",
                "-DBUILD_WSI_XCB_SUPPORT=ON",
                "-DBUILD_WSI_XLIB_SUPPORT=ON",
                "-DBUILD_WSI_WAYLAND_SUPPORT=ON",
            })
        end
        os.execv("cmake", {"--build", loader_build_dir, "--target", "vulkan",
                           "--", "-j" .. NPROC})
    end)

    after_build(function(target)
        local assets_src = path.join(os.projectdir(), "assets")
        local out = path.join(target:targetdir(), "assets")
        os.rm(out)
        os.cp(assets_src, out)
        print("Assets copied to " .. out)

        local loader_so = path.join(loader_lib_dir, "libvulkan.so")
        if os.isfile(loader_so) then
            os.cp(loader_so, path.join(target:targetdir(), "libvulkan.so"))
            print("libvulkan.so copied to " .. target:targetdir())
        end
    end)

task("run")
    on_run(function()
        local builddir = import("core.project.config").builddir()
        local exe = os.files(path.join(builddir, "**/Concordia"))
        if #exe == 0 then exe = os.files(path.join(builddir, "**/Concordia.exe")) end
        if #exe == 0 then raise("Concordia binary not found") end
        os.cd(path.directory(exe[1]))
        os.exec("./" .. path.filename(exe[1]))
    end)
    set_menu {usage = "xmake run", description = "Build and run Concordia"}

task("generate-defaults")
    on_run(function()
        local assets = os.projectdir() .. "/assets"
        local endash = "\226\128\147"

        local cfg = assets .. "/config/engine.toon"
        if not os.exists(cfg) then
            io.writefile(cfg, "window:\n" ..
                "  title: \"Concordia " .. endash .. " Scene\"\n" ..
                "  width: 1920\n" ..
                "  height: 1080\n" ..
                "  vsync: true\n" ..
                "  fullscreen: false\n" ..
                "  monitor: 0\n" ..
                "\n" ..
                "renderer:\n" ..
                "  max_frames_in_flight: 2\n" ..
                "  debug_mode: false\n" ..
                "  enable_validation: true\n" ..
                "  preferred_gpu: 0\n" ..
                "\n" ..
                "camera:\n" ..
                "  fov: 60.0\n" ..
                "  near: 0.1\n" ..
                "  far: 1000.0\n" ..
                "  sensitivity: 0.4\n" ..
                "  speed: 5.0\n")
            print("Generated " .. cfg)
        else
            print("Exists " .. cfg)
        end

        local manifest = assets .. "/config/assets.toon"
        if not os.exists(manifest) then
            io.writefile(manifest, "preload_meshes[3]: \"@primitive(cube)\", \"@asset(assets://models/gltf/DamagedHelmet.glb)\", \"@asset(assets://models/gltf/cube.glb)\"\n" ..
                "\n" ..
                "default_skybox: \"@asset(assets://images/skybox/cubemap/Cubemap_Sky_01-512x512.png)\"\n")
            print("Generated " .. manifest)
        else
            print("Exists " .. manifest)
        end

        local scene = assets .. "/scenes/default.toon"
        if not os.exists(scene) then
            io.writefile(scene, "scene:\n" ..
                "  light_dir: \"@vec3(-0.5,-1,-0.2)\"\n" ..
                "  light_color: \"@vec3(1,1,1)\"\n")
            print("Generated " .. scene)
        else
            print("Exists " .. scene)
        end

        local uipath = assets .. "/config/ui.toon"
        if not os.exists(uipath) then
            io.writefile(uipath, "fonts[1]:\n" ..
                "  - path: \"assets/fonts/JetBrainsMonoNF/JetBrainsMonoNerdFont-Regular.ttf\"\n" ..
                "    size: 16.0\n" ..
                "    glyph_offset_y: 0.0\n" ..
                "\n" ..
                "frame_padding: 6.0\n" ..
                "item_spacing: 8.0\n" ..
                "window_rounding: 6.0\n" ..
                "frame_rounding: 3.0\n" ..
                "scrollbar_size: 12.0\n" ..
                "grab_min_size: 8.0\n" ..
                "stats_padding: 10.0\n" ..
                "inspector_width: 350.0\n" ..
                "asset_window_size: \"@vec2(420,320)\"\n" ..
                "rename_buf_size: 256.0\n" ..
                "slider_speed_min: 0.1\n" ..
                "slider_speed_max: 50.0\n" ..
                "slider_sens_min: 0.01\n" ..
                "slider_sens_max: 1.0\n" ..
                "debug_modes[5]: \"None\", \"Metallic\", \"Roughness\", \"Normals\", \"Vertex Color\"\n" ..
                "descriptor_pool_sets: 11000.0\n" ..
                "descriptor_pool_samplers: 1000.0\n" ..
                "descriptor_pool_combined_image_samplers: 1000.0\n")
            print("Generated " .. uipath)
        else
            print("Exists " .. uipath)
        end

        local ekpath = assets .. "/config/editor_keys.toon"
        if not os.exists(ekpath) then
            io.writefile(ekpath, "camera_forward: \"W\"\n" ..
                "camera_backward: \"S\"\n" ..
                "camera_left: \"A\"\n" ..
                "camera_right: \"D\"\n" ..
                "camera_up: \"SPACE\"\n" ..
                "camera_down: \"LSHIFT\"\n" ..
                "capture_exit: \"ESCAPE\"\n" ..
                "\n" ..
                "bindings[8]:\n" ..
                "  - action: \"delete\"\n" ..
                "    key: \"DELETE\"\n" ..
                "  - action: \"delete\"\n" ..
                "    key: \"X\"\n" ..
                "  - action: \"toggle_visibility\"\n" ..
                "    key: \"H\"\n" ..
                "  - action: \"show_all\"\n" ..
                "    key: \"H\"\n" ..
                "    alt: true\n" ..
                "  - action: \"toggle_selection\"\n" ..
                "    key: \"A\"\n" ..
                "  - action: \"focus_camera\"\n" ..
                "    key: \"GRAVE\"\n" ..
                "  - action: \"duplicate\"\n" ..
                "    key: \"D\"\n" ..
                "    shift: true\n" ..
                "  - action: \"hide_cursor\"\n" ..
                "    key: \"ESCAPE\"\n")
            print("Generated " .. ekpath)
        else
            print("Exists " .. ekpath)
        end

        local rppath = assets .. "/config/render_pipelines.toon"
        if not os.exists(rppath) then
            io.writefile(rppath, "pipelines[3]:\n" ..
                "  - name: \"pbr\"\n" ..
                "    vertex_shader: \"assets://shaders/compiled/vert.spv\"\n" ..
                "    fragment_shader: \"assets://shaders/compiled/pbr_frag.spv\"\n" ..
                "    depth_test: true\n" ..
                "    depth_write: true\n" ..
                "    depth_compare: \"LESS\"\n" ..
                "    cull_mode: \"BACK\"\n" ..
                "    push_constant_size: 160.0\n" ..
                "\n" ..
                "  - name: \"skybox\"\n" ..
                "    vertex_shader: \"assets://shaders/compiled/skybox_vert.spv\"\n" ..
                "    fragment_shader: \"assets://shaders/compiled/skybox_frag.spv\"\n" ..
                "    depth_test: true\n" ..
                "    depth_write: false\n" ..
                "    depth_compare: \"ALWAYS\"\n" ..
                "    cull_mode: \"NONE\"\n" ..
                "    push_constant_size: 160.0\n" ..
                "\n" ..
                "  - name: \"skybox_hdri\"\n" ..
                "    vertex_shader: \"assets://shaders/compiled/skybox_vert.spv\"\n" ..
                "    fragment_shader: \"assets://shaders/compiled/skybox_hdri_frag.spv\"\n" ..
                "    depth_test: true\n" ..
                "    depth_write: false\n" ..
                "    depth_compare: \"ALWAYS\"\n" ..
                "    cull_mode: \"NONE\"\n" ..
                "    push_constant_size: 160.0\n")
            print("Generated " .. rppath)
        else
            print("Exists " .. rppath)
        end
    end)
    set_menu {usage = "xmake generate-defaults", description = "Generate default TOON config and scene files if missing"}
