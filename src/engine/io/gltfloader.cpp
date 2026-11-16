#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include "gltfloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMatrix4x4>
#include <QUrl>

#include <map>

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

// Triangles of each material, by glTF material index (-1 = none): one submesh each.
using MaterialGroups = std::map<int, std::vector<quint32>>;

// Appends one triangle primitive, transformed by `world`, to `mesh` (vertices) and
// `groups` (its triangles, under its material). Clears `allTangents` if it has none.
void appendPrimitive(Mesh &mesh, MaterialGroups &groups, const cgltf_data *data, const cgltf_primitive &prim,
                     const QMatrix4x4 &world, bool &allTangents)
{
    if (prim.type != cgltf_primitive_type_triangles)
        return; // points/lines/strips: not supported yet
    const cgltf_accessor *positions = findAttribute(prim, cgltf_attribute_type_position);
    if (!positions || positions->count == 0)
        return;
    const cgltf_accessor *normals = findAttribute(prim, cgltf_attribute_type_normal);
    if (normals && normals->count != positions->count)
        normals = nullptr;
    const cgltf_accessor *uvs = findAttribute(prim, cgltf_attribute_type_texcoord);
    if (uvs && uvs->count != positions->count)
        uvs = nullptr;
    const cgltf_accessor *tangents = normals ? findAttribute(prim, cgltf_attribute_type_tangent) : nullptr;
    if (tangents && tangents->count != positions->count)
        tangents = nullptr;
    if (!tangents)
        allTangents = false;
    const bool flip = world.determinant() < 0.0;

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
        if (uvs) {
            float t[2] = {};
            cgltf_accessor_read_float(uvs, i, t, 2);
            v.uv = QVector2D(t[0], t[1]);
        }
        if (tangents) {
            float t[4] = {};
            cgltf_accessor_read_float(tangents, i, t, 4);
            // A direction: transformed by the model matrix. A mirroring transform flips the
            // handedness (the winding is flipped below too).
            const QVector3D dir = world.mapVector(QVector3D(t[0], t[1], t[2])).normalized();
            v.tangent = QVector4D(dir, (t[3] < 0.0f ? -1.0f : 1.0f) * (flip ? -1.0f : 1.0f));
        }
        mesh.vertices.push_back(v);
    }

    // A mirroring transform flips the winding: swap two corners to keep CCW = front.
    const int material = prim.material ? int(cgltf_material_index(data, prim.material)) : -1;
    std::vector<quint32> &tris = groups[material];
    const cgltf_size count = prim.indices ? prim.indices->count : positions->count;
    const size_t firstIndex = tris.size();
    for (cgltf_size t = 0; t + 2 < count; t += 3) {
        quint32 tri[3];
        for (int k = 0; k < 3; ++k)
            tri[k] = base + quint32(prim.indices ? cgltf_accessor_read_index(prim.indices, t + k) : t + k);
        if (flip)
            std::swap(tri[1], tri[2]);
        tris.insert(tris.end(), { tri[0], tri[1], tri[2] });
    }

    if (!normals) {
        // Smooth normals: area-weighted sum of the face normals around each vertex.
        for (size_t i = firstIndex; i + 2 < tris.size(); i += 3) {
            MeshVertex &a = mesh.vertices[tris[i]];
            MeshVertex &b = mesh.vertices[tris[i + 1]];
            MeshVertex &c = mesh.vertices[tris[i + 2]];
            const QVector3D n = QVector3D::crossProduct(b.position - a.position, c.position - a.position);
            a.normal += n;
            b.normal += n;
            c.normal += n;
        }
        for (size_t i = base; i < mesh.vertices.size(); ++i)
            mesh.vertices[i].normal.normalize();
    }
}

// Image reference of a texture (see GltfMaterial): its file, or "<gltf>#image<N>".
QString imageReference(const cgltf_data *data, const cgltf_texture_view &view, const QString &gltfPath)
{
    if (!view.texture)
        return QString();
    const cgltf_image *image = view.texture->image;
    if (!image) // e.g. only an extension's image (KTX2, WebP): not supported
        return QString();
    if (image->uri && !QByteArray(image->uri).startsWith("data:")) {
        const QString file = QUrl::fromPercentEncoding(QByteArray(image->uri));
        return QDir::cleanPath(QFileInfo(gltfPath).absoluteDir().absoluteFilePath(file));
    }
    return QStringLiteral("%1#image%2").arg(QFileInfo(gltfPath).absoluteFilePath()).arg(cgltf_image_index(data, image));
}

GltfMaterial readMaterial(const cgltf_data *data, const cgltf_material &m, const QString &path)
{
    GltfMaterial g;
    g.name = m.name ? QString::fromUtf8(m.name) : QString();
    if (m.has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness &pbr = m.pbr_metallic_roughness;
        g.baseColorFactor = QVector4D(pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2],
                                      pbr.base_color_factor[3]);
        g.baseColorTexture = imageReference(data, pbr.base_color_texture, path);
        g.metallicFactor = pbr.metallic_factor;
        g.roughnessFactor = pbr.roughness_factor;
        g.metallicRoughnessTexture = imageReference(data, pbr.metallic_roughness_texture, path);
    }
    g.normalTexture = imageReference(data, m.normal_texture, path);
    g.normalScale = m.normal_texture.texture ? m.normal_texture.scale : 1.0f;
    g.occlusionTexture = imageReference(data, m.occlusion_texture, path);
    g.occlusionStrength = m.occlusion_texture.texture ? m.occlusion_texture.scale : 1.0f;
    g.emissiveFactor = QVector3D(m.emissive_factor[0], m.emissive_factor[1], m.emissive_factor[2]);
    if (m.has_emissive_strength)
        g.emissiveFactor *= m.emissive_strength.emissive_strength;
    g.emissiveTexture = imageReference(data, m.emissive_texture, path);
    g.alphaMode = m.alpha_mode == cgltf_alpha_mode_mask    ? GltfMaterial::Mask
                  : m.alpha_mode == cgltf_alpha_mode_blend ? GltfMaterial::Blend
                                                           : GltfMaterial::Opaque;
    g.alphaCutoff = m.alpha_cutoff;
    return g;
}

// Parses `path` and loads its buffers. nullptr and `error` set on failure.
cgltf_data *openGltf(const QString &path, QString *error)
{
    cgltf_options options = {};
    options.file.read = readFile;
    options.file.release = releaseFile;
    const QByteArray utf8 = QFileInfo(path).absoluteFilePath().toUtf8();
    cgltf_data *data = nullptr;
    cgltf_result r = cgltf_parse_file(&options, utf8.constData(), &data);
    if (r != cgltf_result_success) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), resultText(r));
        return nullptr;
    }
    r = cgltf_load_buffers(&options, data, utf8.constData());
    if (r != cgltf_result_success) {
        cgltf_free(data);
        if (error)
            *error = QStringLiteral("%1: cannot load buffers (%2)").arg(QFileInfo(path).fileName(), resultText(r));
        return nullptr;
    }
    return data;
}

QMatrix4x4 worldMatrix(const cgltf_node *node)
{
    float m[16];
    cgltf_node_transform_world(node, m);
    return QMatrix4x4(m).transposed(); // cgltf: column-major; QMatrix4x4(const float*): row-major
}
} // namespace

GltfModel loadGltfModel(const QString &path, QString *error)
{
    GltfModel model;
    cgltf_data *data = openGltf(path, error);
    if (!data)
        return model;

    auto mesh = std::make_shared<Mesh>();
    mesh->name = QFileInfo(path).completeBaseName();
    mesh->source = QFileInfo(path).absoluteFilePath();

    // Meshes placed by nodes (with their transforms); if no node uses a mesh, take the
    // meshes as they are.
    MaterialGroups groups;
    bool allTangents = true;
    bool anyNode = false;
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        const cgltf_node &node = data->nodes[i];
        if (!node.mesh)
            continue;
        anyNode = true;
        const QMatrix4x4 world = worldMatrix(&node);
        for (cgltf_size p = 0; p < node.mesh->primitives_count; ++p)
            appendPrimitive(*mesh, groups, data, node.mesh->primitives[p], world, allTangents);
    }
    if (!anyNode) {
        for (cgltf_size m = 0; m < data->meshes_count; ++m) {
            for (cgltf_size p = 0; p < data->meshes[m].primitives_count; ++p)
                appendPrimitive(*mesh, groups, data, data->meshes[m].primitives[p], QMatrix4x4(), allTangents);
        }
    }

    for (cgltf_size i = 0; i < data->materials_count; ++i)
        model.materials.push_back(readMaterial(data, data->materials[i], path));
    cgltf_free(data);

    // One submesh per material, in material order (triangles without one last).
    auto addGroup = [&](int material, const std::vector<quint32> &tris) {
        if (tris.empty())
            return;
        QString name = material >= 0 ? model.materials[size_t(material)].name : QString();
        if (name.isEmpty() && material >= 0)
            name = QStringLiteral("Material %1").arg(material);
        mesh->submeshes.push_back({ quint32(mesh->indices.size()), quint32(tris.size()), name, material });
        mesh->indices.insert(mesh->indices.end(), tris.begin(), tris.end());
    };
    for (const auto &[material, tris] : groups)
        if (material >= 0)
            addGroup(material, tris);
    if (groups.count(-1))
        addGroup(-1, groups[-1]);

    if (mesh->indices.empty()) {
        if (error)
            *error = QStringLiteral("%1 contains no triangles").arg(QFileInfo(path).fileName());
        model.materials.clear();
        return model;
    }
    mesh->finish(allTangents);
    model.mesh = mesh;
    return model;
}

MeshPtr loadGltf(const QString &path, QString *error)
{
    return loadGltfModel(path, error).mesh;
}

QByteArray loadGltfImageData(const QString &path, int index, QString *error)
{
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        return QByteArray();
    };
    cgltf_data *data = openGltf(path, error);
    if (!data)
        return QByteArray();
    if (index < 0 || cgltf_size(index) >= data->images_count) {
        cgltf_free(data);
        return fail(QStringLiteral("%1 has no image %2").arg(QFileInfo(path).fileName()).arg(index));
    }
    const cgltf_image &image = data->images[index];
    QByteArray bytes;
    if (image.buffer_view && image.buffer_view->buffer->data) {
        const cgltf_buffer_view &view = *image.buffer_view;
        bytes = QByteArray(static_cast<const char *>(view.buffer->data) + view.offset, qsizetype(view.size));
    } else if (image.uri && QByteArray(image.uri).startsWith("data:")) {
        const QByteArray uri(image.uri);
        const qsizetype comma = uri.indexOf(',');
        if (comma > 0 && uri.left(comma).endsWith(";base64"))
            bytes = QByteArray::fromBase64(uri.mid(comma + 1));
    } else if (image.uri) {
        QFile f(QFileInfo(path).absoluteDir().absoluteFilePath(QUrl::fromPercentEncoding(QByteArray(image.uri))));
        if (f.open(QIODevice::ReadOnly))
            bytes = f.readAll();
    }
    cgltf_free(data);
    if (bytes.isEmpty())
        return fail(QStringLiteral("%1: image %2 can't be read").arg(QFileInfo(path).fileName()).arg(index));
    return bytes;
}
