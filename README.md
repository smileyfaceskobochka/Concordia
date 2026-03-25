# 🔥 Concordia Dev-Log (Kinda Sorta)

## 📅 Dev‑Log Entry: 25.03.26 - The Great CMake & vk-bootstrap Migration

Today, the entire foundation underwent a massive seismic shift. Meson was ripped out, and raw Vulkan initialization was purged in favor of a modern, streamlined, and highly robust architecture. The triangle no longer just sits there squashed—it spins dynamically, aspect-corrected, and time-driven.

<small>🫃 Building the engine, one struct at a time...</small>

### What Achieved

#### 1. Pure CMake + Ninja Build System
* **No more shell scripts or Meson:** Transitioned completely to a `CMakeLists.txt` driven by `CMakePresets.json`.
* **One-command builds:** `cmake --build --preset run` gracefully handles configuring, building, shader compilation, and execution.
* **Hermetic submodules:** `SDL3` and `vk-bootstrap` are now statically linked right from `third_party/`, ensuring guaranteed cross-platform reproducibility without system package hunting.

#### 2. vk-bootstrap Refactoring
* Ripped out hundreds of lines of fragile Vulkan boilerplate.
* Replaced with `vkb::InstanceBuilder`, `vkb::PhysicalDeviceSelector`, `vkb::DeviceBuilder`, and `vkb::SwapchainBuilder`.
* The engine now handles API version bounds, validation layers, debug messengers, and optimal surface formats implicitly and safely.

#### 3. Zero-Stdlib (Almost) & High-Precision Timing
* Nuked `<fstream>`, `<chrono>`, and `<iostream>`.
* Replaced file loading with `SDL_LoadFile`, logging with `SDL_Log`, and timing with `SDL_GetPerformanceCounter`.
* Animation now uses double-precision time to prevent float drift over long sessions.

#### 4. Rock-Solid Swapchains & Wayland Fixes
* **Aspect-ratio correction:** Passed dynamically via Vulkan Push Constants so the triangle is no longer squashed.
* **HiDPI/Wayland correctness:** Replaced logical window dimensions with `SDL_GetWindowSizeInPixels()` during swapchain creation to prevent resolution mismatches on scaling compositors.
* **Safe Resizing:** Moved swapchain recreation out of the synchronous event loop and into the top of the `drawFrame` boundary using a deferred `needsResize` flag.

#### 5. The Latin Architecture (Phase 1 Complete)
* Extracted the monolithic `main.cpp` into a clean, modular namespace architecture.
* **`Petra::Window`**: Manages the SDL3 window, OS events, and Vulkan surface.
* **`Render::Context`**: Owns Vulkan initialization, queues, and swapchain recreation via `vk-bootstrap`.
* **`Vigil::Overlay`**: Encapsulates `imgui` integration across SDL3 and Vulkan backends.
* **`Nucleus::Engine`**: The irreducible center. Owns the render loop, pipeline, and sync primitives.
* `main.cpp` is now a 10-line file that just instantiates `Nucleus::Engine` and runs.

<small>The triangle spins, therefore I am.</small>

---

### 🫃 Next on My Course

#### 1. Vertex & Index Buffers (Memory & Form)
* Introduce `VulkanMemoryAllocator` (VMA) as a submodule.
* Start loading arbitrary geometry (vertices with positions and colors) using VMA-backed `VkBuffer`s.
* Abstract a `Mesh` structure to handle vertex inputs.

#### 3. 3D Math & Uniform Buffers
* Integrate `glm` for vector and matrix math.
* Implement a `Camera` class (View/Projection matrices).
* Pass the MVP (Model-View-Projection) data to the shaders via Uniform Buffers instead of just relying on Push Constants.

#### 4. Depth Buffering & 3D Rendering
* Introduce a depth attachment to the RenderPass.
* Migrate from a 2D rotating triangle to proper 3D cubes with perspective projection.

---

## 📅 Dev‑Log Entry: 25.03.26 (Night) - Dimensions & Textures

With the foundation solid, the engine expanded into the third dimension and underwent a serious dependency detox. The squashed triangle is officially dead; long live the spinning, depth-tested cube.

<small>📦 Allocating the future, one texel at a time...</small>

### What Achieved

#### 1. VMA & Resource Management (`Memoria`)
* **Submodule Integration:** Fully integrated `VulkanMemoryAllocator` (VMA).
* **Unified Allocation:** All buffers (Vertex, Index) and Images are now managed via `Memoria::Allocator`, providing stable GPU memory management with minimal overhead.

#### 2. 3D Engine Core (`Vista`, `Forma`)
* **Perspective Camera:** Implemented `Vista::Camera` to handle View and Projection matrices.
* **Depth Testing:** Added a depth buffer and configured the pipeline to handle occlusion, finally making 3D geometry look correct.
* **Mesh Abstraction:** `Forma::Mesh` now supports UV coordinates and includes a primitive generator for spinning cubes.

#### 3. stb_image Detox & Pathing
* **Purged SDL_image:** Ripped out the complex `SDL_image` dependency and replaced it with the lightweight, header-only `stb_image.h`.
* **Centralized Assets:** Reorganized the build system to output compiled SPIR-V shaders directly to `assets/shaders/compiled/`.
* **Robust Pathing:** Introduced the `CONCORDIA_ASSETS_DIR` compile-time macro, allowing the engine to reliably find textures and shaders regardless of the working directory.

#### 4. Modern Pipeline Integration (`Lumen`)
* Extended `Lumen::Pipeline` to support `VkDescriptorSetLayout`, enabling the wiring of combined image samplers.
* Synchronized descriptor set updates with the texture loading lifecycle.

<small>The cube spins in depth, though its colors remain a mystery for now.</small>

---

#### Texture Pipeline — Fixed (Three Root Causes)
* **Missing UVs in OBJ:** `cube.obj` had no `vt` entries at all. `fast_obj` silently returned `(0.0, 0.0)` for every vertex, making every face sample the same single grey texel. Added proper `vt` entries with per-face UV assignments.
* **Wrong image format:** Texture was declared as `VK_FORMAT_R8G8B8A8_UNORM` during debugging. PNG stores sRGB data — sampling linearly skips gamma correction, making everything look flat and desaturated. Restored `VK_FORMAT_R8G8B8A8_SRGB`.
* **Dark vertex tint:** Some cube vertices had colors as low as `{0.1, 0.1, 0.1}`. The shader computes `texture * vertexColor`, so dark vertices washed out entire faces. Set all to white `{1.0, 1.0, 1.0}`.

<small>The cube finally has a face. And it looks great.</small>

---

Suggestions for future me: Try Slang for shaders, it's supposed to be better than glslc.