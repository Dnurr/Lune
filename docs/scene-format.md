# Scene file format (`.lune`)

A Lune scene is a UTF-8 JSON file. It's meant to be readable and to produce clean diffs
under version control. Reading and writing are in `engine/io/sceneio.cpp`.

## Example

```json
{
    "format": "lune-scene",
    "version": 1,
    "entities": [
        {
            "id": 1,
            "name": "Cube",
            "mesh": "primitive:cube",
            "components": {
                "Transform": {
                    "position": [1.176404, 0.5, 0],
                    "rotation": [0, 0, 0],
                    "scale": [1, 1, 1]
                },
                "Mesh": {
                    "color": "#b9bbc4",
                    "visible": true
                }
            }
        },
        {
            "id": 2,
            "name": "Rock",
            "mesh": "meshes/rock.glb",
            "components": { "Transform": { "position": [0, 0, 3], "rotation": [0, 45, 0], "scale": [2, 2, 2] } }
        }
    ],
    "editor": {
        "camera": { "target": [0, 0, 0], "yaw": 15, "pitch": 30, "distance": 8 }
    }
}
```

## Fields

### Root

| Field | Type | Required | Meaning |
|---|---|---|---|
| `format` | string | yes | Always `"lune-scene"`. |
| `version` | integer | yes | Format version, currently `1`. |
| `entities` | array | no | The entities, in outliner order. |
| `editor` | object | no | Editor state. The engine stores and returns it without reading it. |

### Entity

| Field | Type | Meaning |
|---|---|---|
| `id` | integer | Stable identifier. Missing, zero or duplicate ids are replaced with fresh ones on load. |
| `name` | string | Display name. Default `"Entity"`. |
| `mesh` | string or null | `null`: empty entity. `"primitive:cube"`, `"primitive:sphere"`, `"primitive:plane"`: built-in meshes. Anything else: path of a mesh file (`.obj`, `.gltf`, `.glb`), relative to the scene file. |
| `components` | object | One object per component, keyed by component name, holding its properties. |

### Components

Components and their properties come from the descriptors in
`engine/scene/components.cpp`; only editable properties are written (read-only ones,
like the triangle count, are derived). Current components:

| Component | Property | JSON type |
|---|---|---|
| `Transform` | `position` | `[x, y, z]` |
| | `rotation` | `[x, y, z]`, Euler angles in degrees, applied X then Y then Z |
| | `scale` | `[x, y, z]` |
| `Mesh` (only with a mesh) | `color` | `"#rrggbb"` (sRGB) |
| | `visible` | boolean |
| `Material` (only with a mesh) | `roughness` | number, 0–1 (default 0.5) |
| | `metallic` | number, 0–1 (default 0) |
| `Light` (only on lights) | `type` | `"directional"` or `"point"` |
| | `color` | `"#rrggbb"` (sRGB) |
| | `intensity` | number ≥ 0 (radiance; for point lights, at 1 unit of distance) |
| | `enabled` | boolean |

An entity with a `Light` component is a light. Its transform places it: a point light
shines from its position, a directional light along its local −Y axis (straight down
with no rotation).

Value types by property type: `Float` → number, `Vector3` → array of 3 numbers,
`Color` → `"#rrggbb"` string, `Bool` → boolean, `Text` → string, `Enum` → the option id
string (an unknown id keeps the default and is reported as a warning). Numbers are written
with at most 7 significant digits (all a `float` holds).

### Editor state

Written by the editor (`Editor::editorState()`):

| Field | Meaning |
|---|---|
| `camera.target` | Orbit center `[x, y, z]`. |
| `camera.yaw`, `camera.pitch` | Degrees; pitch is clamped to ±89. |
| `camera.distance` | Distance from the target, clamped to 0.5–500. |
| `display.exposure` | Viewport exposure in EV, clamped to ±10. |
| `display.tonemapper` | `"none"`, `"reinhard"`, `"aces"` or `"agx"`; unknown values fall back to `"none"`. |

## Compatibility rules

These keep old files readable by new versions and make failures clear:

- **Unknown** fields, components and properties are ignored.
- **Missing** properties keep their default value.
- A property with a **wrong type** keeps its default and is reported as a warning.
- A **missing mesh file** is reported once per file; the entity is loaded without a mesh.
- A file whose `version` is **newer** than the reader's is refused with an error.
- A file that isn't JSON, or whose `format` isn't `lune-scene`, is refused.
- A failed load leaves the current scene untouched.

When the format changes in a way old readers can't handle, increase `SceneIO::Version`
and convert older versions on load.

## Mesh references

Mesh files are stored **relative to the scene file** (with `/` separators), so a
project folder can be moved or shared as a whole. A file on another drive is stored
with its absolute path. When several entities use the same file, it's loaded once and
the mesh is shared.

## Writing

Files are written with `QSaveFile`: the content goes to a temporary file that replaces
the old one only when it's complete, so a crash or a full disk never leaves a
half-written scene.
