#pragma once

#include <QString>
#include <vector>

// A floating-point RGB image: linear radiance, row 0 at the top.
// Used for environment maps (equirectangular HDRIs).
struct HdrImage
{
    int width = 0;
    int height = 0;
    std::vector<float> pixels; // width * height * 3 floats, RGB

    bool isNull() const { return width <= 0 || height <= 0; }
    const float *pixel(int x, int y) const { return pixels.data() + (size_t(y) * size_t(width) + size_t(x)) * 3; }
};

// Halves the image (2x2 box filter) until it is at most `maxWidth` wide. Odd sizes
// drop their last row or column. Used to keep huge HDRIs within a texture budget.
HdrImage downscaled(HdrImage image, int maxWidth);

// Radiance RGBE files (.hdr), the common format of HDRI environment maps.
// Reads flat and run-length encoded scanlines, in the standard "-Y height +X width" orientation.
namespace HdrIO
{
inline const QString FileFilter = QStringLiteral("HDR images (*.hdr)");

// Null image on failure, with the reason in `error`.
HdrImage load(const QString &path, QString *error = nullptr);

// Writes flat (uncompressed) scanlines. Used by the tests and for debugging.
bool save(const HdrImage &image, const QString &path, QString *error = nullptr);
} // namespace HdrIO
