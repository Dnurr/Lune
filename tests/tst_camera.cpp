#include <QTest>

#include "engine/render/camera.h"

namespace
{
bool near(const QVector3D &a, const QVector3D &b, float eps = 1e-4f)
{
    return (a - b).length() < eps;
}
} // namespace

class TestCamera : public QObject
{
    Q_OBJECT

private slots:
    void sceneCameraLooksAlongMinusZ()
    {
        Transform t;
        t.position = QVector3D(1, 2, 3);
        t.rotation = QVector3D(-20, 35, 10); // tilted down, turned, rolled
        Camera lens;
        lens.fieldOfView = 35.0f;
        const OrbitCamera c = OrbitCamera::fromSceneCamera(t, lens);
        QVERIFY(near(c.eye(), t.position));
        QVERIFY(near(c.forward(), -t.axis(2)));
        QVERIFY(near(c.up(), t.axis(1)));
        QCOMPARE(c.fieldOfView, 35.0f);
        // The view matrix agrees: the camera's local axes map to view space axes.
        const QMatrix4x4 v = c.view();
        QVERIFY(near(v.mapVector(t.axis(2)), QVector3D(0, 0, 1)));
        QVERIFY(near(v.mapVector(t.axis(1)), QVector3D(0, 1, 0)));

        // And back: the rotation that gives this view is the one we started from.
        Transform back;
        back.rotation = c.sceneCameraRotation(t.rotation);
        QVERIFY(near(back.axis(0), t.axis(0)));
        QVERIFY(near(back.axis(1), t.axis(1)));
        QVERIFY(near(back.rotation, t.rotation, 1e-2f));
    }

    void editorViewBecomesASceneCamera()
    {
        OrbitCamera view; // yaw 45, pitch 30, no roll
        Transform t;
        t.position = view.eye();
        t.rotation = view.sceneCameraRotation();
        const OrbitCamera c = OrbitCamera::fromSceneCamera(t, Camera {});
        QVERIFY(near(c.eye(), view.eye()));
        QVERIFY(near(c.forward(), view.forward()));
        QVERIFY(qAbs(c.roll) < 1e-3f);
    }

    void lookKeepsTheEye()
    {
        OrbitCamera c;
        const QVector3D eye = c.eye();
        c.look(30.0f, -10.0f);
        QVERIFY(near(c.eye(), eye));
        QCOMPARE(c.yaw, 45.0f + 30.0f);
        QCOMPARE(c.pitch, 30.0f - 10.0f);
        QVERIFY(qAbs((c.target - c.eye()).length() - c.distance) < 1e-4f); // target still ahead
    }

    void lookTurnsLikeTheMouse()
    {
        // Same convention as the viewport: mouse right = negative yaw = turn right.
        OrbitCamera c;
        const QVector3D right = c.right();
        const QVector3D before = c.forward();
        c.look(-5.0f, 0.0f);
        QVERIFY(QVector3D::dotProduct(c.forward() - before, right) > 0.0f);
    }

    void rightIsHorizontalAndPerpendicular()
    {
        OrbitCamera c;
        c.yaw = 123.0f;
        c.pitch = 40.0f;
        QVERIFY(qAbs(c.right().y()) < 1e-6f);
        QVERIFY(qAbs(QVector3D::dotProduct(c.right(), c.forward())) < 1e-5f);
        // Right-handed: forward x up points right.
        QVERIFY(near(QVector3D::crossProduct(c.forward(), QVector3D(0, 1, 0)).normalized(), c.right()));
    }

    void flyMovesEyeAndTargetTogether()
    {
        OrbitCamera c;
        const QVector3D eye = c.eye(), target = c.target, forward = c.forward();
        c.fly(QVector3D(0, 0, 2.0f));
        QVERIFY(near(c.eye(), eye + forward * 2.0f));
        QVERIFY(near(c.target, target + forward * 2.0f));
        c.fly(QVector3D(0, 1.5f, 0)); // world up, whatever the pitch
        QVERIFY(near(c.eye(), eye + forward * 2.0f + QVector3D(0, 1.5f, 0)));
    }

    void smoothingReachesTheGoal()
    {
        OrbitCamera drawn, goal;
        goal.target = QVector3D(3, 1, -2);
        goal.yaw = 120.0f;
        goal.pitch = -20.0f;
        goal.distance = 2.0f;
        drawn.moveTowards(goal, 0.5f);
        QVERIFY(near(drawn.target, QVector3D(1.5f, 0.5f, -1.0f)));
        QCOMPARE(drawn.yaw, (45.0f + 120.0f) / 2.0f);
        QVERIFY(qAbs(drawn.distance - 4.0f) < 1e-4f); // halfway in log space: sqrt(8 * 2)
        QVERIFY(!drawn.isCloseTo(goal));
        for (int i = 0; i < 40; ++i)
            drawn.moveTowards(goal, 0.5f);
        QVERIFY(drawn.isCloseTo(goal));
        drawn.moveTowards(goal, 1.0f);
        QVERIFY(near(drawn.eye(), goal.eye()));
    }
};

QTEST_GUILESS_MAIN(TestCamera)
#include "tst_camera.moc"
