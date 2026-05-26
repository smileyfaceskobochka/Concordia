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
        import("lib.detect.find_tool")

        -- Shaders
        local glslc = find_tool("glslc")
        if not glslc then
            raise("glslc not found - install Vulkan SDK or add VULKAN_SDK/bin to PATH")
        end
        local shader_dir = path.join(os.projectdir(), "assets/shaders")
        local compiled_dir = path.join(shader_dir, "compiled")
        os.mkdir(compiled_dir)
        for _, src in ipairs(os.files(path.join(shader_dir, "*.glsl"))) do
            local fn = path.filename(src)
            local base = path.basename(src)
            local stage
            if fn:find("vert") then
                stage = "vertex"
            elseif fn:find("frag") then
                stage = "fragment"
            elseif fn:find("comp") then
                stage = "compute"
            elseif fn:find("geom") then
                stage = "geometry"
            end
            if stage then
                local out = path.join(compiled_dir, base .. ".spv")
                print("  glslc: " .. fn .. " -> " .. path.filename(out))
                os.execv(glslc.program, {"-fshader-stage=" .. stage, src, "-o", out})
            end
        end

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
