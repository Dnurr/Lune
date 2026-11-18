#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QUndoStack>
#include <QVector3D>

#include "engine/io/sceneio.h"
#include "engine/scene/animation.h"
#include "engine/scene/primitives.h"
#include "engine/scene/scene.h"
#include "frontend/core/commands.h"
#include "frontend/core/selection.h"

namespace
{
// Indices of a component's property in components().
std::pair<int, int> find(const char *component, const char *property)
{
    const auto &comps = components();
    for (int c = 0; c < int(comps.size()); ++c) {
        if (comps[size_t(c)].name != QLatin1String(component))
            continue;
        for (int p = 0; p < int(comps[size_t(c)].properties.size()); ++p)
            if (comps[size_t(c)].properties[size_t(p)].key() == QLatin1String(property))
                return { c, p };
    }
    qFatal("no such property");
    return { -1, -1 };
}

AnimationTrack roughnessTrack(std::initializer_list<std::pair<double, double>> keys, Interpolation interpolation)
{
    AnimationTrack t { QStringLiteral("Material"), QStringLiteral("roughness"), {} };
    for (auto [frame, value] : keys)
        t.keys.push_back({ frame, value, interpolation });
    return t;
}

EntityId addSphere(Scene &scene)
{
    Entity e;
    e.name = QStringLiteral("Ball");
    e.mesh = Primitives::sphere();
    return scene.createEntity(e);
}
} // namespace

class TestAnimation : public QObject
{
    Q_OBJECT

private slots:
    void evaluateInterpolations()
    {
        const auto linear = roughnessTrack({ { 10, 0.0 }, { 20, 1.0 } }, Interpolation::Linear);
        QCOMPARE(Animation::evaluate(linear, PropertyType::Float, 0).toDouble(), 0.0);  // before: first key
        QCOMPARE(Animation::evaluate(linear, PropertyType::Float, 15).toDouble(), 0.5);
        QCOMPARE(Animation::evaluate(linear, PropertyType::Float, 99).toDouble(), 1.0); // after: last key

        const auto constant = roughnessTrack({ { 10, 0.0 }, { 20, 1.0 } }, Interpolation::Constant);
        QCOMPARE(Animation::evaluate(constant, PropertyType::Float, 19.9).toDouble(), 0.0);
        QCOMPARE(Animation::evaluate(constant, PropertyType::Float, 20).toDouble(), 1.0);

        // Bezier: flat at the ends (eases), symmetric, through the keys.
        const auto bezier = roughnessTrack({ { 10, 0.0 }, { 20, 1.0 } }, Interpolation::Bezier);
        QCOMPARE(Animation::evaluate(bezier, PropertyType::Float, 15).toDouble(), 0.5);
        QVERIFY(Animation::evaluate(bezier, PropertyType::Float, 11).toDouble() < 0.1 * 0.5); // slower than linear
        QCOMPARE(Animation::evaluate(bezier, PropertyType::Float, 20).toDouble(), 1.0);
    }

    void bezierNeverOvershoots()
    {
        // 0 -> 1 -> 1 -> 0: the middle stays at 1, never above.
        const auto t = roughnessTrack({ { 0, 0.0 }, { 10, 1.0 }, { 12, 1.0 }, { 30, 0.0 } }, Interpolation::Bezier);
        for (double f = 0; f <= 30; f += 0.25) {
            const double v = Animation::evaluate(t, PropertyType::Float, f).toDouble();
            QVERIFY2(v >= -1e-9 && v <= 1.0 + 1e-9, qPrintable(QStringLiteral("frame %1: %2").arg(f).arg(v)));
        }
    }

    void handTangents()
    {
        // A flat key turned into a slope: the curve leaves it rising, and the file keeps it.
        auto t = roughnessTrack({ { 0, 0.0 }, { 10, 0.0 } }, Interpolation::Bezier);
        QCOMPARE(Animation::evaluate(t, PropertyType::Float, 5).toDouble(), 0.0);
        t.keys[0].autoTangent = false;
        t.keys[0].slope[0] = 0.1;
        QVERIFY(Animation::evaluate(t, PropertyType::Float, 1).toDouble() > 0.0);
        QCOMPARE(Animation::tangent(t, PropertyType::Float, 0, 0), 0.1);
        QCOMPARE(Animation::tangent(t, PropertyType::Float, 1, 0), 0.0); // the auto end

        QTemporaryDir dir;
        const QString path = dir.filePath("tangent.lune");
        Scene scene;
        const EntityId id = addSphere(scene);
        scene.modifyEntity(id, [&](Entity &e) { e.animation = { t }; });
        QVERIFY(SceneIO::save(scene, path, {}));
        Scene loaded;
        QVERIFY(SceneIO::load(loaded, path));
        QCOMPARE(loaded.entities().front().animation[0], t);
    }

    void channels()
    {
        const QVariant color = QColor::fromRgbF(0.2f, 0.4f, 0.6f);
        QCOMPARE(Animation::channelCount(PropertyType::Color), 4);
        QVERIFY(qAbs(Animation::channel(PropertyType::Color, color, 1) - 0.4) < 1e-6);
        const QVariant brighter = Animation::withChannel(PropertyType::Color, color, 0, 1.0);
        QCOMPARE(brighter.value<QColor>().redF(), 1.0f);
        QCOMPARE(Animation::withChannel(PropertyType::Bool, false, 0, 0.7).toBool(), true);
    }

    void vectorsAndBooleans()
    {
        AnimationTrack pos { QStringLiteral("Transform"), QStringLiteral("position"), {} };
        pos.keys.push_back({ 0, QVariant::fromValue(QVector3D(0, 0, 0)), Interpolation::Linear });
        pos.keys.push_back({ 10, QVariant::fromValue(QVector3D(10, -2, 4)), Interpolation::Linear });
        QCOMPARE(Animation::evaluate(pos, PropertyType::Vector3, 5).value<QVector3D>(), QVector3D(5, -1, 2));

        AnimationTrack visible { QStringLiteral("Mesh"), QStringLiteral("visible"), {} };
        visible.keys.push_back({ 0, true, Interpolation::Linear });
        visible.keys.push_back({ 10, false, Interpolation::Linear });
        QCOMPARE(Animation::evaluate(visible, PropertyType::Bool, 9).toBool(), true); // booleans hold
        QCOMPARE(Animation::evaluate(visible, PropertyType::Bool, 10).toBool(), false);
    }

    void editingKeys()
    {
        const auto [c, p] = find("Material", "roughness");
        auto anim = Animation::withKey({}, c, p, 10, 0.2);
        anim = Animation::withKey(anim, c, p, 30, 0.8);
        anim = Animation::withKey(anim, c, p, 20, 0.5); // in between: sorted
        QCOMPARE(anim.size(), size_t(1));
        QCOMPARE(anim[0].keys.size(), size_t(3));
        QCOMPARE(anim[0].keys[1].frame, 20.0);
        anim = Animation::withKey(anim, c, p, 20, 0.6); // same frame: replaced
        QCOMPARE(anim[0].keys.size(), size_t(3));
        QCOMPARE(anim[0].keys[1].value.toDouble(), 0.6);

        anim = Animation::withInterpolation(anim, c, p, 25, Interpolation::Constant); // the key before 25
        QCOMPARE(anim[0].keys[1].interpolation, Interpolation::Constant);

        anim = Animation::withoutKey(anim, c, p, 10);
        anim = Animation::withoutKey(anim, c, p, 20);
        anim = Animation::withoutKey(anim, c, p, 30);
        QVERIFY(anim.empty()); // no keys left: no track
    }

    void frameChangesApplyValues()
    {
        Scene scene;
        const EntityId id = addSphere(scene);
        const auto [c, p] = find("Material", "roughness");
        scene.modifyEntity(id, [&](Entity &e) {
            e.animation = { roughnessTrack({ { 1, 0.0 }, { 11, 1.0 } }, Interpolation::Linear) };
        });
        scene.setFrame(6);
        Animation::apply(scene);
        QCOMPARE(scene.entity(id)->roughness, 0.5f);
        scene.setFrame(11);
        Animation::apply(scene);
        QCOMPARE(scene.entity(id)->roughness, 1.0f);
    }

    void editingAnimatedPropertySetsKey()
    {
        Scene scene;
        QUndoStack undo;
        const EntityId id = addSphere(scene);
        const auto [c, p] = find("Material", "roughness");
        scene.modifyEntity(id, [&](Entity &e) {
            e.animation = { roughnessTrack({ { 1, 0.0 }, { 11, 1.0 } }, Interpolation::Linear) };
        });
        scene.setFrame(6);
        Animation::apply(scene);

        undo.push(new SetPropertyCommand(&scene, id, c, p, 0.9));
        const AnimationTrack *t = Animation::track(*scene.entity(id), c, p);
        QVERIFY(t);
        QCOMPARE(t->keys.size(), size_t(3)); // a key at frame 6
        QCOMPARE(Animation::keyAt(*t, 6), 1);
        Animation::apply(scene);
        QCOMPARE(scene.entity(id)->roughness, 0.9f); // stays: the key holds it

        undo.undo();
        QCOMPARE(Animation::track(*scene.entity(id), c, p)->keys.size(), size_t(2));
        QCOMPARE(scene.entity(id)->roughness, 0.5f);
    }

    void gizmoMoveKeysAnimatedPosition()
    {
        Scene scene;
        QUndoStack undo;
        const EntityId id = addSphere(scene);
        AnimationTrack pos { QStringLiteral("Transform"), QStringLiteral("position"), {} };
        pos.keys.push_back({ 1, QVariant::fromValue(QVector3D(0, 0, 0)), Interpolation::Linear });
        scene.modifyEntity(id, [&](Entity &e) { e.animation = { pos }; });
        scene.setFrame(20);

        Transform before = scene.entity(id)->transform;
        Transform after = before;
        after.position = QVector3D(3, 0, 0);
        undo.push(new SetTransformCommand(&scene, id, before, after, TransformField::Position));
        Animation::apply(scene);
        QCOMPARE(scene.entity(id)->transform.position, QVector3D(3, 0, 0));
        QCOMPARE(scene.entity(id)->animation[0].keys.size(), size_t(2));
        scene.setFrame(1);
        Animation::apply(scene);
        QCOMPARE(scene.entity(id)->transform.position, QVector3D(0, 0, 0));
    }

    void removingKeyUndoes()
    {
        Scene scene;
        QUndoStack undo;
        const EntityId id = addSphere(scene);
        const auto [c, p] = find("Material", "roughness");
        scene.modifyEntity(id, [&](Entity &e) {
            e.animation = { roughnessTrack({ { 1, 0.2 }, { 11, 1.0 } }, Interpolation::Linear) };
        });
        scene.setFrame(1);
        Animation::apply(scene);
        const auto anim = Animation::withoutKey(scene.entity(id)->animation, c, p, 1);
        undo.push(new SetAnimationCommand(&scene, id, anim, QStringLiteral("Delete key")));
        QCOMPARE(scene.entity(id)->roughness, 1.0f); // only the key at 11 is left
        undo.undo();
        QCOMPARE(scene.entity(id)->roughness, 0.2f);
        QCOMPARE(scene.entity(id)->animation[0].keys.size(), size_t(2));
    }

    void savedInSceneFiles()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("anim.lune");
        Scene scene;
        const EntityId id = addSphere(scene);
        AnimationTrack pos { QStringLiteral("Transform"), QStringLiteral("position"), {} };
        pos.keys.push_back({ 1, QVariant::fromValue(QVector3D(0, 1, 0)), Interpolation::Constant });
        pos.keys.push_back({ 24, QVariant::fromValue(QVector3D(2, 1, 0)), Interpolation::Bezier });
        scene.modifyEntity(id, [&](Entity &e) {
            e.animation = { pos, roughnessTrack({ { 5, 0.25 } }, Interpolation::Linear) };
        });
        QVERIFY(SceneIO::save(scene, path, {}));

        Scene loaded;
        QStringList warnings;
        QVERIFY(SceneIO::load(loaded, path, nullptr, &warnings));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        const Entity &e = loaded.entities().front();
        QCOMPARE(e.animation.size(), size_t(2));
        QCOMPARE(e.animation[0], pos);
        QCOMPARE(e.animation[1].keys[0].value.toDouble(), 0.25);
    }
};

QTEST_GUILESS_MAIN(TestAnimation)
#include "tst_animation.moc"
