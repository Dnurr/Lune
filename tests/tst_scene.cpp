// Scene: entity list, ids, change notifications.
#include <QSignalSpy>
#include <QTest>

#include "engine/scene/scene.h"

class TestScene : public QObject
{
    Q_OBJECT

private slots:
    void createAssignsIncreasingIds()
    {
        Scene scene;
        QSignalSpy added(&scene, &Scene::entityAdded);
        const EntityId a = scene.createEntity(QStringLiteral("A"));
        const EntityId b = scene.createEntity(QStringLiteral("B"));
        QCOMPARE(a, EntityId(1));
        QCOMPARE(b, EntityId(2));
        QCOMPARE(scene.entities().size(), size_t(2));
        QCOMPARE(added.count(), 2);
        QCOMPARE(added.at(1).at(0).value<EntityId>(), b);
        QCOMPARE(added.at(1).at(1).toInt(), 1); // index
    }

    void createAtIndex()
    {
        Scene scene;
        scene.createEntity(QStringLiteral("A"));
        scene.createEntity(QStringLiteral("C"));
        const EntityId b = scene.createEntity(QStringLiteral("B"), {}, 1);
        QCOMPARE(scene.indexOf(b), 1);
        QCOMPARE(scene.entities()[1].name, QStringLiteral("B"));
    }

    void idsAreNeverReused()
    {
        Scene scene;
        scene.createEntity(QStringLiteral("A"));
        const EntityId b = scene.createEntity(QStringLiteral("B"));
        QVERIFY(scene.removeEntity(b));
        QCOMPARE(scene.createEntity(QStringLiteral("C")), EntityId(3));
    }

    void insertKeepsIdAndAdvancesCounter()
    {
        Scene scene;
        Entity e;
        e.id = 10;
        e.name = QStringLiteral("Restored");
        scene.insertEntity(e);
        QVERIFY(scene.entity(10));
        QCOMPARE(scene.createEntity(QStringLiteral("Next")), EntityId(11));
    }

    void removeUnknownFails()
    {
        Scene scene;
        QSignalSpy removed(&scene, &Scene::entityRemoved);
        QVERIFY(!scene.removeEntity(42));
        QCOMPARE(removed.count(), 0);
    }

    void clearRemovesAllAndRestartsIds()
    {
        Scene scene;
        scene.createEntity(QStringLiteral("A"));
        scene.createEntity(QStringLiteral("B"));
        QSignalSpy removed(&scene, &Scene::entityRemoved);
        scene.clear();
        QCOMPARE(removed.count(), 2);
        QVERIFY(scene.entities().empty());
        QCOMPARE(scene.createEntity(QStringLiteral("C")), EntityId(1));
    }

    void setTransformNotifiesOnlyOnChange()
    {
        Scene scene;
        const EntityId id = scene.createEntity(QStringLiteral("A"));
        QSignalSpy changed(&scene, &Scene::entityChanged);
        scene.setTransform(id, Transform {}); // same as the default
        QCOMPARE(changed.count(), 0);
        Transform t;
        t.position = QVector3D(1, 2, 3);
        scene.setTransform(id, t);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(scene.entity(id)->transform.position, QVector3D(1, 2, 3));
    }

    void setNameNotifiesOnlyOnChange()
    {
        Scene scene;
        const EntityId id = scene.createEntity(QStringLiteral("A"));
        QSignalSpy changed(&scene, &Scene::entityChanged);
        scene.setName(id, QStringLiteral("A"));
        QCOMPARE(changed.count(), 0);
        scene.setName(id, QStringLiteral("B"));
        QCOMPARE(changed.count(), 1);
    }

    void modifyEntity()
    {
        Scene scene;
        const EntityId id = scene.createEntity(QStringLiteral("A"));
        QSignalSpy changed(&scene, &Scene::entityChanged);
        scene.modifyEntity(id, [](Entity &e) { e.visible = false; });
        QCOMPARE(changed.count(), 1);
        QVERIFY(!scene.entity(id)->visible);
        scene.modifyEntity(999, [](Entity &e) { e.visible = true; }); // unknown id: no-op
        QCOMPARE(changed.count(), 1);
    }

    void uniqueName()
    {
        Scene scene;
        QCOMPARE(scene.uniqueName(QStringLiteral("Entity")), QStringLiteral("Entity"));
        scene.createEntity(QStringLiteral("Entity"));
        QCOMPARE(scene.uniqueName(QStringLiteral("Entity")), QStringLiteral("Entity 2"));
        scene.createEntity(QStringLiteral("Entity 2"));
        QCOMPARE(scene.uniqueName(QStringLiteral("Entity")), QStringLiteral("Entity 3"));
    }

    void transformMatrix()
    {
        Transform t;
        t.position = QVector3D(1, 0, 0);
        t.rotation = QVector3D(0, 90, 0);
        t.scale = QVector3D(2, 2, 2);
        // (1,0,0) scaled to (2,0,0), rotated 90 deg around Y to (0,0,-2), moved by +1 on X.
        const QVector3D p = t.matrix().map(QVector3D(1, 0, 0));
        QVERIFY(qFuzzyCompare(p.x(), 1.0f));
        QVERIFY(qAbs(p.y()) < 1e-5f);
        QVERIFY(qFuzzyCompare(p.z(), -2.0f));
    }
};

QTEST_GUILESS_MAIN(TestScene)
#include "tst_scene.moc"
