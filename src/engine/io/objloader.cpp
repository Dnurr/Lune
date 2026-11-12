#include "objloader.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTextStream>

#include <algorithm>

namespace
{
struct Corner
{
    int position = -1; // 0-based
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
    auto mesh = std::make_shared<Mesh>();
    mesh->name = QFileInfo(path).completeBaseName();

    // Corners with a normal are shared between faces; key = (position, normal).
    QHash<quint64, quint32> shared;

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
        } else if (kind == u"f") {
            face.clear();
            for (qsizetype i = 1; i < parts.size(); ++i) {
                const auto fields = parts[i].split(u'/');
                Corner c;
                bool ok = false;
                c.position = resolveIndex(fields[0], int(positions.size()), &ok);
                if (!ok)
                    return fail(QStringLiteral("Line %1: bad vertex index").arg(lineNo));
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
                if (hasNormals) {
                    const quint64 key = (quint64(c.position) << 32) | quint32(c.normal);
                    auto it = shared.constFind(key);
                    if (it == shared.constEnd()) {
                        it = shared.insert(key, quint32(mesh->vertices.size()));
                        mesh->vertices.push_back({ positions[c.position], normals[c.normal].normalized() });
                    }
                    ids.push_back(*it);
                } else {
                    ids.push_back(quint32(mesh->vertices.size()));
                    mesh->vertices.push_back({ positions[c.position], flat });
                }
            }
            for (size_t i = 1; i + 1 < ids.size(); ++i)
                mesh->indices.insert(mesh->indices.end(), { ids[0], ids[i], ids[i + 1] });
        }
        // o, g, s, usemtl, mtllib, vt: ignored
    }

    if (mesh->indices.empty())
        return fail(QStringLiteral("%1 contains no faces").arg(QFileInfo(path).fileName()));
    mesh->computeBounds();
    return mesh;
}
