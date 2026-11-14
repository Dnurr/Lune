#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "gltfloader.h"

#include <QFile>
#include <QFileInfo>
#include <QMatrix4x4>

#include <cstdlib>
#include <cstring>

namespace
{
// File access through QFile: cgltf's default uses fopen, which on Windows can't open
// paths with non-ASCII characters. cgltf passes UTF-8 paths.
cgltf_result readFile(const cgltf_memory_options *, const cgltf_file_options *, const char *path, cgltf_size *size,
                      void **data)
{
    QFile f(QString::fromUtf8(path));
    if (!f.open(QIODevice::ReadOnly))
        return cgltf_result_file_not_found;
    const QByteArray bytes = f.readAll();
    void *buffer = std::malloc(size_t(bytes.size()) + 1);
    if (!buffer)
        return cgltf_result_out_of_memory;
    std::memcpy(buffer, bytes.constData(), size_t(bytes.size()));
    *size = cgltf_size(bytes.size());
    *data = buffer;
    return cgltf_result_success;
}

void releaseFile(const cgltf_memory_options *, const cgltf_file_options *, void *data)
{
    std::free(data);
}

QString resultText(cgltf_result r)
{
    switch (r) {
    case cgltf_result_file_not_found: return QStringLiteral("file not found");
    case cgltf_result_io_error: return QStringLiteral("read error");
    case cgltf_result_invalid_json: return QStringLiteral("invalid JSON");
    case cgltf_result_invalid_gltf: return QStringLiteral("invalid glTF");
    case cgltf_result_unknown_format: return QStringLiteral("unknown format");
    case cgltf_result_out_of_memory: return QStringLiteral("out of memory");
    case cgltf_result_legacy_gltf: return QStringLiteral("glTF 1.0 is not supported");
    default: return QStringLiteral("error %1").arg(int(r));
    }
}

const cgltf_accessor *findAttribute(const cgltf_primitive &prim, cgltf_attribute_type type)
{
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        if (prim.attributes[i].type == type && prim.attributes[i].index == 0)
            return prim.attributes[i].data;
    }
    return nullptr;
}

// Appends one triangle primitive, transformed by `world`, to `mesh`.
void appendPrimitive(Mesh &mesh, const cgltf_primitive &prim, const QMatrix4x4 &world)
{
    if (prim.type != cgltf_primitive_type_triangles)
        return; // points/lines/strips: not supported yet
    const cgltf_accessor *positions = findAttribute(prim, cgltf_attribute_type_position);
    if (!positions || positions->count == 0)
        return;
    const cgltf_accessor *normals = findAttribute(prim, cgltf_attribute_type_normal);
    if (normals && normals->count != positions->count)
        normals = nullptr;

    const QMatrix3x3 normalMatrix = world.normalMatrix();
    const quint32 base = quint32(mesh.vertices.size());
    for (cgltf_size i = 0; i < positions->count; ++i) {
        float p[3] = {};
        cgltf_accessor_read_float(positions, i, p, 3);
        MeshVertex v;
        v.position = world.map(QVector3D(p[0], p[1], p[2]));
        if (normals) {
            float n[3] = {};
            cgltf_accessor_read_float(normals, i, n, 3);
            const float *m = normalMatrix.constData(); // column-major
            v.normal = QVector3D(m[0] * n[0] + m[3] * n[1] + m[6] * n[2], m[1] * n[0] + m[4] * n[1] + m[7] * n[2],
                                 m[2] * n[0] + m[5] * n[1] + m[8] * n[2])
                           .normalized();
        }
        mesh.vertices.push_back(v);
    }

    // A mirroring transform flips the winding: swap two corners to keep CCW = front.
    const bool flip = world.determinant() < 0.0;
    const cgltf_size count = prim.indices ? prim.indices->count : positions->count;
    const size_t firstIndex = mesh.indices.size();
    for (cgltf_size t = 0; t + 2 < count; t += 3) {
        quint32 tri[3];
        for (int k = 0; k < 3; ++k)
            tri[k] = base + quint32(prim.indices ? cgltf_accessor_read_index(prim.indices, t + k) : t + k);
        if (flip)
            std::swap(tri[1], tri[2]);
        mesh.indices.insert(mesh.indices.end(), { tri[0], tri[1], tri[2] });
    }

    if (!normals) {
        // Smooth normals: area-weighted sum of the face normals around each vertex.
        for (size_t i = firstIndex; i + 2 < mesh.indices.size(); i += 3) {
            MeshVertex &a = mesh.vertices[mesh.indices[i]];
            MeshVertex &b = mesh.vertices[mesh.indices[i + 1]];
            MeshVertex &c = mesh.vertices[mesh.indices[i + 2]];
            const QVector3D n = QVector3D::crossProduct(b.position - a.position, c.position - a.position);
            a.normal += n;
            b.normal += n;
            c.normal += n;
        }
        for (size_t i = base; i < mesh.vertices.size(); ++i)
            mesh.vertices[i].normal.normalize();
    }
}

QMatrix4x4 worldMatrix(const cgltf_node *node)
{
    float m[16];
    cgltf_node_transform_world(node, m);
    return QMatrix4x4(m).transposed(); // cgltf: column-major; QMatrix4x4(const float*): row-major
}
} // namespace

MeshPtr loadGltf(const QString &path, QString *error)
{
    auto fail = [error](const QString &message) -> MeshPtr {
        if (error)
            *error = message;
        return nullptr;
    };

    cgltf_options options = {};
    options.file.read = readFile;
    options.file.release = releaseFile;

    const QByteArray utf8 = QFileInfo(path).absoluteFilePath().toUtf8();
    cgltf_data *data = nullptr;
    cgltf_result r = cgltf_parse_file(&options, utf8.constData(), &data);
    if (r != cgltf_result_success)
        return fail(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), resultText(r)));
    r = cgltf_load_buffers(&options, data, utf8.constData());
    if (r != cgltf_result_success) {
        cgltf_free(data);
        return fail(QStringLiteral("%1: cannot load buffers (%2)").arg(QFileInfo(path).fileName(), resultText(r)));
    }

    auto mesh = std::make_shared<Mesh>();
    mesh->name = QFileInfo(path).completeBaseName();
    mesh->source = QFileInfo(path).absoluteFilePath();

    // Meshes placed by nodes (with their transforms); if no node uses a mesh, take the
    // meshes as they are.
    bool anyNode = false;
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        const cgltf_node &node = data->nodes[i];
        if (!node.mesh)
            continue;
        anyNode = true;
        const QMatrix4x4 world = worldMatrix(&node);
        for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
            appendPrimitive(*mesh, node.mesh->primitives[p], world);
    }
    if (!anyNode) {
        for (cgltf_size m = 0; m < data->meshes_count; ++m) {
            for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p)
                appendPrimitive(*mesh, data->meshes[m].primitives[p], QMatrix4x4());
        }
    }
    cgltf_free(data);

    if (mesh->indices.empty())
        return fail(QStringLiteral("%1 contains no triangles").arg(QFileInfo(path).fileName()));
    mesh->computeBounds();
    mesh->computeClosed();
    return mesh;
}
