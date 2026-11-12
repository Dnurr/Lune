// Undo commands: every user edit must be undoable and redoable, and drags must merge.
#include <QTest>
#include <QUndoStack>

#include "engine/scene/components.h"
#include "engine/scene/primitives.h"
#include "engine/scene/scene.h"
#include "frontend/core/commands.h"
#include "frontend/core/selection.h"

namespace
{
// Index of a component / property in the descriptors, by name.
int componentIndex(const QString &name)
{
    const auto &all = components();
    for (int i = 0; i < int(all.size()); ++i) {
        if (all[size_t(i)].name == name)
            return i;
    }
    return -1;
}

int propertyIndex(int component, const QString &name)
{
    const auto &props = components()[size_t(component)].properties;
    for (int i = 0; i < int(props.size()); ++i) {
        if (props[size_t(i)].name == name)
            return i;
    }
    return -1;
}
} // namespace

class TestCommands : public QObject
{
    Q_OBJECT

    Scene *scene = nullptr;
    Selection *selection = nullptr;
    QUndoStack *stack = nullptr;

private slots:
    void init()
    {
        scene = new Scene(this);
        selection = new Selection(this);
        stack = new QUndoStack(this);
    }

    void cleanup()
    {
        delete stack;
        delete selection;
        delete scene;
    }

    void addUndoRedo()
    {
        Entity proto;
        proto.name = QStringLiteral("Cube");
        proto.mesh = Primitives::cube();
        stack->push(new AddEntityCommand(scene, selection, proto));

        QCOMPARE(scene->entities().size(), size_t(1));
        const EntityId id = scene->entities().front().id;
        QCOMPARE(selection->current(), id);
        QCOMPARE(scene->entity(id)->mesh, Primitives::cube());

        stack->undo();
        QVERIFY(scene->entities().empty());
        QVERIFY(selection->isEmpty()); // back to the previous selection (none)

        stack->redo();
        QVERIFY(scene->entity(id)); // same id after redo
        QCOMPARE(selection->current(), id);
    }

    void deleteRestoresPositionAndSelection()
    {
        scene->createEntity(QStringLiteral("A"));
        const EntityId b = scene->createEntity(QStringLiteral("B"));
        scene->createEntity(QStringLiteral("C"));
        selection->select(b);

        stack->push(new DeleteEntityCommand(scene, selection, b));
        QCOMPARE(scene->entities().size(), size_t(2));
        QVERIFY(selection->isEmpty());

        stack->undo();
        QCOMPARE(scene->indexOf(b), 1);
        QCOMPARE(selection->current(), b);
    }

    void deleteMissingEntityLeavesNoUndoStep()
    {
        stack->push(new DeleteEntityCommand(scene, selection, 42));
        QCOMPARE(stack->count(), 0); // obsolete commands are dropped
    }

    void rename()
    {
        const EntityId id = scene->createEntity(QStringLiteral("Old"));
        stack->push(new RenameEntityCommand(scene, id, QStringLiteral("New")));
        QCOMPARE(scene->entity(id)->name, QStringLiteral("New"));
        stack->undo();
        QCOMPARE(scene->entity(id)->name, QStringLiteral("Old"));
    }

    void propertyEditsMergeIntoOneStep()
    {
        const EntityId id = scene->createEntity(QStringLiteral("A"));
        const int transform = componentIndex(QStringLiteral("Transform"));
        const int position = propertyIndex(transform, QStringLiteral("position"));
        QVERIFY(transform >= 0 && position >= 0);

        // A drag on a label: many small edits in a row.
        for (int i = 1; i <= 5; ++i)
            stack->push(new SetPropertyCommand(scene, id, transform, position, QVariant::fromValue(QVector3D(i, 0, 0))));
        QCOMPARE(stack->count(), 1);
        QCOMPARE(scene->entity(id)->transform.position, QVector3D(5, 0, 0));

        stack->undo();
        QCOMPARE(scene->entity(id)->transform.position, QVector3D(0, 0, 0));
    }

    void differentPropertiesDontMerge()
    {
        const EntityId id = scene->createEntity(QStringLiteral("A"));
        const int transform = componentIndex(QStringLiteral("Transform"));
        const int position = propertyIndex(transform, QStringLiteral("position"));
        const int scale = propertyIndex(transform, QStringLiteral("scale"));
        stack->push(new SetPropertyCommand(scene, id, transform, position, QVariant::fromValue(QVector3D(1, 0, 0))));
        stack->push(new SetPropertyCommand(scene, id, transform, scale, QVariant::fromValue(QVector3D(2, 2, 2))));
        QCOMPARE(stack->count(), 2);
    }

    void editBackToStartLeavesNoUndoStep()
    {
        const EntityId id = scene->createEntity(QStringLiteral("A"));
        const int transform = componentIndex(QStringLiteral("Transform"));
        const int position = propertyIndex(transform, QStringLiteral("position"));
        stack->push(new SetPropertyCommand(scene, id, transform, position, QVariant::fromValue(QVector3D(1, 0, 0))));
        stack->push(new SetPropertyCommand(scene, id, transform, position, QVariant::fromValue(QVector3D(0, 0, 0))));
        QCOMPARE(stack->count(), 0);
    }

    void meshDisplayProperties()
    {
        Entity proto;
        proto.mesh = Primitives::sphere();
        const EntityId id = scene->createEntity(proto);
        const int mesh = componentIndex(QStringLiteral("Mesh"));
        const int visible = propertyIndex(mesh, QStringLiteral("visible"));
        const int color = propertyIndex(mesh, QStringLiteral("color"));

        stack->push(new SetPropertyCommand(scene, id, mesh, visible, false));
        stack->push(new SetPropertyCommand(scene, id, mesh, color, QColor(Qt::red)));
        QVERIFY(!scene->entity(id)->visible);
        QCOMPARE(scene->entity(id)->displayColor, QColor(Qt::red));
        stack->undo();
        stack->undo();
        QVERIFY(scene->entity(id)->visible);
    }

    void gizmoTransformUsesExplicitBefore()
    {
        const EntityId id = scene->createEntity(QStringLiteral("A"));
        const Transform before = scene->entity(id)->transform;
        Transform after = before;
        after.position = QVector3D(3, 0, 0);
        scene->setTransform(id, after); // live preview during the drag

        stack->push(new SetTransformCommand(scene, id, before, after, TransformField::Position));
        stack->push(new SetTransformCommand(scene, id, after, before, TransformField::Position));
        QCOMPARE(stack->count(), 2); // gizmo drags never merge

        stack->undo();
        stack->undo();
        QCOMPARE(scene->entity(id)->transform.position, QVector3D(0, 0, 0));
    }
};

QTEST_GUILESS_MAIN(TestCommands)
#include "tst_commands.moc"
