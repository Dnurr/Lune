#pragma once

#include <QObject>
#include <functional>
#include <vector>

#include "entity.h"
#include "material.h"

// The scene: an ordered list of entities, and a library of materials they can use.
// It knows nothing about the editor (no selection, no undo): it just holds the data and
// notifies every change, so the UI and the renderer can follow it.
class Scene : public QObject
{
    Q_OBJECT

public:
    explicit Scene(QObject *parent = nullptr);

    // New entity with a fresh id, appended at the end (or at `index`). Returns its id.
    // The prototype's id is ignored.
    EntityId createEntity(Entity prototype, int index = -1);
    EntityId createEntity(const QString &name, const Transform &transform = {}, int index = -1);
    // Puts back an entity with its own id (e.g. undoing a delete). `index` -1 = at the end.
    void insertEntity(const Entity &entity, int index = -1);
    bool removeEntity(EntityId id);
    // Removes every entity (entityRemoved for each) and restarts the ids.
    void clear();

    const std::vector<Entity> &entities() const { return m_entities; }
    const Entity *entity(EntityId id) const;
    int indexOf(EntityId id) const; // -1 if not found

    void setName(EntityId id, const QString &name);
    void setTransform(EntityId id, const Transform &transform);
    // Generic edit: `change` modifies the entity in place, then entityChanged is emitted.
    // The id must not be changed.
    void modifyEntity(EntityId id, const std::function<void(Entity &)> &change);

    // Name not used by any entity: "Entity", "Entity 2", "Entity 3"...
    QString uniqueName(const QString &base) const;

    // Materials. Removing one doesn't touch the entities using it: they fall back to their
    // own surface values until it comes back (undo) or they're given another one.
    MaterialId addMaterial(Material prototype); // fresh id; returns it
    void insertMaterial(const Material &material); // with its own id (undo)
    bool removeMaterial(MaterialId id);
    const std::vector<Material> &materials() const { return m_materials; }
    const Material *material(MaterialId id) const;
    void setMaterialGraph(MaterialId id, const NodeGraph &graph);
    void setMaterialName(MaterialId id, const QString &name);
    void setEntityMaterial(EntityId id, MaterialId material);
    QString uniqueMaterialName(const QString &base) const;

signals:
    void entityAdded(EntityId id, int index);
    void entityRemoved(EntityId id);
    void entityChanged(EntityId id); // name or transform
    void materialAdded(MaterialId id);
    void materialRemoved(MaterialId id);
    void materialChanged(MaterialId id); // name or graph

private:
    Entity *find(EntityId id);

    std::vector<Entity> m_entities;
    EntityId m_nextId = 1;
    std::vector<Material> m_materials;
    MaterialId m_nextMaterialId = 1;
};
