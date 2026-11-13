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
ComponentDesc materialComponent()
{
    ComponentDesc c;
    c.name = QStringLiteral("Material");
    PropertyDesc roughness { QStringLiteral("roughness"), PropertyType::Float, 0.005, 0.0, 1.0 };
    PropertyDesc metallic { QStringLiteral("metallic"), PropertyType::Float, 0.005, 0.0, 1.0 };
    c.properties = { roughness, metallic };
    c.present = [](const Entity &e) { return e.mesh != nullptr; };
    c.get = [](const Entity &e, int i) -> QVariant {
        switch (i) {
        case 0: return double(e.roughness);
        case 1: return double(e.metallic);
        }
        return {};
    };
    c.set = [](Entity &e, int i, const QVariant &v) {
        const float x = float(qBound(0.0, v.toDouble(), 1.0));
        switch (i) {
        case 0: e.roughness = x; break;
        case 1: e.metallic = x; break;
        }
    };
    return c;
}
ComponentDesc lightComponent()
{
    ComponentDesc c;
    c.name = QStringLiteral("Light");
    PropertyDesc type { QStringLiteral("type"), PropertyType::Enum };
    type.options = { QStringLiteral("directional"), QStringLiteral("point") };
    PropertyDesc intensity { QStringLiteral("intensity"), PropertyType::Float, 0.02, 0.0, 1000.0 };
    c.properties = {
        type,
        { QStringLiteral("color"), PropertyType::Color },
        intensity,
        { QStringLiteral("enabled"), PropertyType::Bool },
    };
    c.present = [](const Entity &e) { return e.light.has_value(); };
    c.get = [](const Entity &e, int i) -> QVariant {
        if (!e.light)
            return {};
        switch (i) {
        case 0: return int(e.light->type);
        case 1: return e.light->color;
        case 2: return double(e.light->intensity);
        case 3: return e.light->enabled;
        }
        return {};
    };
    c.set = [](Entity &e, int i, const QVariant &v) {
        if (!e.light)
            return;
        switch (i) {
        case 0: e.light->type = v.toInt() == Light::Point ? Light::Point : Light::Directional; break;
        case 1: e.light->color = v.value<QColor>(); break;
        case 2: e.light->intensity = float(qMax(0.0, v.toDouble())); break;
        case 3: e.light->enabled = v.toBool(); break;
        }
    };
    c.add = [](Entity &e) {
        if (!e.light)
            e.light = Light {};
    };
    return c;
}
} // namespace

const std::vector<ComponentDesc> &components()
{
    static const std::vector<ComponentDesc> all = { transformComponent(), meshComponent(), materialComponent(), lightComponent() };
    return all;
}
