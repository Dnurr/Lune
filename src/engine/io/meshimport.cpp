#include "meshimport.h"

#include <QFileInfo>

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

QString meshFileFilter()
{
    return QStringLiteral("Meshes (*.obj *.gltf *.glb);;Wavefront OBJ (*.obj);;glTF 2.0 (*.gltf *.glb)");
}
