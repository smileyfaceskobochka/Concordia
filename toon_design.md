# TOON + Concordia Design Spec

## 1. Purpose

TOON is the human-editable source format for Concordia configuration, scenes, and engine metadata.

It is designed to be:

* readable
* deterministic
* easy to validate
* easy to compile into runtime data
* extensible through plugins

TOON is **not** the runtime storage format.

---

## 2. Core Rules

TOON core stays small.

### Built-in value kinds

* string
* number
* bool
* null
* object
* array
* tagged constructor

### Core syntax goals

* indentation-based structure
* no braces for objects
* explicit arrays
* stable key order
* no execution logic
* no expressions

---

## 3. Concordia-Type System

Concordia extends TOON with tagged constructors for engine-specific values.

### Recommended tagged values

* `@vec2(x, y)`
* `@vec3(x, y, z)`
* `@vec4(x, y, z, w)`
* `@quat(x, y, z, w)`
* `@color(r, g, b, a)`
* `@asset(uri)`
* `@primitive(name)`
* `@entity(id)`
* `@uuid(value)`

### Rules

* Use plain literals for normal primitives.
* Use tagged constructors for semantic engine values.
* Do not encode semantic values as plain strings.

Example:

```toon
scene:
  light_dir: @vec3(-0.5, -1.0, -0.2)
  light_color: @vec3(1, 1, 1)
```

---

## 4. Asset References

Concordia uses a virtual asset root.

### Asset URI form

```toon
@asset(assets://models/gltf/DamagedHelmet.glb)
```

### Meaning

* `assets://` is a virtual project-root scheme.
* It is **not** a direct filesystem path.
* The asset resolver maps it to the actual storage backend.
* The backend may be local files, packed archives, editor database entries, or generated resources.

### Rules

* Scene files must not depend on absolute paths.
* Scene files must not depend on host filesystem layout.
* Asset references are opaque identifiers at the scene layer.

### Examples

```toon
mesh: @asset(assets://models/gltf/cube.glb)
shader: @asset(assets://shaders/pbr.shader)
skybox: @asset(assets://textures/skybox.hdr)
```

---

## 5. Primitive Meshes

Primitive meshes are not assets.
They are engine-defined semantic shapes.

### Example

```toon
mesh: @primitive(cube)
```

### Rules

* primitives are resolved by the engine
* primitives do not go through asset import
* primitives are stable and backend-independent

---

## 6. Entity References

Entities use stable IDs.

### Example

```toon
parent: @entity(cube)
```

### Rules

* references must survive reordering
* references must not depend on array indices
* IDs must be unique within the scene scope

---

## 7. Scene Layer

Scene files describe structure and intent.

### Scene layer responsibilities

* entities
* hierarchy
* transforms
* render components
* asset references
* primitive references
* authoring metadata

### Scene layer must not contain

* filesystem paths
* GPU objects
* compiled binaries
* runtime state snapshots
* execution logic

### Example

```toon
scene:
  light_dir: @vec3(-0.5, -1.0, -0.2)
  entities[4]:
    - id: skybox
      name: Skybox
      visible: true
      transform:
        pos: @vec3(0, 0, 0)
        rot: @quat(0, 0, 0, 1)
        scale: @vec3(10, 10, 10)
      mesh: @primitive(cube)
      material:
        shader: skybox
        base_color: @vec4(1, 1, 1, 1)
        roughness: 0.5
        metallic: 0.0
```

---

## 8. Engine Config Layer

Engine config files use the same core syntax, but different semantics.

### Config responsibilities

* window settings
* renderer settings
* input settings
* camera defaults
* debug flags
* backend preferences

### Example

```toon
window:
  title: "Concordia – Scene"
  width: 1920
  height: 1080
  vsync: true
  fullscreen: false
  monitor: 0

renderer:
  max_frames_in_flight: 2
  debug_mode: false
  enable_validation: true
  preferred_gpu: 0

camera:
  fov: 60.0
  near: 0.1
  far: 1000.0
  sensitivity: 0.4
  speed: 5.0
```

---

## 9. Type Safety

Type safety belongs in schemas and plugins, not in the core syntax.

### Strongly recommended explicit types

* vectors
* quaternions
* colors
* asset refs
* entity refs
* UUIDs

### Optional explicit numeric tags

* `@u32(...)`
* `@f32(...)`
* `@i32(...)`

Use these only when exact width matters.

### Do not overuse tags for normal primitives

These are already clear as literals:

* `true`
* `false`
* `0.5`
* `1920`
* `"Cube"`

---

## 10. Concordia Plugin Model

Plugins extend meaning without changing core TOON.

### Plugin categories

* math types
* asset refs
* entity refs
* primitive refs
* ECS schema validation
* prefab handling
* editor metadata
* import hints

### Plugin rules

* plugins must be namespaced
* plugins must be optional unless explicitly required
* unknown plugins must not break core parsing unless marked required
* plugins must not add execution logic to the format

### Good plugin examples

* `concordia.math`
* `concordia.assets`
* `concordia.scene`
* `concordia.ecs`
* `concordia.editor`
* `concordia.prefab`

---

## 11. Schema System

Schemas define valid structure for configs and scenes.

### Example

```toon
@schema Transform
  pos: vec3
  rot: quat
  scale: vec3

@schema Material
  shader: string
  base_color: vec4
  roughness: f32
  metallic: f32
```

### Schema goals

* validation
* better editor tooling
* compile-time conversion
* safer asset pipelines

---

## 12. Loading Pipeline

TOON is an authoring source format.

### Recommended pipeline

1. Parse TOON
2. Resolve tags and plugins
3. Validate against schema
4. Resolve assets
5. Build internal scene IR
6. Compile to runtime ECS / renderer structures

### Important rule

TOON text should never be the hot-path runtime representation.

---

## 13. What TOON Should Be Used For

Use TOON for:

* engine config
* project config
* scene files
* prefab definitions
* ECS component descriptions
* asset references
* editor metadata
* material parameter files
* animation/state data
* versioning and migration metadata

---

## 14. What TOON Should Not Be Used For

Do not use TOON for:

* runtime binary storage
* save-game blobs
* network payloads
* meshes
* textures
* audio
* video
* compiled shaders
* physics caches
* navmeshes
* lock-free hot-path streaming data
* gameplay logic or scripting runtime

---

## 15. File Layout in Concordia

Suggested split:

* `assets/config/engine.toon` → engine config
* `assets/scenes/*.toon` → scenes
* `assets/prefabs/*.toon` → prefabs
* `assets/materials/*.toon` → material params
* `assets/editor/*.toon` → tool metadata

---

## 16. Final Design Rule

TOON describes data.
Plugins define semantics.
The engine resolves references and compiles runtime state.

That is the Concordia boundary.
