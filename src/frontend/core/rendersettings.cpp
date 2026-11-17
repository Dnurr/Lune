#include "rendersettings.h"

#include "engine/render/imagerender.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QStandardPaths>

QStringList RenderSettings::defaultPasses()
{
    return { QStringLiteral("rendered"), QStringLiteral("wireframe"), QStringLiteral("clay"), QStringLiteral("unlit"),
             QStringLiteral("preview") };
}

QJsonObject RenderSettings::toJson() const
{
    QJsonObject json;
    json["viewportSize"] = viewportSize;
    json["width"] = width;
    json["height"] = height;
    if (!folder.isEmpty())
        json["folder"] = folder;
    json["passes"] = QJsonArray::fromStringList(passes);
    return json;
}

RenderSettings RenderSettings::fromJson(const QJsonObject &json)
{
    RenderSettings s;
    s.viewportSize = json["viewportSize"].toBool(s.viewportSize);
    s.width = qBound(MinSize, json["width"].toInt(s.width), MaxSize);
    s.height = qBound(MinSize, json["height"].toInt(s.height), MaxSize);
    s.folder = json["folder"].toString();
    if (json.contains("passes")) {
        s.passes.clear();
        for (const QJsonValue &v : json["passes"].toArray()) {
            const QString id = v.toString();
            if (imagePass(id) && !s.passes.contains(id))
                s.passes.append(id);
        }
    }
    return s;
}

QString RenderSettings::outputFolder(const QString &scenePath) const
{
    if (!folder.isEmpty()) {
        // Relative folders are relative to the scene (or to the default place when unsaved).
        if (QDir::isAbsolutePath(folder))
            return QDir::cleanPath(folder);
    }
    const QString base = scenePath.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(QStringLiteral("Lune"))
        : QFileInfo(scenePath).absolutePath();
    return QDir::cleanPath(QDir(base).filePath(folder.isEmpty() ? QStringLiteral("renders") : folder));
}

QString RenderSettings::fileName(const QString &base, int frame, const QString &passId)
{
    QString name = QStringLiteral("%1_frame%2").arg(base).arg(frame, 4, 10, QLatin1Char('0'));
    if (!passId.isEmpty())
        name += QLatin1Char('_') + passId;
    return name + QStringLiteral(".png");
}

QSize RenderSettings::imageSize(const QSize &viewportPixels) const
{
    const QSize size = viewportSize ? viewportPixels : QSize(width, height);
    return size.boundedTo(QSize(MaxSize, MaxSize)).expandedTo(QSize(MinSize, MinSize));
}
