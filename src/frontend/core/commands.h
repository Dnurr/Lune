#pragma once

#include <QElapsedTimer>
#include <QUndoCommand>

#include "engine/scene/entity.h"

class Scene;
class Selection;

// Every change to the scene made by the user goes through one of these commands,
// pushed on the editor's QUndoStack: that's what makes it undoable.
// (Code that must not be undoable, e.g. loading a file, edits the Scene directly.)

class AddEntityCommand : public QUndoCommand
{
public:
    AddEntityCommand(Scene *scene, Selection *selection, const QString &name, const Transform &transform = {});

    void redo() override;
    void undo() override;

    EntityId entityId() const { return m_entity.id; }

private:
    Scene *m_scene;
    Selection *m_selection;
    Entity m_entity; // id assigned on the first redo, then reused
    EntityId m_previousSelection = InvalidEntity;
};

class DeleteEntityCommand : public QUndoCommand
{
public:
    DeleteEntityCommand(Scene *scene, Selection *selection, EntityId id);

    void redo() override;
    void undo() override;

private:
    Scene *m_scene;
    Selection *m_selection;
    Entity m_entity; // full copy, to restore it on undo
    int m_index = -1;
    bool m_wasSelected = false;
};

class RenameEntityCommand : public QUndoCommand
{
public:
    RenameEntityCommand(Scene *scene, EntityId id, const QString &newName);

    void redo() override;
    void undo() override;

private:
    Scene *m_scene;
    EntityId m_id;
    QString m_before, m_after;
};

// Transform edit. Consecutive edits of the same field of the same entity that arrive
// close together (e.g. the many small steps of a drag on a label) merge into a single
// undo step.
class SetTransformCommand : public QUndoCommand
{
public:
    SetTransformCommand(Scene *scene, EntityId id, const Transform &after, TransformField field);

    void redo() override;
    void undo() override;

    int id() const override { return 1; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    Scene *m_scene;
    EntityId m_entity;
    Transform m_before, m_after;
    TransformField m_field;
    QElapsedTimer m_lastEdit;
};
