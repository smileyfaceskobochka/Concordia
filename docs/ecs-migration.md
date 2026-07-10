# ECS Migration — Flecs + TOON Integration

## Component Catalog

| Component | Source | Flecs Name | Fields |
|-----------|--------|------------|--------|
| `Transform` | `entity.h` → `components.h` | `Mundus::Transform` | `pos, rot, scale, angularVelocity` |
| `GlobalTransform` | `entity.h` (part of Entity) | `Mundus::GlobalTransform` | `mat4 value` |
| `Name` | `entity.h` | `Mundus::Name` | `string value` |
| `MeshSource` | `entity.h` | `Mundus::MeshSource` | `string value` |
| `Visibility` | `entity.h` | `Mundus::Visibility` | `bool visible` |
| `EffectiveVisibility` | internal cascade | `Mundus::EffectiveVisibility` | `bool visible` |
| `MeshAssetRef` | `types.h` | `Mundus::MeshAssetRef` | `shared_ptr<MeshAsset>` |
| `MaterialRef` | `material.h` | `Mundus::MaterialRef` | `shared_ptr<Material>` |

## Relationships

- `flecs::ChildOf` — parent-child hierarchy (replaces `parentIndex`/`children`)

## Singletons

One entity tagged `SceneGlobals` holds:
- `LightDir` (`vec3`)
- `LightColor` (`vec3`)

## Systems

| System | Trigger | Phase | Query | Work |
|--------|---------|-------|-------|------|
| `TransformSystem` | `OnUpdate` | `PreUpdate` | `Transform` + `ChildOf` (optional) | angularVel → rot, compute local → global mat4, store in `GlobalTransform` |
| `VisibilitySystem` | `OnUpdate` | `PreUpdate` | `Visibility` + `ChildOf` | cascade visible flag → `EffectiveVisibility` |

The compute hierarchy walks roots → children using `flecs::entity::children()`.

## Migration Roadmap

gantt
    title ECS Migration Phases
    dateFormat  YYYY-MM-DD
    section Phase 0
    Design doc + Components header    :done, 2024-01-01, 1d
    section Phase 1
    Systems (Transform, Visibility)   :active, 2024-01-02, 1d
    section Phase 2
    Flecs world in Engine (hybrid)    :2024-01-03, 1d
    section Phase 3
    Render loop → flecs query         :2024-01-04, 1d
    section Phase 4
    Editor hierarchy → flecs          :2024-01-05, 1d
    section Phase 5
    GLTF loading → flecs              :2024-01-06, 1d
    section Phase 6
    TOON serialization → flecs        :2024-01-07, 2d
    section Phase 7
    Picking + cleanup + remove Scene  :2024-01-09, 1d

## Key Design Decisions

1. **No FLECS_IMPLEMENTATION** — flecs v4.x uses amalgamated build (`distr/flecs.c`)
2. **Hybrid mode** during migration: old `Scene` and new `flecs::world` coexist, compile-time guard `CONCORDIA_ECS` toggles consumers
3. **Single-threaded** initially — all systems run on `OnUpdate` in the main loop
4. **Named entities** — use entity names for stable references (replaces index tracking like `m_skyboxEntityIndex`)
5. **TOON remains** — only the iteration target changes from `m_entities` vector to world queries; tagged string format stays compatible

## File Changes

```
src/mundus/components.h       NEW  — all component declarations
src/mundus/systems/           NEW  — transform, visibility, resolve
src/nucleus/engine.h          EDIT — add flecs::world member
src/nucleus/engine.cpp        EDIT — render loop → query, init → flecs
src/vigil/overlay.cpp         EDIT — hierarchy → e.children()
src/vigil/editor_keys.cpp     EDIT — ops → flecs
src/memoria/asset_manager.cpp EDIT — GLTF → flecs entities
src/mundus/scene.cpp          EDIT — save/load → world queries
src/mundus/scene_pick.cpp     EDIT — query world
src/mundus/entity.h           KEEP — Transform struct (used as component)
src/mundus/scene.h            REMOVE — after full migration
```

## Serialization Bridge (TOON ↔ Flecs)

```
TOON entity {
    id: "foo"
    parent: @entity(parent_id)
    transform: { pos: @vec3(...), rot: @quat(...), ... }
    mesh: @asset(assets://...)
    material: { shader: "pbr", base_color: @color(...), ... }
}
    │
    ▼
flecs::entity "foo"
    .set<Name>({"foo"})
    .set<Transform>({pos, rot, scale, angVel})
    .set<MeshSource>({"@asset(...)"})
    .set<MaterialRef>({materialPtr})
    .add(flecs::ChildOf, parentEntity)
```
