#include "meshimport.h"

#include <QFileInfo>

#include "engine/material/gltfmaterialgraph.h"
#include "engine/material/nodelibrary.h"
#include "gltfloader.h"
#include "objloader.h"

MeshPtr importMesh(const QString &path, QString *error)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QLatin1String("obj"))
        return loadObj(path, error);
    if (ext == QLatin1String("gltf") || ext == QLatin1String("glb"))
        return loadGltf(path, error);
    if (error)
        *error = QStringLiteral("Unsupported mesh format: .%1").arg(ext);
    return nullptr;
}

ImportedModel importModel(const QString &path, QString *error)
{
    ImportedModel model;
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext != QLatin1String("gltf") && ext != QLatin1String("glb")) {
        model.mesh = importMesh(path, error);
        if (model.mesh)
            model.materials.resize(model.mesh->submeshes.size());
        return model;
    }
    const GltfModel gltf = loadGltfModel(path, error);
    model.mesh = gltf.mesh;
    if (!model.mesh)
        return model;
    for (const Submesh &sm : model.mesh->submeshes) {
        Material m;
        if (sm.sourceMaterial >= 0 && size_t(sm.sourceMaterial) < gltf.materials.size()) {
            const GltfMaterial &g = gltf.materials[size_t(sm.sourceMaterial)];
            m.name = g.name.isEmpty() ? sm.name : g.name;
            m.graph = graphFromGltf(g, NodeLibrary::instance());
        }
        model.materials.push_back(m);
    }
    return model;
}

QString meshFileFilter()
{
    return QStringLiteral("Meshes (*.obj *.gltf *.glb);;Wavefront OBJ (*.obj);;glTF 2.0 (*.gltf *.glb)");
}
