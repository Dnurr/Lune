// Light icons in the viewport: hit testing, and the direction of a directional light.
#include <QTest>

#include "engine/scene/scene.h"
#include "frontend/components/viewport/lighticons.h"

namespace
{
const QSizeF kView(800, 600);
}

class TestLightIcons : public QObject
{
    Q_OBJECT

    OrbitCamera camera;

private slots:
    void clickOnTheIconSelectsTheLight()
    {
        Scene scene;
        Entity light;
        light.light = Light {};
        light.transform.position = QVector3D(1, 0, 0);
        const EntityId id = scene.createEntity(light);
        scene.createEntity(QStringLiteral("Not a light"));

        const QPointF icon = camera.project(light.transform.position, kView);
        QCOMPARE(LightIcons::hitTest(scene, camera, kView, icon), id);
        QCOMPARE(LightIcons::hitTest(scene, camera, kView, icon + QPointF(5, 5)), id);
        QCOMPARE(LightIcons::hitTest(scene, camera, kView, icon + QPointF(40, 0)), InvalidEntity);
    }

    void closestIconWins()
    {
        Scene scene;
        Entity a;
        a.light = Light {};
        Entity b = a;
        a.transform.position = QVector3D(0, 0, 0);
        b.transform.position = QVector3D(0.05f, 0, 0); // a few pixels apart
        scene.createEntity(a);
        const EntityId idB = scene.createEntity(b);
        QCOMPARE(LightIcons::hitTest(scene, camera, kView, camera.project(b.transform.position, kView)), idB);
    }

    void iconsAreDrawnOnlyForLights()
    {
        Scene scene;
        scene.createEntity(QStringLiteral("Empty"));
        std::vector<OverlayVertex> out;
        LightIcons::append(scene, camera, kView, InvalidEntity, out);
        QVERIFY(out.empty());

        Entity light;
        light.light = Light {};
        scene.createEntity(light);
        LightIcons::append(scene, camera, kView, InvalidEntity, out);
        QVERIFY(!out.empty());
        QCOMPARE(out.size() % 3, size_t(0)); // whole triangles
    }

    void directionalLightPointsDownWithoutRotation()
    {
        Transform t;
        QVERIFY((Light::direction(t) - QVector3D(0, -1, 0)).length() < 1e-5f);
        t.rotation = QVector3D(90, 0, 0); // tilt around X: -Y turns towards -Z
        QVERIFY((Light::direction(t) - QVector3D(0, 0, -1)).length() < 1e-5f);
        t.scale = QVector3D(5, 0.1f, 2); // scale doesn't change the direction
        QVERIFY((Light::direction(t) - QVector3D(0, 0, -1)).length() < 1e-5f);
    }
};

QTEST_GUILESS_MAIN(TestLightIcons)
#include "tst_lighticons.moc"
