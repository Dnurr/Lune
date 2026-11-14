#include "sceneio.h"

#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QVector3D>

#include <algorithm>
#include <vector>

#include "engine/scene/components.h"
#include "engine/scene/primitives.h"
#include "engine/scene/scene.h"
#include "meshimport.h"

namespace
{
const QString kFormat = QStringLiteral("lune-scene");

// Values are floats: written as doubles they show conversion noise (1.1764039993286133).
// 7 significant digits are all a float holds, and keep the file clean in diffs.
double tidy(double v)
{
    return QString::number(v, 'g', 7).toDouble();
}

QJsonValue toJson(PropertyType type, const QVariant &v)
{
    switch (type) {
    case PropertyType::Float:
        return tidy(v.toDouble());
    case PropertyType::Vector3: {
        const QVector3D x = v.value<QVector3D>();
        return QJsonArray { tidy(x.x()), tidy(x.y()), tidy(x.z()) };
    }
    case PropertyType::Color:
        return v.value<QColor>().name(QColor::HexRgb);
    case PropertyType::Bool:
        return v.toBool();
    case PropertyType::Text:
        return v.toString();
    case PropertyType::Enum:
    case PropertyType::File:
        break; // need more context: see propertyToJson
    }
    return {};
}

// Relative to the scene file, so a project folder can be moved as a whole.
// (On another drive relativeFilePath returns the absolute path.)
QString relativePath(const QString &path, const QDir &sceneDir)
{
    return QDir::fromNativeSeparators(sceneDir.relativeFilePath(path));
}

QJsonValue propertyToJson(const PropertyDesc &p, const QVariant &v, const QDir &sceneDir)
{
    if (p.type == PropertyType::Enum)
        return p.options.value(v.toInt());
    if (p.type == PropertyType::File)
        return v.toString().isEmpty() ? QString() : relativePath(v.toString(), sceneDir);
    return toJson(p.type, v);
}

// Invalid QVariant if the JSON value doesn't fit the type.
QVariant fromJson(PropertyType type, const QJsonValue &j)
{
    switch (type) {
    case PropertyType::Float:
        return j.isDouble() ? QVariant(j.toDouble()) : QVariant();
    case PropertyType::Vector3: {
        const QJsonArray a = j.toArray();
        if (a.size() != 3 || !a[0].isDouble() || !a[1].isDouble() || !a[2].isDouble())
            return {};
        return QVariant::fromValue(QVector3D(float(a[0].toDouble()), float(a[1].toDouble()), float(a[2].toDouble())));
    }
    case PropertyType::Color: {
        const QColor c = QColor::fromString(j.toString());
        return c.isValid() ? QVariant(c) : QVariant();
    }
    case PropertyType::Bool:
        return j.isBool() ? QVariant(j.toBool()) : QVariant();
    case PropertyType::Text:
        return j.isString() ? QVariant(j.toString()) : QVariant();
    case PropertyType::Enum:
    case PropertyType::File:
        break; // need more context: see propertyFromJson
    }
    return {};
}

QVariant propertyFromJson(const PropertyDesc &p, const QJsonValue &j, const QDir &sceneDir)
{
    if (p.type == PropertyType::Enum) {
        const int index = p.options.indexOf(j.toString());
        return index >= 0 ? QVariant(index) : QVariant();
    }
    if (p.type == PropertyType::File) {
        if (!j.isString())
            return {};
        return j.toString().isEmpty() ? QString() : QDir::cleanPath(sceneDir.absoluteFilePath(j.toString()));
    }
    return fromJson(p.type, j);
}

QString meshReference(const MeshPtr &mesh, const QDir &sceneDir)
{
    if (mesh->source.startsWith(QLatin1String("primitive:")))
        return mesh->source;
    return relativePath(mesh->source, sceneDir);
}
} // namespace

namespace SceneIO
{
bool save(const Scene &scene, const QString &path, const QJsonObject &editorState, QString *error)
{
    const QDir sceneDir = QFileInfo(path).absoluteDir();
    const auto &comps = components();

    QJsonArray entities;
    for (const Entity &e : scene.entities()) {
        QJsonObject je;
        je["id"] = double(e.id);
        je["name"] = e.name;
        je["mesh"] = e.mesh && !e.mesh->source.isEmpty() ? QJsonValue(meshReference(e.mesh, sceneDir)) : QJsonValue();

        QJsonObject jc;
        for (const ComponentDesc &c : comps) {
            if (!c.present(e))
                continue;
            QJsonObject props;
            for (int i = 0; i < int(c.properties.size()); ++i) {
                const PropertyDesc &p = c.properties[size_t(i)];
                if (!p.readOnly) // read-only values are derived (e.g. triangle count)
                    props[p.name] = propertyToJson(p, c.get(e, i), sceneDir);
            }
            jc[c.name] = props;
        }
        je["components"] = jc;
        entities.append(je);
    }

    QJsonObject root;
    root["format"] = kFormat;
    root["version"] = Version;
    root["entities"] = entities;
    if (!editorState.isEmpty())
        root["editor"] = editorState;

    // QSaveFile writes to a temporary file and renames it at the end:
    // a crash or a full disk never leaves a half-written scene.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool load(Scene &scene, const QString &path, QJsonObject *editorState, QStringList *warnings, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    auto warn = [warnings](const QString &message) {
        if (warnings)
            warnings->append(message);
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("Cannot open %1: %2").arg(path, file.errorString()));
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull())
        return fail(QStringLiteral("%1 is not valid JSON (%2)").arg(QFileInfo(path).fileName(), parseError.errorString()));
    const QJsonObject root = doc.object();
    if (root["format"].toString() != kFormat)
        return fail(QStringLiteral("%1 is not a Lune scene").arg(QFileInfo(path).fileName()));
    if (root["version"].toInt() > Version)
        return fail(QStringLiteral("%1 was saved by a newer version of Lune (format %2, this one reads up to %3)")
                        .arg(QFileInfo(path).fileName())
                        .arg(root["version"].toInt())
                        .arg(Version));

    // Build everything first, touch the scene only at the end (no half-loaded scene on errors).
    const QDir sceneDir = QFileInfo(path).absoluteDir();
    const auto &comps = components();
    QHash<QString, MeshPtr> meshCache; // absolute path -> mesh, each file loaded once
    QHash<QString, bool> missing;      // report each missing file once
    std::vector<Entity> entities;
    QHash<EntityId, bool> usedIds;

    for (const QJsonValue &v : root["entities"].toArray()) {
        const QJsonObject je = v.toObject();
        Entity e;
        e.id = EntityId(je["id"].toDouble());
        if (e.id == InvalidEntity || usedIds.contains(e.id))
            e.id = InvalidEntity; // assigned below
        else
            usedIds.insert(e.id, true);
        e.name = je["name"].toString(QStringLiteral("Entity"));

        const QString meshRef = je["mesh"].toString();
        if (meshRef.startsWith(QLatin1String("primitive:"))) {
            e.mesh = Primitives::fromSource(meshRef);
            if (!e.mesh)
                warn(QStringLiteral("%1: unknown built-in mesh \"%2\"").arg(e.name, meshRef));
        } else if (!meshRef.isEmpty()) {
            const QString abs = QDir::cleanPath(sceneDir.absoluteFilePath(meshRef));
            if (MeshPtr cached = meshCache.value(abs)) {
                e.mesh = cached;
            } else if (!missing.contains(abs)) {
                QString meshError;
                e.mesh = importMesh(abs, &meshError);
                if (e.mesh)
                    meshCache.insert(abs, e.mesh);
                else {
                    missing.insert(abs, true);
                    warn(meshError);
                }
            }
        }

        const QJsonObject jc = je["components"].toObject();
        for (const ComponentDesc &c : comps) {
            if (!jc.contains(c.name))
                continue;
            if (!c.present(e) && c.add)
                c.add(e); // optional component (e.g. Light) saved in the file
            if (!c.present(e))
                continue;
            const QJsonObject props = jc[c.name].toObject();
            for (int i = 0; i < int(c.properties.size()); ++i) {
                const PropertyDesc &p = c.properties[size_t(i)];
                if (p.readOnly || !props.contains(p.name))
                    continue;
                const QVariant value = propertyFromJson(p, props[p.name], sceneDir);
                if (value.isValid()) {
                    c.set(e, i, value);
                    // Kept anyway: the file may come back, and saving must not lose the reference.
                    if (p.type == PropertyType::File && !value.toString().isEmpty() && !QFileInfo::exists(value.toString()))
                        warn(QStringLiteral("%1: file not found: %2").arg(e.name, QDir::toNativeSeparators(value.toString())));
                } else
                    warn(QStringLiteral("%1: invalid value for %2.%3").arg(e.name, c.name, p.name));
            }
        }
        entities.push_back(std::move(e));
    }

    // Entities without a usable id get fresh ones above the highest saved id.
    EntityId next = 1;
    for (const Entity &e : entities)
        next = std::max(next, e.id + 1);
    for (Entity &e : entities) {
        if (e.id == InvalidEntity)
            e.id = next++;
    }

    scene.clear();
    for (const Entity &e : entities)
        scene.insertEntity(e);
    if (editorState)
        *editorState = root["editor"].toObject();
    return true;
}
} // namespace SceneIO
