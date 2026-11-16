#pragma once

#include <QElapsedTimer>
#include <QUndoCommand>
#include <QVariant>

#include "engine/scene/entity.h"
#include "engine/scene/material.h"

class Scene;
class Selection;

// Every change to the scene made by the user goes through one of these commands,
// pushed on the editor's QUndoStack: that's what makes it undoable.
// (Code that must not be undoable, e.g. loading a file, edits the Scene directly.)

class AddEntityCommand : public QUndoCommand
{
public:
    // `prototype`: name, transform, mesh of the new entity (its id is assigned on the first redo).
    AddEntityCommand(Scene *scene, Selection *selection, const Entity &prototype);

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

// Edit of any reflected property (see engine/scene/components.h), used by the Inspector.
// Consecutive edits of the same property of the same entity that arrive close together
// (e.g. the many small steps of a drag on a label) merge into one undo step.
class SetPropertyCommand : public QUndoCommand
{
public:
    SetPropertyCommand(Scene *scene, EntityId id, int component, int property, const QVariant &after);

    void redo() override;
    void undo() override;

    int id() const override { return 2; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    void apply(const QVariant &value);

    Scene *m_scene;
    EntityId m_entity;
    int m_component, m_property;
    QVariant m_before, m_after;
    QElapsedTimer m_lastEdit;
};

// Transform edit.
// From the Inspector: consecutive edits of the same field of the same entity that arrive
// close together (e.g. the many small steps of a drag on a label) merge into one undo step.
// From a gizmo drag: the scene is previewed live during the drag, then one command with the
// explicit `before` state is pushed on release (never merged).
class SetTransformCommand : public QUndoCommand
{
public:
    SetTransformCommand(Scene *scene, EntityId id, const Transform &after, TransformField field);
    SetTransformCommand(Scene *scene, EntityId id, const Transform &before, const Transform &after, TransformField field);

    void redo() override;
    void undo() override;

    int id() const override { return 1; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    Scene *m_scene;
    EntityId m_entity;
    Transform m_before, m_after;
    TransformField m_field;
    bool m_mergeable = true;
    QElapsedTimer m_lastEdit;
};

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------

// New material in the scene's library, optionally assigned to an entity in the same step.
class AddMaterialCommand : public QUndoCommand
{
public:
    // Adds the material and, if `assignTo` is an entity, puts it in that entity's `slot`.
    AddMaterialCommand(Scene *scene, const Material &prototype, EntityId assignTo = InvalidEntity, int slot = 0);

    void redo() override;
    void undo() override;

    MaterialId materialId() const { return m_material.id; }

private:
    Scene *m_scene;
    Material m_material; // id assigned on the first redo, then reused
    EntityId m_entity;
    int m_slot = 0;
    MaterialId m_previous = NoMaterial; // the entity's material before
};

// Puts a material (or none) in one of an entity's material slots.
class AssignMaterialCommand : public QUndoCommand
{
public:
    AssignMaterialCommand(Scene *scene, EntityId entity, int slot, MaterialId material);

    void redo() override;
    void undo() override;

private:
    Scene *m_scene;
    EntityId m_entity;
    int m_slot;
    MaterialId m_before, m_after;
};

// Any edit of a material's node graph: the whole graph before and after (graphs are small).
// Edits with the same non-empty `mergeKey` on the same material that arrive close together
// (dragging a node, scrubbing a value) merge into one undo step.
class SetMaterialGraphCommand : public QUndoCommand
{
public:
    SetMaterialGraphCommand(Scene *scene, MaterialId material, const NodeGraph &after, const QString &text,
                            const QString &mergeKey = QString());

    void redo() override;
    void undo() override;

    int id() const override { return 4; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    Scene *m_scene;
    MaterialId m_material;
    NodeGraph m_before, m_after;
    QString m_mergeKey;
    QElapsedTimer m_lastEdit;
};

// A material's alpha mode and cutoff. Cutoff edits close together (dragging the value)
// merge into one undo step.
class SetMaterialAlphaCommand : public QUndoCommand
{
public:
    SetMaterialAlphaCommand(Scene *scene, MaterialId material, AlphaMode mode, float cutoff);

    void redo() override;
    void undo() override;

    int id() const override { return 5; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    Scene *m_scene;
    MaterialId m_material;
    AlphaMode m_beforeMode = AlphaMode::Opaque, m_afterMode;
    float m_beforeCutoff = 0.5f, m_afterCutoff;
    QElapsedTimer m_lastEdit;
};

class RenameMaterialCommand : public QUndoCommand
{
public:
    RenameMaterialCommand(Scene *scene, MaterialId material, const QString &name);

    void redo() override;
    void undo() override;

private:
    Scene *m_scene;
    MaterialId m_material;
    QString m_before, m_after;
};
