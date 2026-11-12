#include "commands.h"

#include "engine/scene/scene.h"
#include "selection.h"

namespace
{
// Edits further apart than this become separate undo steps.
constexpr qint64 kMergeWindowMs = 600;

QString fieldName(TransformField f)
{
    switch (f) {
    case TransformField::Position:
        return QStringLiteral("position");
    case TransformField::Rotation:
        return QStringLiteral("rotation");
    case TransformField::Scale:
        return QStringLiteral("scale");
    }
    return {};
}
} // namespace

// ---------------------------------------------------------------------------
// Add
// ---------------------------------------------------------------------------

AddEntityCommand::AddEntityCommand(Scene *scene, Selection *selection, const QString &name, const Transform &transform)
    : m_scene(scene)
    , m_selection(selection)
{
    m_entity.name = name;
    m_entity.transform = transform;
    setText(QStringLiteral("Add %1").arg(name));
}

void AddEntityCommand::redo()
{
    m_previousSelection = m_selection->current();
    if (m_entity.id == InvalidEntity)
        m_entity.id = m_scene->createEntity(m_entity.name, m_entity.transform);
    else
        m_scene->insertEntity(m_entity); // redo after undo: same id
    m_selection->select(m_entity.id);
}

void AddEntityCommand::undo()
{
    if (const Entity *e = m_scene->entity(m_entity.id))
        m_entity = *e; // keep the latest state for a later redo
    m_scene->removeEntity(m_entity.id);
    m_selection->select(m_previousSelection);
}

// ---------------------------------------------------------------------------
// Delete
// ---------------------------------------------------------------------------

DeleteEntityCommand::DeleteEntityCommand(Scene *scene, Selection *selection, EntityId id)
    : m_scene(scene)
    , m_selection(selection)
{
    if (const Entity *e = scene->entity(id))
        m_entity = *e;
    setText(QStringLiteral("Delete %1").arg(m_entity.name));
}

void DeleteEntityCommand::redo()
{
    m_index = m_scene->indexOf(m_entity.id);
    if (m_index < 0) {
        setObsolete(true); // the entity is already gone: nothing to do or undo
        return;
    }
    m_entity = *m_scene->entity(m_entity.id);
    m_wasSelected = m_selection->current() == m_entity.id;
    if (m_wasSelected)
        m_selection->clear();
    m_scene->removeEntity(m_entity.id);
}

void DeleteEntityCommand::undo()
{
    m_scene->insertEntity(m_entity, m_index);
    if (m_wasSelected)
        m_selection->select(m_entity.id);
}

// ---------------------------------------------------------------------------
// Rename
// ---------------------------------------------------------------------------

RenameEntityCommand::RenameEntityCommand(Scene *scene, EntityId id, const QString &newName)
    : m_scene(scene)
    , m_id(id)
    , m_after(newName)
{
    if (const Entity *e = scene->entity(id))
        m_before = e->name;
    setText(QStringLiteral("Rename %1 to %2").arg(m_before, m_after));
}

void RenameEntityCommand::redo()
{
    m_scene->setName(m_id, m_after);
}

void RenameEntityCommand::undo()
{
    m_scene->setName(m_id, m_before);
}

// ---------------------------------------------------------------------------
// Transform
// ---------------------------------------------------------------------------

SetTransformCommand::SetTransformCommand(Scene *scene, EntityId id, const Transform &after, TransformField field)
    : m_scene(scene)
    , m_entity(id)
    , m_after(after)
    , m_field(field)
{
    if (const Entity *e = scene->entity(id))
        m_before = e->transform;
    m_lastEdit.start();
    setText(QStringLiteral("Change %1").arg(fieldName(field)));
}

void SetTransformCommand::redo()
{
    m_scene->setTransform(m_entity, m_after);
}

void SetTransformCommand::undo()
{
    m_scene->setTransform(m_entity, m_before);
}

bool SetTransformCommand::mergeWith(const QUndoCommand *other)
{
    const auto *o = static_cast<const SetTransformCommand *>(other); // same id() = same type
    if (o->m_entity != m_entity || o->m_field != m_field || m_lastEdit.elapsed() > kMergeWindowMs)
        return false;
    m_after = o->m_after;
    m_lastEdit.restart();
    setObsolete(m_after == m_before); // dragged back to where it started: no undo step
    return true;
}
