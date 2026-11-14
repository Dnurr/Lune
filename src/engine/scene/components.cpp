#include "components.h"

#include <QVector3D>

#include "engine/io/hdrimage.h"

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
    PropertyDesc size { QStringLiteral("size"), PropertyType::Float, 0.005, 0.0, 100.0 };
    PropertyDesc softness { QStringLiteral("softness"), PropertyType::Float, 0.01, 0.0, 8.0 };
    c.properties = {
        type,
        { QStringLiteral("color"), PropertyType::Color },
        intensity,
        { QStringLiteral("shadows"), PropertyType::Bool },
        size,
        softness,
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
        case 3: return e.light->shadows;
        case 4: return double(e.light->size);
        case 5: return double(e.light->softness);
        case 6: return e.light->enabled;
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
        case 3: e.light->shadows = v.toBool(); break;
        case 4: e.light->size = float(qBound(0.0, v.toDouble(), 100.0)); break;
        case 5: e.light->softness = float(qBound(0.0, v.toDouble(), 8.0)); break;
        case 6: e.light->enabled = v.toBool(); break;
        }
    };
    c.add = [](Entity &e) {
        if (!e.light)
            e.light = Light {};
    };
    return c;
}
ComponentDesc environmentComponent()
{
    ComponentDesc c;
    c.name = QStringLiteral("Environment");
    PropertyDesc image { QStringLiteral("image"), PropertyType::File };
    image.fileFilter = HdrIO::FileFilter;
    PropertyDesc intensity { QStringLiteral("intensity"), PropertyType::Float, 0.01, 0.0, 100.0 };
    PropertyDesc blur { QStringLiteral("blur"), PropertyType::Float, 0.005, 0.0, 1.0 };
    c.properties = {
        image,
        intensity,
        { QStringLiteral("sky"), PropertyType::Color },
        { QStringLiteral("ground"), PropertyType::Color },
        { QStringLiteral("background"), PropertyType::Bool },
        blur,
        { QStringLiteral("enabled"), PropertyType::Bool },
    };
    c.present = [](const Entity &e) { return e.environment.has_value(); };
    c.get = [](const Entity &e, int i) -> QVariant {
        if (!e.environment)
            return {};
        const Environment &env = *e.environment;
        switch (i) {
        case 0: return env.image;
        case 1: return double(env.intensity);
        case 2: return env.skyColor;
        case 3: return env.groundColor;
        case 4: return env.background;
        case 5: return double(env.blur);
        case 6: return env.enabled;
        }
        return {};
    };
    c.set = [](Entity &e, int i, const QVariant &v) {
        if (!e.environment)
            return;
        Environment &env = *e.environment;
        switch (i) {
        case 0: env.image = v.toString(); break;
        case 1: env.intensity = float(qMax(0.0, v.toDouble())); break;
        case 2: env.skyColor = v.value<QColor>(); break;
        case 3: env.groundColor = v.value<QColor>(); break;
        case 4: env.background = v.toBool(); break;
        case 5: env.blur = float(qBound(0.0, v.toDouble(), 1.0)); break;
        case 6: env.enabled = v.toBool(); break;
        }
    };
    c.add = [](Entity &e) {
        if (!e.environment)
            e.environment = Environment {};
    };
    return c;
}
} // namespace

const std::vector<ComponentDesc> &components()
{
    static const std::vector<ComponentDesc> all = { transformComponent(), meshComponent(), materialComponent(), lightComponent(),
                                                    environmentComponent() };
    return all;
}
