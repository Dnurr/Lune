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
#include <functional>
#include <vector>

#include "engine/scene/animation.h"
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

// The image references in a material graph's texture values ({ "image": ..., ... }),
// passed through `map`. A reference into a glTF ("<file>#image<N>") maps its file part.
NodeGraph mapImages(NodeGraph graph, const std::function<QString(const QString &)> &map)
{
    for (GraphNode &n : graph.nodes) {
        for (auto it = n.values.begin(); it != n.values.end(); ++it) {
            if (it.value().metaType().id() != QMetaType::QVariantMap)
                continue;
            QVariantMap texture = it.value().toMap();
            const QString image = texture.value(QStringLiteral("image")).toString();
            if (image.isEmpty())
                continue;
            const int hash = image.lastIndexOf(QLatin1String("#image"));
            texture[QStringLiteral("image")] = hash > 0 ? map(image.left(hash)) + image.mid(hash) : map(image);
            it.value() = texture;
        }
    }
    return graph;
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
        if (e.materials.size() == 1) {
            je["material"] = double(e.materials[0]);
        } else if (!e.materials.empty()) {
            QJsonArray slotIds; // one per submesh, 0 = none
            for (MaterialId m : e.materials)
                slotIds.append(double(m));
            je["materials"] = slotIds;
        }

        QJsonObject jc;
        for (const ComponentDesc &c : comps) {
            if (!c.present(e))
                continue;
            QJsonObject props;
            for (int i = 0; i < int(c.properties.size()); ++i) {
                const PropertyDesc &p = c.properties[size_t(i)];
                if (!p.readOnly) // read-only values are derived (e.g. triangle count)
                    props[p.key()] = propertyToJson(p, c.get(e, i), sceneDir);
            }
            jc[c.name] = props;
        }
        je["components"] = jc;

        QJsonArray animation;
        for (const AnimationTrack &t : e.animation) {
            int ci = 0, pi = 0;
            if (t.keys.empty() || !Animation::resolve(t, &ci, &pi))
                continue;
            const PropertyDesc &p = comps[size_t(ci)].properties[size_t(pi)];
            QJsonArray keys;
            for (const Keyframe &k : t.keys) {
                QJsonObject jk;
                jk["frame"] = tidy(k.frame);
                jk["value"] = propertyToJson(p, k.value, sceneDir);
                if (Animation::interpolates(p.type))
                    jk["interpolation"] = Animation::interpolationName(k.interpolation);
                keys.append(jk);
            }
            animation.append(QJsonObject { { "component", t.component }, { "property", t.property }, { "keys", keys } });
        }
        if (!animation.isEmpty())
            je["animation"] = animation;
        entities.append(je);
    }

    QJsonArray materials;
    for (const Material &m : scene.materials()) {
        QJsonObject jm;
        jm["id"] = double(m.id);
        jm["name"] = m.name;
        jm["graph"] = mapImages(m.graph, [&](const QString &path) { return relativePath(path, sceneDir); }).toJson();
        if (m.alphaMode != AlphaMode::Opaque) {
            jm["alphaMode"] = Material::alphaModeName(m.alphaMode);
            jm["alphaCutoff"] = double(m.alphaCutoff);
        }
        materials.append(jm);
    }

    QJsonObject root;
    root["format"] = kFormat;
    root["version"] = Version;
    root["entities"] = entities;
    if (!materials.isEmpty())
        root["materials"] = materials;
    const Timeline &t = scene.timeline();
    root["timeline"] = QJsonObject { { "start", t.start }, { "end", t.end }, { "fps", t.fps } };
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
        if (je["materials"].isArray()) {
            for (const QJsonValue &m : je["materials"].toArray())
                e.materials.push_back(MaterialId(m.toDouble()));
        } else if (je.contains("material")) {
            e.materials.push_back(MaterialId(je["material"].toDouble()));
        }

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
                if (p.readOnly || !props.contains(p.key()))
                    continue;
                const QVariant value = propertyFromJson(p, props[p.key()], sceneDir);
                if (value.isValid()) {
                    c.set(e, i, value);
                    // Kept anyway: the file may come back, and saving must not lose the reference.
                    if (p.type == PropertyType::File && !value.toString().isEmpty() && !QFileInfo::exists(value.toString()))
                        warn(QStringLiteral("%1: file not found: %2").arg(e.name, QDir::toNativeSeparators(value.toString())));
                } else
                    warn(QStringLiteral("%1: invalid value for %2.%3").arg(e.name, c.name, p.key()));
            }
        }

        for (const QJsonValue &tv : je["animation"].toArray()) {
            const QJsonObject jt = tv.toObject();
            AnimationTrack t;
            t.component = jt["component"].toString();
            t.property = jt["property"].toString();
            int ci = 0, pi = 0;
            if (!Animation::resolve(t, &ci, &pi)) {
                warn(QStringLiteral("%1: can't animate %2.%3").arg(e.name, t.component, t.property));
                continue;
            }
            const PropertyDesc &p = comps[size_t(ci)].properties[size_t(pi)];
            for (const QJsonValue &kv : jt["keys"].toArray()) {
                const QJsonObject jk = kv.toObject();
                Keyframe k;
                k.frame = jk["frame"].toDouble();
                k.value = propertyFromJson(p, jk["value"], sceneDir);
                k.interpolation = Animation::interpolationFromName(jk["interpolation"].toString()).value_or(Interpolation::Bezier);
                if (!k.value.isValid() || !jk["frame"].isDouble()) {
                    warn(QStringLiteral("%1: invalid key of %2.%3").arg(e.name, t.component, t.property));
                    continue;
                }
                if (Animation::keyAt(t, k.frame) < 0)
                    t.keys.push_back(k);
            }
            std::sort(t.keys.begin(), t.keys.end(), [](const Keyframe &a, const Keyframe &b) { return a.frame < b.frame; });
            if (!t.keys.empty())
                e.animation.push_back(std::move(t));
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

    std::vector<Material> materials;
    QHash<MaterialId, bool> usedMaterials;
    for (const QJsonValue &v : root["materials"].toArray()) {
        const QJsonObject jm = v.toObject();
        Material m;
        m.id = MaterialId(jm["id"].toDouble());
        m.name = jm["name"].toString(QStringLiteral("Material"));
        m.graph = mapImages(NodeGraph::fromJson(jm["graph"].toObject()), [&](const QString &path) {
            return QDir::cleanPath(sceneDir.absoluteFilePath(path));
        });
        m.alphaMode = Material::alphaModeFromName(jm["alphaMode"].toString());
        m.alphaCutoff = float(qBound(0.0, jm["alphaCutoff"].toDouble(0.5), 1.0));
        if (m.id == NoMaterial || usedMaterials.contains(m.id)) {
            warn(QStringLiteral("Material %1: missing or duplicate id, skipped").arg(m.name));
            continue;
        }
        usedMaterials.insert(m.id, true);
        materials.push_back(std::move(m));
    }
    for (Entity &e : entities) {
        for (int slot = 0; slot < int(e.materials.size()); ++slot) {
            const MaterialId m = e.materials[size_t(slot)];
            if (m != NoMaterial && !usedMaterials.contains(m)) {
                warn(QStringLiteral("%1: unknown material %2").arg(e.name).arg(m));
                e.setMaterial(slot, NoMaterial);
            }
        }
    }

    scene.clear();
    for (const Material &m : materials)
        scene.insertMaterial(m);
    for (const Entity &e : entities)
        scene.insertEntity(e);
    {
        const QJsonObject jt = root["timeline"].toObject();
        Timeline t;
        t.start = jt["start"].toInt(t.start);
        t.end = jt["end"].toInt(t.end);
        t.fps = jt["fps"].toDouble(t.fps);
        scene.setTimeline(t);
    }
    if (editorState)
        *editorState = root["editor"].toObject();
    return true;
}
} // namespace SceneIO
