// Transform gizmo: hit testing and the transform produced by a drag.
#include <QTest>

#include "frontend/components/viewport/gizmo.h"

namespace
{
const QSizeF kView(800, 600);

bool near(const QVector3D &a, const QVector3D &b, float eps = 1e-3f)
{
    return (a - b).length() < eps;
}
} // namespace

class TestGizmo : public QObject
{
    Q_OBJECT

    OrbitCamera camera; // default: looks at the origin from above
    Transform transform;

private slots:
    void hitTestFindsEachAxis()
    {
        Gizmo g;
        g.layout(camera, kView, transform);
        QVERIFY(g.isVisible());
        // 0.5 world units along each axis lies on its handle (handles are ~1.4 units here).
        QCOMPARE(g.hitTest(camera.project(QVector3D(0.5f, 0, 0), kView)), Gizmo::AxisX);
        QCOMPARE(g.hitTest(camera.project(QVector3D(0, 0.5f, 0), kView)), Gizmo::AxisY);
        QCOMPARE(g.hitTest(camera.project(QVector3D(0, 0, 0.5f), kView)), Gizmo::AxisZ);
        QCOMPARE(g.hitTest(QPointF(10, 10)), Gizmo::None);
    }

    void scaleModeHasUniformHandle()
    {
        Gizmo g;
        g.setMode(Gizmo::Mode::Scale);
        g.layout(camera, kView, transform);
        QCOMPARE(g.hitTest(camera.project(QVector3D(), kView)), Gizmo::Uniform);
    }

    void translateFollowsTheMouseAlongTheAxis()
    {
        Gizmo g;
        g.layout(camera, kView, transform);
        g.beginDrag(Gizmo::AxisX, camera.project(QVector3D(0.5f, 0, 0), kView), transform);
        const Transform t = g.dragTo(camera.project(QVector3D(1.5f, 0, 0), kView));
        QVERIFY2(near(t.position, QVector3D(1, 0, 0)),
                 qPrintable(QStringLiteral("%1 %2 %3").arg(t.position.x()).arg(t.position.y()).arg(t.position.z())));
        QCOMPARE(g.field(), TransformField::Position);

        // A mouse movement off the axis still moves only along it.
        const Transform t2 = g.dragTo(camera.project(QVector3D(1.5f, 0.7f, 0), kView) + QPointF(0, 0));
        QVERIFY(qAbs(t2.position.y()) < 1e-5f);
        QVERIFY(qAbs(t2.position.z()) < 1e-5f);
        g.endDrag();
        QVERIFY(!g.isDragging());
    }

    void scaleAxisGrowsOutwards()
    {
        Gizmo g;
        g.setMode(Gizmo::Mode::Scale);
        g.layout(camera, kView, transform);
        const QPointF start = camera.project(QVector3D(0.5f, 0, 0), kView);
        const QPointF outward = camera.project(QVector3D(1.0f, 0, 0), kView);
        g.beginDrag(Gizmo::AxisX, start, transform);
        const Transform t = g.dragTo(outward);
        QVERIFY(t.scale.x() > 1.0f);
        QCOMPARE(t.scale.y(), 1.0f);
        QCOMPARE(t.scale.z(), 1.0f);
        QCOMPARE(g.field(), TransformField::Scale);
    }

    void rotateAroundYCounterClockwiseIsPositive()
    {
        Gizmo g;
        g.setMode(Gizmo::Mode::Rotate);
        g.layout(camera, kView, transform);
        const QPointF c = camera.project(QVector3D(), kView);
        // Quarter turn counter-clockwise on screen, seen from above (Y points at the camera).
        g.beginDrag(Gizmo::AxisY, c + QPointF(100, 0), transform);
        g.dragTo(c + QPointF(70, -70));
        const Transform t = g.dragTo(c + QPointF(0, -100));
        QVERIFY2(qAbs(t.rotation.y() - 90.0f) < 0.5f, qPrintable(QString::number(t.rotation.y())));
        QCOMPARE(t.rotation.x(), 0.0f);
        QCOMPARE(t.rotation.z(), 0.0f);
        QCOMPARE(g.field(), TransformField::Rotation);
    }

    void rotationAccumulatesPastHalfATurn()
    {
        Gizmo g;
        g.setMode(Gizmo::Mode::Rotate);
        g.layout(camera, kView, transform);
        const QPointF c = camera.project(QVector3D(), kView);
        g.beginDrag(Gizmo::AxisY, c + QPointF(100, 0), transform);
        Transform t;
        for (int deg = 10; deg <= 270; deg += 10) { // counter-clockwise on screen
            const double a = qDegreesToRadians(double(deg));
            t = g.dragTo(c + QPointF(100 * std::cos(a), -100 * std::sin(a)));
        }
        QVERIFY2(qAbs(t.rotation.y() - 270.0f) < 0.5f, qPrintable(QString::number(t.rotation.y())));
    }

    void hiddenWhenBehindTheCamera()
    {
        Gizmo g;
        Transform behind;
        behind.position = camera.eye() + (camera.eye() - camera.target);
        g.layout(camera, kView, behind);
        QVERIFY(!g.isVisible());
        QCOMPARE(g.hitTest(QPointF(400, 300)), Gizmo::None);
    }
};

QTEST_GUILESS_MAIN(TestGizmo)
#include "tst_gizmo.moc"
