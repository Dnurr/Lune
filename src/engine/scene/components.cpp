#include "components.h"

#include <QVector3D>
#include <cmath>

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
    c.remove = [](Entity &e) { e.light.reset(); };
    c.canAdd = [](const Entity &e) { return !e.light && !e.camera && !e.emitter && !e.force && !e.fluid && !e.fluidSource; };
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
    c.remove = [](Entity &e) { e.environment.reset(); };
    c.canAdd = [](const Entity &e) { return !e.environment && !e.camera && !e.emitter && !e.force && !e.fluid && !e.fluidSource; };
    return c;
}
ComponentDesc cameraComponent()
{
    ComponentDesc c;
    c.name = QStringLiteral("Camera");
    PropertyDesc fov { QStringLiteral("field of view"), PropertyType::Float, 0.1, 1.0, 170.0 };
    PropertyDesc nearPlane { QStringLiteral("near"), PropertyType::Float, 0.001, 0.001, 1e5 };
    PropertyDesc farPlane { QStringLiteral("far"), PropertyType::Float, 1.0, 0.01, 1e6 };
    c.properties = { fov, nearPlane, farPlane, { QStringLiteral("enabled"), PropertyType::Bool } };
    c.present = [](const Entity &e) { return e.camera.has_value(); };
    c.get = [](const Entity &e, int i) -> QVariant {
        if (!e.camera)
            return {};
        switch (i) {
        case 0: return double(e.camera->fieldOfView);
        case 1: return double(e.camera->nearPlane);
        case 2: return double(e.camera->farPlane);
        case 3: return e.camera->enabled;
        }
        return {};
    };
    c.set = [](Entity &e, int i, const QVariant &v) {
        if (!e.camera)
            return;
        Camera &cam = *e.camera;
        switch (i) {
        case 0: cam.fieldOfView = float(qBound(1.0, v.toDouble(), 170.0)); break;
        case 1: cam.nearPlane = float(qBound(0.001, v.toDouble(), 1e5)); break;
        case 2: cam.farPlane = float(qBound(0.01, v.toDouble(), 1e6)); break;
        case 3: cam.enabled = v.toBool(); break;
        }
    };
    c.add = [](Entity &e) {
        if (!e.camera)
            e.camera = Camera {};
    };
    c.remove = [](Entity &e) {
        e.camera.reset();
        e.postProcess.reset(); // only means something on a camera
    };
    c.canAdd = [](const Entity &e) { return !e.mesh && !e.light && !e.environment && !e.emitter && !e.force && !e.fluid && !e.fluidSource; };
    return c;
}

ComponentDesc postProcessComponent()
{
    // One row per field: a switch or a number, under the effect's heading.
    struct Field
    {
        PropertyDesc desc;
        bool PostProcess::*flag = nullptr;
        float PostProcess::*value = nullptr;
    };
    auto flag = [](const char *name, const char *group, bool PostProcess::*member) {
        Field f;
        f.desc = { QString::fromLatin1(name), PropertyType::Bool };
        f.desc.group = QString::fromLatin1(group);
        f.flag = member;
        return f;
    };
    auto number = [](const char *name, const char *group, float PostProcess::*member, double step, double lo, double hi) {
        Field f;
        f.desc = { QString::fromLatin1(name), PropertyType::Float, step, lo, hi };
        f.desc.group = QString::fromLatin1(group);
        f.value = member;
        return f;
    };
    using P = PostProcess;
    static const std::vector<Field> fields = {
        flag("enabled", "", &P::enabled),
        flag("enabled", "depth of field", &P::depthOfField),
        number("focus distance", "depth of field", &P::focusDistance, 0.02, 0.01, 1e5),
        number("f-stop", "depth of field", &P::fStop, 0.02, 0.5, 64.0),
        number("max blur", "depth of field", &P::maxBlur, 0.05, 0.0, 64.0),
        flag("enabled", "bloom", &P::bloom),
        number("intensity", "bloom", &P::bloomIntensity, 0.002, 0.0, 10.0),
        number("threshold", "bloom", &P::bloomThreshold, 0.01, 0.0, 100.0),
        number("radius", "bloom", &P::bloomRadius, 0.005, 0.0, 1.0),
        flag("enabled", "chromatic aberration", &P::chromaticAberration),
        number("intensity", "chromatic aberration", &P::chromaticIntensity, 0.005, 0.0, 10.0),
        flag("enabled", "vignette", &P::vignette),
        number("intensity", "vignette", &P::vignetteIntensity, 0.005, 0.0, 1.0),
        number("smoothness", "vignette", &P::vignetteSmoothness, 0.005, 0.0, 1.0),
        flag("enabled", "film grain", &P::filmGrain),
        number("intensity", "film grain", &P::grainIntensity, 0.002, 0.0, 1.0),
        number("size", "film grain", &P::grainSize, 0.01, 0.5, 8.0),
        number("response", "film grain", &P::grainResponse, 0.005, 0.05, 2.0),
        flag("enabled", "fxaa", &P::fxaa),
    };

    ComponentDesc c;
    c.name = QStringLiteral("Post Process");
    for (const Field &f : fields)
        c.properties.push_back(f.desc);
    c.present = [](const Entity &e) { return e.postProcess.has_value(); };
    c.get = [](const Entity &e, int i) -> QVariant {
        if (!e.postProcess || i < 0 || i >= int(fields.size()))
            return {};
        const Field &f = fields[size_t(i)];
        return f.flag ? QVariant((*e.postProcess).*f.flag) : QVariant(double((*e.postProcess).*f.value));
    };
    c.set = [](Entity &e, int i, const QVariant &v) {
        if (!e.postProcess || i < 0 || i >= int(fields.size()))
            return;
        const Field &f = fields[size_t(i)];
        if (f.flag)
            (*e.postProcess).*f.flag = v.toBool();
        else
            (*e.postProcess).*f.value = float(qBound(f.desc.minimum, v.toDouble(), f.desc.maximum));
    };
    c.add = [](Entity &e) {
        if (!e.postProcess)
            e.postProcess = PostProcess {};
    };
    c.remove = [](Entity &e) { e.postProcess.reset(); };
    c.canAdd = [](const Entity &e) { return e.camera.has_value() && !e.postProcess; };
    return c;
}
ComponentDesc emitterComponent()
{
    auto prop = [](const char *name, const char *group, PropertyType type, double step = 0.01, double lo = -1e6,
                   double hi = 1e6) {
        PropertyDesc d { QString::fromLatin1(name), type, step, lo, hi };
        d.group = QString::fromLatin1(group);
        return d;
    };
    PropertyDesc shape = prop("shape", "shape", PropertyType::Enum);
    shape.options = { QStringLiteral("point"), QStringLiteral("sphere"), QStringLiteral("box"), QStringLiteral("disc") };
    PropertyDesc blend = prop("blend", "look", PropertyType::Enum);
    blend.options = { QStringLiteral("additive"), QStringLiteral("alpha") };

    ComponentDesc c;
    c.name = QStringLiteral("Particle Emitter");
    c.properties = {
        prop("enabled", "", PropertyType::Bool),
        prop("rate", "emission", PropertyType::Float, 1.0, 0.0, 1e6),
        prop("lifetime", "emission", PropertyType::Float, 0.01, 0.01, 600.0),
        prop("lifetime variation", "emission", PropertyType::Float, 0.005, 0.0, 1.0),
        prop("max particles", "emission", PropertyType::Float, 50.0, 1.0, 4000000.0),
        prop("seed", "emission", PropertyType::Float, 0.2, 0.0, 1000000.0),
        shape,
        prop("size", "shape", PropertyType::Vector3, 0.01),
        prop("speed", "velocity", PropertyType::Float, 0.02, 0.0, 1e4),
        prop("speed variation", "velocity", PropertyType::Float, 0.005, 0.0, 1.0),
        prop("spread", "velocity", PropertyType::Float, 0.2, 0.0, 180.0),
        prop("start color", "look", PropertyType::Color),
        prop("end color", "look", PropertyType::Color),
        prop("start opacity", "look", PropertyType::Float, 0.005, 0.0, 1.0),
        prop("end opacity", "look", PropertyType::Float, 0.005, 0.0, 1.0),
        prop("intensity", "look", PropertyType::Float, 0.02, 0.0, 1000.0),
        prop("start size", "look", PropertyType::Float, 0.002, 0.0, 1e3),
        prop("end size", "look", PropertyType::Float, 0.002, 0.0, 1e3),
        blend,
        prop("softness", "look", PropertyType::Float, 0.005, 0.0, 100.0),
    };
    c.present = [](const Entity &e) { return e.emitter.has_value(); };
    c.get = [](const Entity &e, int i) -> QVariant {
        if (!e.emitter)
            return {};
        const ParticleEmitter &p = *e.emitter;
        switch (i) {
        case 0: return p.enabled;
        case 1: return double(p.rate);
        case 2: return double(p.lifetime);
        case 3: return double(p.lifetimeVariation);
        case 4: return double(p.maxParticles);
        case 5: return double(p.seed);
        case 6: return int(p.shape);
        case 7: return QVariant::fromValue(p.shapeSize);
        case 8: return double(p.speed);
        case 9: return double(p.speedVariation);
        case 10: return double(p.spread);
        case 11: return p.startColor;
        case 12: return p.endColor;
        case 13: return double(p.startOpacity);
        case 14: return double(p.endOpacity);
        case 15: return double(p.intensity);
        case 16: return double(p.startSize);
        case 17: return double(p.endSize);
        case 18: return int(p.blend);
        case 19: return double(p.softness);
        }
        return {};
    };
    c.set = [](Entity &e, int i, const QVariant &v) {
        if (!e.emitter)
            return;
        ParticleEmitter &p = *e.emitter;
        const double x = v.toDouble();
        auto f = [x](double lo, double hi) { return float(qBound(lo, x, hi)); };
        switch (i) {
        case 0: p.enabled = v.toBool(); break;
        case 1: p.rate = f(0.0, 1e6); break;
        case 2: p.lifetime = f(0.01, 600.0); break;
        case 3: p.lifetimeVariation = f(0.0, 1.0); break;
        case 4: p.maxParticles = int(std::lround(qBound(1.0, x, 4000000.0))); break;
        case 5: p.seed = int(std::lround(qBound(0.0, x, 1000000.0))); break;
        case 6: p.shape = ParticleEmitter::Shape(qBound(0, v.toInt(), 3)); break;
        case 7: p.shapeSize = v.value<QVector3D>(); break;
        case 8: p.speed = f(0.0, 1e4); break;
        case 9: p.speedVariation = f(0.0, 1.0); break;
        case 10: p.spread = f(0.0, 180.0); break;
        case 11: p.startColor = v.value<QColor>(); break;
        case 12: p.endColor = v.value<QColor>(); break;
        case 13: p.startOpacity = f(0.0, 1.0); break;
        case 14: p.endOpacity = f(0.0, 1.0); break;
        case 15: p.intensity = f(0.0, 1000.0); break;
        case 16: p.startSize = f(0.0, 1e3); break;
        case 17: p.endSize = f(0.0, 1e3); break;
        case 18: p.blend = ParticleEmitter::Blend(qBound(0, v.toInt(), 1)); break;
        case 19: p.softness = f(0.0, 100.0); break;
        }
    };
    c.add = [](Entity &e) {
        if (!e.emitter)
            e.emitter = ParticleEmitter {};
    };
    c.remove = [](Entity &e) { e.emitter.reset(); };
    c.canAdd = [](const Entity &e) { return !e.mesh && !e.light && !e.environment && !e.camera && !e.fluid; };
    return c;
}
ComponentDesc forceComponent()
{
    auto prop = [](const char *name, const char *group, PropertyType type, double step = 0.01, double lo = -1e6,
                   double hi = 1e6) {
        PropertyDesc d { QString::fromLatin1(name), type, step, lo, hi };
        d.group = QString::fromLatin1(group);
        return d;
    };
    PropertyDesc type = prop("type", "", PropertyType::Enum);
    type.options = { QStringLiteral("gravity"), QStringLiteral("wind"), QStringLiteral("turbulence"),
                     QStringLiteral("attractor"), QStringLiteral("vortex") };

    ComponentDesc c;
    c.name = QStringLiteral("Force");
    c.properties = {
        prop("enabled", "", PropertyType::Bool),
        type,
        prop("strength", "", PropertyType::Float, 0.02, -1e5, 1e5),
        prop("radius", "reach", PropertyType::Float, 0.02, 0.0, 1e5),
        prop("falloff", "reach", PropertyType::Float, 0.005, 0.0, 1.0),
        prop("drag", "wind", PropertyType::Float, 0.01, 0.0, 100.0),
        prop("frequency", "noise", PropertyType::Float, 0.01, 0.0, 100.0),
        prop("speed", "noise", PropertyType::Float, 0.01, 0.0, 100.0),
        prop("amount", "vortex", PropertyType::Float, 0.02, 0.0, 1e5),
        prop("pull", "vortex", PropertyType::Float, 0.02, -1e5, 1e5),
        prop("lift", "vortex", PropertyType::Float, 0.02, -1e5, 1e5),
    };
    c.present = [](const Entity &e) { return e.force.has_value(); };
    c.get = [](const Entity &e, int i) -> QVariant {
        if (!e.force)
            return {};
        const ParticleForce &f = *e.force;
        switch (i) {
        case 0: return f.enabled;
        case 1: return int(f.type);
        case 2: return double(f.strength);
        case 3: return double(f.radius);
        case 4: return double(f.falloff);
        case 5: return double(f.drag);
        case 6: return double(f.frequency);
        case 7: return double(f.speed);
        case 8: return double(f.noise);
        case 9: return double(f.pull);
        case 10: return double(f.lift);
        }
        return {};
    };
    c.set = [](Entity &e, int i, const QVariant &v) {
        if (!e.force)
            return;
        ParticleForce &f = *e.force;
        const double x = v.toDouble();
        auto clamp = [x](double lo, double hi) { return float(qBound(lo, x, hi)); };
        switch (i) {
        case 0: f.enabled = v.toBool(); break;
        case 1: f.type = ParticleForce::Type(qBound(0, v.toInt(), 4)); break;
        case 2: f.strength = clamp(-1e5, 1e5); break;
        case 3: f.radius = clamp(0.0, 1e5); break;
        case 4: f.falloff = clamp(0.0, 1.0); break;
        case 5: f.drag = clamp(0.0, 100.0); break;
        case 6: f.frequency = clamp(0.0, 100.0); break;
        case 7: f.speed = clamp(0.0, 100.0); break;
        case 8: f.noise = clamp(0.0, 1e5); break;
        case 9: f.pull = clamp(-1e5, 1e5); break;
        case 10: f.lift = clamp(-1e5, 1e5); break;
        }
    };
    c.add = [](Entity &e) {
        if (!e.force)
            e.force = ParticleForce {};
    };
    c.remove = [](Entity &e) { e.force.reset(); };
    c.canAdd = [](const Entity &e) { return !e.light && !e.environment && !e.camera; };
    return c;
}
ComponentDesc fluidVolumeComponent()
{
    auto prop = [](const char *name, const char *group, PropertyType type, double step = 0.01, double lo = -1e6,
                   double hi = 1e6) {
        PropertyDesc d { QString::fromLatin1(name), type, step, lo, hi };
        d.group = QString::fromLatin1(group);
        return d;
    };
    ComponentDesc c;
    c.name = QStringLiteral("Fluid Volume");
    c.properties = {
        prop("enabled", "", PropertyType::Bool),
        prop("size", "grid", PropertyType::Vector3, 0.02),
        prop("resolution", "grid", PropertyType::Float, 0.5, 8.0, 256.0),
        prop("dissipation", "solver", PropertyType::Float, 0.002, 0.0, 10.0),
        prop("cooling", "solver", PropertyType::Float, 0.005, 0.0, 10.0),
        prop("buoyancy", "solver", PropertyType::Float, 0.02, -1e3, 1e3),
        prop("weight", "solver", PropertyType::Float, 0.01, -1e3, 1e3),
        prop("vorticity", "solver", PropertyType::Float, 0.005, 0.0, 100.0),
        prop("pressure iterations", "solver", PropertyType::Float, 0.2, 1.0, 200.0),
        prop("color", "look", PropertyType::Color),
        prop("density", "look", PropertyType::Float, 0.02, 0.0, 1e4),
        prop("emission color", "look", PropertyType::Color),
        prop("emission", "look", PropertyType::Float, 0.02, 0.0, 1e4),
        prop("samples", "look", PropertyType::Float, 0.5, 8.0, 512.0),
    };
    c.present = [](const Entity &e) { return e.fluid.has_value(); };
    c.get = [](const Entity &e, int i) -> QVariant {
        if (!e.fluid)
            return {};
        const FluidVolume &f = *e.fluid;
        switch (i) {
        case 0: return f.enabled;
        case 1: return QVariant::fromValue(f.size);
        case 2: return double(f.resolution);
        case 3: return double(f.dissipation);
        case 4: return double(f.cooling);
        case 5: return double(f.buoyancy);
        case 6: return double(f.weight);
        case 7: return double(f.vorticity);
        case 8: return double(f.pressureIterations);
        case 9: return f.color;
        case 10: return double(f.density);
        case 11: return f.emissionColor;
        case 12: return double(f.emission);
        case 13: return double(f.samples);
        }
        return {};
    };
    c.set = [](Entity &e, int i, const QVariant &v) {
        if (!e.fluid)
            return;
        FluidVolume &f = *e.fluid;
        const double x = v.toDouble();
        auto clamp = [x](double lo, double hi) { return float(qBound(lo, x, hi)); };
        switch (i) {
        case 0: f.enabled = v.toBool(); break;
        case 1: {
            const QVector3D s = v.value<QVector3D>();
            f.size = QVector3D(qMax(0.01f, s.x()), qMax(0.01f, s.y()), qMax(0.01f, s.z()));
            break;
        }
        case 2: f.resolution = int(std::lround(qBound(8.0, x, 256.0))); break;
        case 3: f.dissipation = clamp(0.0, 10.0); break;
        case 4: f.cooling = clamp(0.0, 10.0); break;
        case 5: f.buoyancy = clamp(-1e3, 1e3); break;
        case 6: f.weight = clamp(-1e3, 1e3); break;
        case 7: f.vorticity = clamp(0.0, 100.0); break;
        case 8: f.pressureIterations = int(std::lround(qBound(1.0, x, 200.0))); break;
        case 9: f.color = v.value<QColor>(); break;
        case 10: f.density = clamp(0.0, 1e4); break;
        case 11: f.emissionColor = v.value<QColor>(); break;
        case 12: f.emission = clamp(0.0, 1e4); break;
        case 13: f.samples = int(std::lround(qBound(8.0, x, 512.0))); break;
        }
    };
    c.add = [](Entity &e) {
        if (!e.fluid)
            e.fluid = FluidVolume {};
    };
    c.remove = [](Entity &e) { e.fluid.reset(); };
    c.canAdd = [](const Entity &e) { return !e.mesh && !e.light && !e.environment && !e.camera && !e.emitter; };
    return c;
}

ComponentDesc fluidSourceComponent()
{
    ComponentDesc c;
    c.name = QStringLiteral("Fluid Source");
    c.properties = {
        { QStringLiteral("enabled"), PropertyType::Bool },
        { QStringLiteral("radius"), PropertyType::Float, 0.005, 0.0, 1e4 },
        { QStringLiteral("density"), PropertyType::Float, 0.01, 0.0, 1e4 },
        { QStringLiteral("temperature"), PropertyType::Float, 0.01, -1e4, 1e4 },
        { QStringLiteral("velocity"), PropertyType::Vector3, 0.02 },
    };
    c.present = [](const Entity &e) { return e.fluidSource.has_value(); };
    c.get = [](const Entity &e, int i) -> QVariant {
        if (!e.fluidSource)
            return {};
        const FluidSource &s = *e.fluidSource;
        switch (i) {
        case 0: return s.enabled;
        case 1: return double(s.radius);
        case 2: return double(s.density);
        case 3: return double(s.temperature);
        case 4: return QVariant::fromValue(s.velocity);
        }
        return {};
    };
    c.set = [](Entity &e, int i, const QVariant &v) {
        if (!e.fluidSource)
            return;
        FluidSource &s = *e.fluidSource;
        const double x = v.toDouble();
        switch (i) {
        case 0: s.enabled = v.toBool(); break;
        case 1: s.radius = float(qBound(0.0, x, 1e4)); break;
        case 2: s.density = float(qBound(0.0, x, 1e4)); break;
        case 3: s.temperature = float(qBound(-1e4, x, 1e4)); break;
        case 4: s.velocity = v.value<QVector3D>(); break;
        }
    };
    c.add = [](Entity &e) {
        if (!e.fluidSource)
            e.fluidSource = FluidSource {};
    };
    c.remove = [](Entity &e) { e.fluidSource.reset(); };
    c.canAdd = [](const Entity &e) { return !e.light && !e.environment && !e.camera; };
    return c;
}
} // namespace

const std::vector<ComponentDesc> &components()
{
    static const std::vector<ComponentDesc> all = { transformComponent(), meshComponent(), materialComponent(), lightComponent(),
                                                    environmentComponent(), cameraComponent(), postProcessComponent(),
                                                    emitterComponent(), forceComponent(), fluidVolumeComponent(),
                                                    fluidSourceComponent() };
    return all;
}
