#include "icons.h"

#include <QColor>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "theme.h"

namespace
{
QString resourcePath(const QString &name)
{
    return QStringLiteral(":/icons/") + name + QStringLiteral(".svg");
}

// The SVG with every white fill / stroke in `color`, written once to a temporary file
// (QIcon's SVG engine then draws it sharp at any size and pixel ratio).
QString tinted(const QString &name, const QColor &color)
{
    static QTemporaryDir dir;
    const QString hex = color.name(QColor::HexArgb).mid(1);
    const QString path = dir.filePath(QStringLiteral("%1-%2.svg").arg(name, hex));
    if (QFile::exists(path))
        return path;
    QFile in(resourcePath(name));
    if (!in.open(QIODevice::ReadOnly))
        return {};
    QString svg = QString::fromUtf8(in.readAll());
    static const QRegularExpression white(
        QStringLiteral("(fill|stroke)(:\\s*|=\")(white|#fff(?:fff)?|#FFF(?:FFF)?)"));
    const QString rgb = color.name(QColor::HexRgb);
    svg.replace(white, QStringLiteral("\\1\\2") + rgb);
    if (color.alpha() < 255) // fade the whole drawing
        svg.replace(QRegularExpression(QStringLiteral("<svg\\b")),
                    QStringLiteral("<svg opacity=\"%1\"").arg(color.alphaF(), 0, 'f', 2));
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return {};
    out.write(svg.toUtf8());
    return path;
}
} // namespace

namespace Icons
{
bool exists(const QString &name)
{
    return QFile::exists(resourcePath(name));
}

QIcon get(const QString &name)
{
    if (!exists(name))
        return {};
    const Theme::Palette &p = Theme::currentPalette();
    QColor disabled = p.onSurfaceVariant;
    disabled.setAlphaF(0.38f);
    QIcon icon;
    icon.addFile(tinted(name, p.onSurfaceVariant), QSize(), QIcon::Normal, QIcon::Off);
    icon.addFile(tinted(name, p.onSurface), QSize(), QIcon::Active, QIcon::Off);
    icon.addFile(tinted(name, p.onSecondaryContainer), QSize(), QIcon::Normal, QIcon::On);
    icon.addFile(tinted(name, p.onSecondaryContainer), QSize(), QIcon::Active, QIcon::On);
    icon.addFile(tinted(name, disabled), QSize(), QIcon::Disabled, QIcon::Off);
    icon.addFile(tinted(name, disabled), QSize(), QIcon::Disabled, QIcon::On);
    return icon;
}

QIcon first(std::initializer_list<const char *> names)
{
    for (const char *n : names) {
        const QString name = QString::fromLatin1(n);
        if (exists(name))
            return get(name);
    }
    return {};
}
} // namespace Icons
