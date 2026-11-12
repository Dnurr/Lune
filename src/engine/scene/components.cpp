#include "components.h"

#include <QVector3D>

namespace
{
ComponentDesc transformComponent()
{
    ComponentDesc c;
    c.name = QStringLiteral("Transform");
    c.properties = {
        { QStringLiteral("position"), PropertyType::Vector3, 0.02 }, // world units per pixel
        { QStringLiteral("rotation"), PropertyType::Vector3, 0.5 },  // degrees per pixel
        { QStringLiteral("scale"), PropertyType::Vector3, 0.01 },
    };
    c.present = [](const Entity &) { return true; };
    c.get = [](const Entity &e, int i) -> QVariant {
        switch (i) {
        case 0: return QVariant::fromValue(e.transform.position);
        case 1: return QVariant::fromValue(e.transform.rotation);
        case 2: return QVariant::fromValue(e.transform.scale);
        }
        return {};
    };
    c.set = [](Entity &e, int i, const QVariant &v) {
        const QVector3D x = v.value<QVector3D>();
        switch (i) {
        case 0: e.transform.position = x; break;
        case 1: e.transform.rotation = x; break;
        case 2: e.transform.scale = x; break;
        }
    };
    return c;
}

ComponentDesc meshComponent()
{
    ComponentDesc c;
    c.name = QStringLiteral("Mesh");
    PropertyDesc mesh { QStringLiteral("mesh"), PropertyType::Text };
    mesh.readOnly = true;
    PropertyDesc triangles { QStringLiteral("triangles"), PropertyType::Text };
    triangles.readOnly = true;
    c.properties = {
        mesh,
        triangles,
        { QStringLiteral("color"), PropertyType::Color },
        { QStringLiteral("visible"), PropertyType::Bool },
    };
    c.present = [](const Entity &e) { return e.mesh != nullptr; };
    c.get = [](const Entity &e, int i) -> QVariant {
        switch (i) {
        case 0: return e.mesh ? e.mesh->name : QString();
        case 1: return e.mesh ? QString::number(e.mesh->indices.size() / 3) : QString();
        case 2: return e.displayColor;
        case 3: return e.visible;
        }
        return {};
    };
    c.set = [](Entity &e, int i, const QVariant &v) {
        switch (i) {
        case 2: e.displayColor = v.value<QColor>(); break;
        case 3: e.visible = v.toBool(); break;
        }
    };
    return c;
}
} // namespace

const std::vector<ComponentDesc> &components()
{
    static const std::vector<ComponentDesc> all = { transformComponent(), meshComponent() };
    return all;
}
