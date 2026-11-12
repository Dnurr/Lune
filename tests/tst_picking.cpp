// Picking (ray vs. meshes) and the camera's screen <-> world helpers.
#include <QTest>

#include "engine/render/camera.h"
#include "engine/scene/picking.h"
#include "engine/scene/primitives.h"
#include "engine/scene/scene.h"

namespace
{
EntityId addCube(Scene &scene, const QVector3D &position, const QVector3D &scale = QVector3D(1, 1, 1))
{
    Entity e;
    e.name = QStringLiteral("Cube");
    e.mesh = Primitives::cube();
    e.transform.position = position;
    e.transform.scale = scale;
    return scene.createEntity(e);
}

const QVector3D kDown(0, 0, -1); // rays from z = 10 towards the origin
} // namespace

class TestPicking : public QObject
{
    Q_OBJECT

private slots:
    void hitFromScreenCenter()
    {
        Scene scene;
        const EntityId cube = addCube(scene, QVector3D());
        OrbitCamera camera; // looks at the origin
        const QSizeF view(800, 600);
        QVector3D o, d;
        camera.screenRay(QPointF(400, 300), view, &o, &d);
        QCOMPARE(pickEntity(scene, o, d), cube);

        camera.screenRay(QPointF(5, 5), view, &o, &d); // corner: misses the small cube
        QCOMPARE(pickEntity(scene, o, d), InvalidEntity);
    }

    void nearestWins()
    {
        Scene scene;
        addCube(scene, QVector3D(0, 0, 0));
        const EntityId front = addCube(scene, QVector3D(0, 0, 3));
        float t = 0;
        QCOMPARE(pickEntity(scene, QVector3D(0, 0, 10), kDown, &t), front);
        QVERIFY(qAbs(t - 6.5f) < 1e-4f); // front face of the near cube is at z = 3.5
    }

    void transformedEntity()
    {
        Scene scene;
        const EntityId cube = addCube(scene, QVector3D(5, 0, 0), QVector3D(2, 2, 2)); // spans x 4..6
        QCOMPARE(pickEntity(scene, QVector3D(5.9f, 0, 10), kDown), cube);
        QCOMPARE(pickEntity(scene, QVector3D(6.1f, 0, 10), kDown), InvalidEntity);
    }

    void invisibleAndEmptyEntitiesAreSkipped()
    {
        Scene scene;
        const EntityId hidden = addCube(scene, QVector3D(0, 0, 3));
        scene.modifyEntity(hidden, [](Entity &e) { e.visible = false; });
        scene.createEntity(QStringLiteral("Empty")); // no mesh
        const EntityId back = addCube(scene, QVector3D());
        QCOMPARE(pickEntity(scene, QVector3D(0, 0, 10), kDown), back);
    }

    void rayBehindTheOriginMisses()
    {
        Scene scene;
        addCube(scene, QVector3D());
        QCOMPARE(pickEntity(scene, QVector3D(0, 0, 10), -kDown), InvalidEntity); // pointing away
    }

    void projectAndScreenRayAgree()
    {
        OrbitCamera camera;
        camera.yaw = 30;
        camera.pitch = 20;
        camera.target = QVector3D(1, 0, -2);
        const QSizeF view(1024, 576);
        const QVector3D world(2, 1, -1);

        bool inFront = false;
        const QPointF screen = camera.project(world, view, &inFront);
        QVERIFY(inFront);
        QVector3D o, d;
        camera.screenRay(screen, view, &o, &d);
        // Distance from the point to the ray through its own projection.
        const QVector3D dn = d.normalized();
        const QVector3D v = world - o;
        const float distance = (v - dn * QVector3D::dotProduct(v, dn)).length();
        QVERIFY2(distance < 1e-3f, qPrintable(QString::number(distance)));
    }

    void pointBehindCameraIsReported()
    {
        OrbitCamera camera;
        bool inFront = true;
        camera.project(camera.eye() + (camera.eye() - camera.target), QSizeF(800, 600), &inFront);
        QVERIFY(!inFront);
    }
};

QTEST_GUILESS_MAIN(TestPicking)
#include "tst_picking.moc"
