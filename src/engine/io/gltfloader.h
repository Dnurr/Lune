#pragma once

#include <QByteArray>
#include <QString>
#include <QVector3D>
#include <QVector4D>
#include <vector>

#include "engine/scene/mesh.h"

// A glTF material (metallic-roughness PBR), as the file describes it. Texture fields are
// image references: a file path, or "<gltf file>#image<N>" for an image stored inside the
// glTF (a .glb buffer or a data URI; see loadGltfImageData()). Empty = no texture.
struct GltfMaterial
{
    QString name;
    QVector4D baseColorFactor { 1, 1, 1, 1 }; // linear
    QString baseColorTexture;                // sRGB
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    QString metallicRoughnessTexture;        // linear: G = roughness, B = metallic
    QString normalTexture;                   // linear, tangent space
    float normalScale = 1.0f;
    QString occlusionTexture;                // linear: R = occlusion
    float occlusionStrength = 1.0f;
    QVector3D emissiveFactor;                // linear
    QString emissiveTexture;                 // sRGB
    // Alpha: baseColorFactor.w times the base color texture's alpha.
    enum Alpha { Opaque, Mask, Blend } alphaMode = Opaque;
    float alphaCutoff = 0.5f;                // Mask: alpha below it is cut away
};

struct GltfModel
{
    MeshPtr mesh;                        // nullptr on failure
    std::vector<GltfMaterial> materials; // by glTF material index (Submesh::sourceMaterial)
};

// Loads a glTF 2.0 file (.gltf with its .bin/embedded buffers, or .glb) as a single mesh:
// every triangle primitive of every node, baked with the node's world transform
// (same "one file = one mesh" model as the OBJ loader), with one submesh per material.
// Positions, normals (computed smooth when missing), the first texture coordinates and
// the tangents (computed when missing) are read; skins and animations are ignored.
// Fills `error` on failure.
GltfModel loadGltfModel(const QString &path, QString *error = nullptr);

// Just the mesh of loadGltfModel().
MeshPtr loadGltf(const QString &path, QString *error = nullptr);

// The encoded bytes (PNG, JPEG...) of image `index` of a glTF file, wherever the file keeps
// it: a .glb buffer, a data URI, or a file next to it. Empty and `error` set on failure.
QByteArray loadGltfImageData(const QString &path, int index, QString *error = nullptr);
