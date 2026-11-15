#include "objloader.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>

#include <algorithm>
#include <map>
#include <tuple>

namespace
{
struct Corner
{
    int position = -1; // 0-based
    int uv = -1;       // 0-based, -1 = none
    int normal = -1;   // 0-based, -1 = none
};

// OBJ indices are 1-based; negative ones count back from the end of the list.
int resolveIndex(const QStringView token, int count, bool *ok)
{
    const int i = token.toInt(ok);
    if (!*ok || i == 0)
        return *ok = false, -1;
    const int r = i > 0 ? i - 1 : count + i;
    *ok = r >= 0 && r < count;
    return r;
}
} // namespace

MeshPtr loadObj(const QString &path, QString *error)
{
    auto fail = [error](const QString &message) -> MeshPtr {
        if (error)
            *error = message;
        return nullptr;
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QStringLiteral("Cannot open %1").arg(path));

    std::vector<QVector3D> positions;
    std::vector<QVector3D> normals;
    std::vector<QVector2D> uvs;
    auto mesh = std::make_shared<Mesh>();
    mesh->name = QFileInfo(path).completeBaseName();
    mesh->source = QFileInfo(path).absoluteFilePath();

    // Corners with a normal are shared between faces; key = (position, uv, normal).
    std::map<std::tuple<int, int, int>, quint32> shared;
    // Triangles per material (usemtl), in order of first use: one submesh each.
    std::vector<std::pair<QString, std::vector<quint32>>> groups(1);
    size_t group = 0;

    QTextStream in(&file);
    QString line;
    int lineNo = 0;
    std::vector<Corner> face;
    while (in.readLineInto(&line)) {
        ++lineNo;
        const QStringView l = QStringView(line).trimmed();
        if (l.isEmpty() || l.startsWith(u'#'))
            continue;
        const auto parts = l.split(u' ', Qt::SkipEmptyParts);
        const QStringView kind = parts.front();

        if (kind == u"v" || kind == u"vn") {
            if (parts.size() < 4)
                return fail(QStringLiteral("Line %1: expected 3 numbers").arg(lineNo));
            const QVector3D p(parts[1].toFloat(), parts[2].toFloat(), parts[3].toFloat());
            (kind == u"v" ? positions : normals).push_back(p);
        } else if (kind == u"vt") {
            if (parts.size() < 2)
                return fail(QStringLiteral("Line %1: expected texture coordinates").arg(lineNo));
            // OBJ has v = 0 at the bottom of the image; Lune (like glTF) at the top.
            uvs.push_back(QVector2D(parts[1].toFloat(), 1.0f - (parts.size() > 2 ? parts[2].toFloat() : 0.0f)));
        } else if (kind == u"usemtl") {
            const QString name = parts.size() > 1 ? l.mid(6).trimmed().toString() : QString();
            auto it = std::find_if(groups.begin(), groups.end(), [&](const auto &g) { return g.first == name; });
            if (it == groups.end()) {
                if (groups.size() == 1 && groups[0].second.empty())
                    groups[0].first = name; // nothing before the first usemtl
                else
                    groups.push_back({ name, {} });
                it = std::find_if(groups.begin(), groups.end(), [&](const auto &g) { return g.first == name; });
            }
            group = size_t(it - groups.begin());
        } else if (kind == u"f") {
            face.clear();
            for (qsizetype i = 1; i < parts.size(); ++i) {
                const auto fields = parts[i].split(u'/');
                Corner c;
                bool ok = false;
                c.position = resolveIndex(fields[0], int(positions.size()), &ok);
                if (!ok)
                    return fail(QStringLiteral("Line %1: bad vertex index").arg(lineNo));
                if (fields.size() >= 2 && !fields[1].isEmpty()) {
                    c.uv = resolveIndex(fields[1], int(uvs.size()), &ok);
                    if (!ok)
                        return fail(QStringLiteral("Line %1: bad texture coordinate index").arg(lineNo));
                }
                if (fields.size() >= 3 && !fields[2].isEmpty()) {
                    c.normal = resolveIndex(fields[2], int(normals.size()), &ok);
                    if (!ok)
                        return fail(QStringLiteral("Line %1: bad normal index").arg(lineNo));
                }
                face.push_back(c);
            }
            if (face.size() < 3)
                continue;

            const bool hasNormals = std::all_of(face.begin(), face.end(), [](const Corner &c) { return c.normal >= 0; });
            QVector3D flat;
            if (!hasNormals) {
                // Newell's method: robust normal for any (even slightly non-planar) polygon.
                for (size_t i = 0; i < face.size(); ++i) {
                    const QVector3D &a = positions[face[i].position];
                    const QVector3D &b = positions[face[(i + 1) % face.size()].position];
                    flat += QVector3D((a.y() - b.y()) * (a.z() + b.z()), (a.z() - b.z()) * (a.x() + b.x()),
                                      (a.x() - b.x()) * (a.y() + b.y()));
                }
                flat.normalize();
            }

            std::vector<quint32> ids;
            ids.reserve(face.size());
            for (const Corner &c : face) {
                const QVector2D uv = c.uv >= 0 ? uvs[c.uv] : QVector2D();
                if (hasNormals) {
                    const auto key = std::make_tuple(c.position, c.uv, c.normal);
                    auto it = shared.find(key);
                    if (it == shared.end()) {
                        it = shared.emplace(key, quint32(mesh->vertices.size())).first;
                        mesh->vertices.push_back({ positions[c.position], normals[c.normal].normalized(), uv });
                    }
                    ids.push_back(it->second);
                } else {
                    ids.push_back(quint32(mesh->vertices.size()));
                    mesh->vertices.push_back({ positions[c.position], flat, uv });
                }
            }
            std::vector<quint32> &tris = groups[group].second;
            for (size_t i = 1; i + 1 < ids.size(); ++i)
                tris.insert(tris.end(), { ids[0], ids[i], ids[i + 1] });
        }
        // o, g, s, mtllib: ignored
    }

    for (auto &[name, tris] : groups) {
        if (tris.empty())
            continue;
        mesh->submeshes.push_back({ quint32(mesh->indices.size()), quint32(tris.size()), name, -1 });
        mesh->indices.insert(mesh->indices.end(), tris.begin(), tris.end());
    }

    if (mesh->indices.empty())
        return fail(QStringLiteral("%1 contains no faces").arg(QFileInfo(path).fileName()));
    mesh->finish();
    return mesh;
}
