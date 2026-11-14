#include "hdrimage.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace
{
// Shared exponent encoding: three 8-bit mantissas and one exponent (Greg Ward's RGBE).
void decodeRgbe(const unsigned char *rgbe, float *rgb)
{
    if (rgbe[3] == 0) {
        rgb[0] = rgb[1] = rgb[2] = 0.0f;
        return;
    }
    const float f = std::ldexp(1.0f, int(rgbe[3]) - (128 + 8));
    rgb[0] = rgbe[0] * f;
    rgb[1] = rgbe[1] * f;
    rgb[2] = rgbe[2] * f;
}

void encodeRgbe(const float *rgb, unsigned char *rgbe)
{
    const float v = std::max({ rgb[0], rgb[1], rgb[2] });
    if (!(v > 1e-32f)) { // also catches NaN
        rgbe[0] = rgbe[1] = rgbe[2] = rgbe[3] = 0;
        return;
    }
    int e = 0;
    const float scale = std::frexp(v, &e) * 256.0f / v;
    for (int i = 0; i < 3; ++i)
        rgbe[i] = (unsigned char)std::clamp(int(std::max(rgb[i], 0.0f) * scale), 0, 255);
    rgbe[3] = (unsigned char)(e + 128);
}

// Reads the scanlines that follow the header into `out` (width * 4 RGBE bytes per row).
bool readScanlines(const unsigned char *p, const unsigned char *end, int width, int height,
                   std::vector<unsigned char> &out, QString &error)
{
    out.resize(size_t(width) * size_t(height) * 4);
    for (int y = 0; y < height; ++y) {
        unsigned char *row = out.data() + size_t(y) * size_t(width) * 4;
        if (end - p < 4) {
            error = QStringLiteral("the file is truncated");
            return false;
        }
        // New run-length encoding: 2, 2, width (16 bit), then each channel separately.
        const bool rle = width >= 8 && width < 0x8000 && p[0] == 2 && p[1] == 2 && !(p[2] & 0x80);
        if (!rle) {
            const size_t bytes = size_t(width) * 4;
            if (size_t(end - p) < bytes) {
                error = QStringLiteral("the file is truncated");
                return false;
            }
            std::copy(p, p + bytes, row);
            p += bytes;
            continue;
        }
        if (((p[2] << 8) | p[3]) != width) {
            error = QStringLiteral("a scanline has the wrong width");
            return false;
        }
        p += 4;
        for (int channel = 0; channel < 4; ++channel) {
            int x = 0;
            while (x < width) {
                if (p >= end) {
                    error = QStringLiteral("the file is truncated");
                    return false;
                }
                int count = *p++;
                const bool run = count > 128;
                if (run)
                    count -= 128;
                if (count == 0 || x + count > width || end - p < (run ? 1 : count)) {
                    error = QStringLiteral("a scanline is corrupted");
                    return false;
                }
                for (int i = 0; i < count; ++i)
                    row[size_t(x + i) * 4 + size_t(channel)] = run ? *p : p[i];
                p += run ? 1 : count;
                x += count;
            }
        }
    }
    return true;
}
} // namespace

HdrImage downscaled(HdrImage image, int maxWidth)
{
    while (image.width > maxWidth && image.width >= 2 && image.height >= 2) {
        HdrImage half;
        half.width = image.width / 2;
        half.height = image.height / 2;
        half.pixels.resize(size_t(half.width) * size_t(half.height) * 3);
        for (int y = 0; y < half.height; ++y) {
            for (int x = 0; x < half.width; ++x) {
                float *dst = half.pixels.data() + (size_t(y) * size_t(half.width) + size_t(x)) * 3;
                for (int c = 0; c < 3; ++c) {
                    dst[c] = 0.25f
                        * (image.pixel(2 * x, 2 * y)[c] + image.pixel(2 * x + 1, 2 * y)[c]
                           + image.pixel(2 * x, 2 * y + 1)[c] + image.pixel(2 * x + 1, 2 * y + 1)[c]);
                }
            }
        }
        image = std::move(half);
    }
    return image;
}

namespace HdrIO
{
HdrImage load(const QString &path, QString *error)
{
    const QString name = QFileInfo(path).fileName();
    auto fail = [&](const QString &reason) {
        if (error)
            *error = QStringLiteral("Cannot read %1: %2").arg(name, reason);
        return HdrImage {};
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(file.errorString());
    const QByteArray data = file.readAll();
    const auto *p = reinterpret_cast<const unsigned char *>(data.constData());
    const unsigned char *end = p + data.size();

    auto readLine = [&]() -> QByteArray {
        const unsigned char *start = p;
        while (p < end && *p != '\n')
            ++p;
        QByteArray line(reinterpret_cast<const char *>(start), int(p - start));
        if (p < end)
            ++p; // the newline
        return line;
    };

    // Header: "#?RADIANCE" (or "#?RGBE"), variable lines, an empty line, then the resolution.
    if (!readLine().startsWith("#?"))
        return fail(QStringLiteral("not a Radiance HDR file"));
    for (;;) {
        if (p >= end)
            return fail(QStringLiteral("the header has no end"));
        const QByteArray line = readLine().trimmed();
        if (line.isEmpty())
            break;
        if (line.startsWith("FORMAT=") && line != "FORMAT=32-bit_rle_rgbe")
            return fail(QStringLiteral("unsupported pixel format (%1)").arg(QString::fromLatin1(line.mid(7))));
    }
    static const QRegularExpression resolution(QStringLiteral("^-Y\\s+(\\d+)\\s+\\+X\\s+(\\d+)$"));
    const QString resLine = QString::fromLatin1(readLine().trimmed());
    const QRegularExpressionMatch m = resolution.match(resLine);
    if (!m.hasMatch())
        return fail(QStringLiteral("unsupported orientation or resolution \"%1\"").arg(resLine));
    const int height = m.captured(1).toInt();
    const int width = m.captured(2).toInt();
    if (width <= 0 || height <= 0 || width > 65536 || height > 65536)
        return fail(QStringLiteral("invalid size %1 x %2").arg(width).arg(height));

    std::vector<unsigned char> rgbe;
    QString reason;
    if (!readScanlines(p, end, width, height, rgbe, reason))
        return fail(reason);

    HdrImage image;
    image.width = width;
    image.height = height;
    image.pixels.resize(size_t(width) * size_t(height) * 3);
    for (size_t i = 0; i < size_t(width) * size_t(height); ++i)
        decodeRgbe(rgbe.data() + i * 4, image.pixels.data() + i * 3);
    return image;
}

bool save(const HdrImage &image, const QString &path, QString *error)
{
    QSaveFile file(path);
    if (image.isNull() || !file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(path, image.isNull() ? QStringLiteral("empty image") : file.errorString());
        return false;
    }
    file.write("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n");
    file.write(QStringLiteral("-Y %1 +X %2\n").arg(image.height).arg(image.width).toLatin1());
    QByteArray pixels(image.width * image.height * 4, Qt::Uninitialized);
    for (int i = 0; i < image.width * image.height; ++i)
        encodeRgbe(image.pixels.data() + size_t(i) * 3, reinterpret_cast<unsigned char *>(pixels.data()) + i * 4);
    file.write(pixels);
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}
} // namespace HdrIO
