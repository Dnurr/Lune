#pragma once

#include <QImage>
#include <QString>

// Decodes an image for a texture: a file Qt can read (PNG, JPEG, BMP...), or an image inside
// a glTF file, referenced as "<gltf file>#image<N>" (see GltfMaterial). The result is
// RGBA8888 (straight alpha), at most MaxImageSize pixels on its longer side (larger images
// are scaled down). A null image and `error` set on failure. Thread-safe: the renderer
// calls it off the main thread.
constexpr int MaxImageSize = 8192;
QImage loadImage(const QString &reference, QString *error = nullptr);

// Splits "<gltf file>#image<N>" into the file and N; false for a plain path.
bool splitGltfImageReference(const QString &reference, QString *file, int *index);
