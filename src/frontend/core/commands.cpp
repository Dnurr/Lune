#include "commands.h"

#include "engine/scene/components.h"
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

AddEntityCommand::AddEntityCommand(Scene *scene, Selection *selection, const Entity &prototype)
    : m_scene(scene)
    , m_selection(selection)
    , m_entity(prototype)
{
    m_entity.id = InvalidEntity;
    setText(QStringLiteral("Add %1").arg(m_entity.name));
}

void AddEntityCommand::redo()
{
    m_previousSelection = m_selection->current();
    if (m_entity.id == InvalidEntity)
        m_entity.id = m_scene->createEntity(m_entity);
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

SetComponentCommand::SetComponentCommand(Scene *scene, EntityId id, int component, bool present)
    : m_scene(scene)
    , m_entity(id)
    , m_component(component)
    , m_present(present)
{
    if (const Entity *e = scene->entity(id))
        m_before = *e;
    const QString name = components()[size_t(component)].name;
    setText(present ? QStringLiteral("Add %1").arg(name) : QStringLiteral("Remove %1").arg(name));
}

void SetComponentCommand::redo()
{
    const ComponentDesc &c = components()[size_t(m_component)];
    m_scene->modifyEntity(m_entity, [&](Entity &e) {
        if (m_present && c.add)
            c.add(e);
        else if (!m_present && c.remove)
            c.remove(e);
    });
}

void SetComponentCommand::undo()
{
    const Entity before = m_before;
    m_scene->modifyEntity(m_entity, [&](Entity &e) { e = before; });
}

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
// Property
// ---------------------------------------------------------------------------

SetPropertyCommand::SetPropertyCommand(Scene *scene, EntityId id, int component, int property, const QVariant &after)
    : m_scene(scene)
    , m_entity(id)
    , m_component(component)
    , m_property(property)
    , m_after(after)
{
    const ComponentDesc &c = components()[size_t(component)];
    if (const Entity *e = scene->entity(id))
        m_before = c.get(*e, property);
    m_lastEdit.start();
    setText(QStringLiteral("Change %1").arg(c.properties[size_t(property)].name));
}

void SetPropertyCommand::apply(const QVariant &value)
{
    const ComponentDesc &c = components()[size_t(m_component)];
    const int property = m_property;
    m_scene->modifyEntity(m_entity, [&](Entity &e) { c.set(e, property, value); });
}

void SetPropertyCommand::redo()
{
    apply(m_after);
}

void SetPropertyCommand::undo()
{
    apply(m_before);
}

bool SetPropertyCommand::mergeWith(const QUndoCommand *other)
{
    const auto *o = static_cast<const SetPropertyCommand *>(other);
    if (o->m_entity != m_entity || o->m_component != m_component || o->m_property != m_property
        || m_lastEdit.elapsed() > kMergeWindowMs)
        return false;
    m_after = o->m_after;
    m_lastEdit.restart();
    setObsolete(m_after == m_before);
    return true;
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

SetTransformCommand::SetTransformCommand(Scene *scene, EntityId id, const Transform &before, const Transform &after,
                                         TransformField field)
    : m_scene(scene)
    , m_entity(id)
    , m_before(before)
    , m_after(after)
    , m_field(field)
    , m_mergeable(false)
{
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
    if (!m_mergeable || !o->m_mergeable || o->m_entity != m_entity || o->m_field != m_field
        || m_lastEdit.elapsed() > kMergeWindowMs)
        return false;
    m_after = o->m_after;
    m_lastEdit.restart();
    setObsolete(m_after == m_before); // dragged back to where it started: no undo step
    return true;
}

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------

AddMaterialCommand::AddMaterialCommand(Scene *scene, const Material &prototype, EntityId assignTo, int slot)
    : m_scene(scene)
    , m_material(prototype)
    , m_entity(assignTo)
    , m_slot(slot)
{
    setText(QStringLiteral("New material"));
    if (const Entity *e = scene->entity(assignTo))
        m_previous = e->material(slot);
}

void AddMaterialCommand::redo()
{
    if (m_material.id == NoMaterial)
        m_material.id = m_scene->addMaterial(m_material);
    else
        m_scene->insertMaterial(m_material);
    if (m_entity != InvalidEntity)
        m_scene->setEntityMaterial(m_entity, m_slot, m_material.id);
}

void AddMaterialCommand::undo()
{
    if (m_entity != InvalidEntity)
        m_scene->setEntityMaterial(m_entity, m_slot, m_previous);
    if (const Material *m = m_scene->material(m_material.id))
        m_material = *m; // keep edits made since, for redo
    m_scene->removeMaterial(m_material.id);
}

AssignMaterialCommand::AssignMaterialCommand(Scene *scene, EntityId entity, int slot, MaterialId material)
    : m_scene(scene)
    , m_entity(entity)
    , m_slot(slot)
    , m_before(NoMaterial)
    , m_after(material)
{
    setText(QStringLiteral("Assign material"));
    if (const Entity *e = scene->entity(entity))
        m_before = e->material(slot);
}

void AssignMaterialCommand::redo()
{
    m_scene->setEntityMaterial(m_entity, m_slot, m_after);
}

void AssignMaterialCommand::undo()
{
    m_scene->setEntityMaterial(m_entity, m_slot, m_before);
}

SetMaterialGraphCommand::SetMaterialGraphCommand(Scene *scene, MaterialId material, const NodeGraph &after,
                                                 const QString &text, const QString &mergeKey)
    : m_scene(scene)
    , m_material(material)
    , m_after(after)
    , m_mergeKey(mergeKey)
{
    setText(text);
    if (const Material *m = scene->material(material))
        m_before = m->graph;
    m_lastEdit.start();
}

void SetMaterialGraphCommand::redo()
{
    m_scene->setMaterialGraph(m_material, m_after);
}

void SetMaterialGraphCommand::undo()
{
    m_scene->setMaterialGraph(m_material, m_before);
}

bool SetMaterialGraphCommand::mergeWith(const QUndoCommand *other)
{
    const auto *o = static_cast<const SetMaterialGraphCommand *>(other);
    if (m_mergeKey.isEmpty() || o->m_mergeKey != m_mergeKey || o->m_material != m_material
        || m_lastEdit.elapsed() > kMergeWindowMs)
        return false;
    m_after = o->m_after;
    m_lastEdit.restart();
    setObsolete(m_after == m_before);
    return true;
}

SetMaterialAlphaCommand::SetMaterialAlphaCommand(Scene *scene, MaterialId material, AlphaMode mode, float cutoff)
    : m_scene(scene)
    , m_material(material)
    , m_afterMode(mode)
    , m_afterCutoff(cutoff)
{
    if (const Material *m = scene->material(material)) {
        m_beforeMode = m->alphaMode;
        m_beforeCutoff = m->alphaCutoff;
    }
    setText(mode != m_beforeMode ? QStringLiteral("Set alpha mode") : QStringLiteral("Set alpha cutoff"));
    m_lastEdit.start();
}

void SetMaterialAlphaCommand::redo()
{
    m_scene->setMaterialAlpha(m_material, m_afterMode, m_afterCutoff);
}

void SetMaterialAlphaCommand::undo()
{
    m_scene->setMaterialAlpha(m_material, m_beforeMode, m_beforeCutoff);
}

bool SetMaterialAlphaCommand::mergeWith(const QUndoCommand *other)
{
    // Only cutoff changes merge: a mode change is its own step.
    const auto *o = static_cast<const SetMaterialAlphaCommand *>(other);
    if (o->m_material != m_material || o->m_afterMode != m_afterMode || m_afterMode != m_beforeMode
        || m_lastEdit.elapsed() > kMergeWindowMs)
        return false;
    m_afterCutoff = o->m_afterCutoff;
    m_lastEdit.restart();
    setObsolete(m_afterCutoff == m_beforeCutoff);
    return true;
}

RenameMaterialCommand::RenameMaterialCommand(Scene *scene, MaterialId material, const QString &name)
    : m_scene(scene)
    , m_material(material)
    , m_after(name)
{
    setText(QStringLiteral("Rename material"));
    if (const Material *m = scene->material(material))
        m_before = m->name;
}

void RenameMaterialCommand::redo()
{
    m_scene->setMaterialName(m_material, m_after);
}

void RenameMaterialCommand::undo()
{
    m_scene->setMaterialName(m_material, m_before);
}
