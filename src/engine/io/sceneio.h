#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

class Scene;

// Scene files (.lune): JSON, readable and diff-friendly.
//
//   {
//     "format": "lune-scene", "version": 1,
//     "entities": [
//       { "id": 1, "name": "Cube", "mesh": "primitive:cube",
//         "components": { "Transform": { "position": [0, 0.5, 0], ... },
//                         "Mesh": { "color": "#b9bbc4", "visible": true } } }
//     ],
//     "editor": { ... }   // opaque to the engine: camera etc., written by the editor
//   }
//
// Components are written generically from their descriptors (engine/scene/components.h):
// every editable property of every component the entity has. Unknown components or
// properties in a file are skipped, missing ones keep their default, so the format
// survives new fields. Imported meshes and file properties (an environment's image) are
// stored by path, relative to the scene file when possible; each mesh file is loaded once
// even if many entities use it.
namespace SceneIO
{
constexpr int Version = 1;
inline const QString FileFilter = QStringLiteral("Lune scene (*.lune)");

bool save(const Scene &scene, const QString &path, const QJsonObject &editorState, QString *error = nullptr);

// Replaces the content of `scene`. On failure the scene is left untouched.
// `warnings`: problems that didn't stop the load (e.g. a mesh file that's missing).
bool load(Scene &scene, const QString &path, QJsonObject *editorState = nullptr, QStringList *warnings = nullptr,
          QString *error = nullptr);
} // namespace SceneIO
