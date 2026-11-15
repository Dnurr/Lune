#include "imageloader.h"

#include "gltfloader.h"

#include <QBuffer>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>

bool splitGltfImageReference(const QString &reference, QString *file, int *index)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^(.*)#image(\d+)$)"));
    const QRegularExpressionMatch m = pattern.match(reference);
    if (!m.hasMatch())
        return false;
    if (file)
        *file = m.captured(1);
    if (index)
        *index = m.captured(2).toInt();
    return true;
}

QImage loadImage(const QString &reference, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return QImage();
    };

    QImage image;
    QString gltf;
    int index = 0;
    if (splitGltfImageReference(reference, &gltf, &index)) {
        QString e;
        const QByteArray bytes = loadGltfImageData(gltf, index, &e);
        if (bytes.isEmpty())
            return fail(e);
        QBuffer buffer;
        buffer.setData(bytes);
        QImageReader reader(&buffer);
        if (!reader.read(&image))
            return fail(QStringLiteral("%1, image %2: %3").arg(QFileInfo(gltf).fileName()).arg(index).arg(reader.errorString()));
    } else {
        if (!QFileInfo::exists(reference))
            return fail(QStringLiteral("%1 not found").arg(reference));
        QImageReader reader(reference);
        reader.setAutoTransform(true); // EXIF orientation
        if (!reader.read(&image))
            return fail(QStringLiteral("%1: %2").arg(QFileInfo(reference).fileName(), reader.errorString()));
    }

    if (image.width() > MaxImageSize || image.height() > MaxImageSize)
        image = image.scaled(MaxImageSize, MaxImageSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image.convertToFormat(QImage::Format_RGBA8888);
}
