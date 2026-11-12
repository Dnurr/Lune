#pragma once

#include <QObject>
#include <functional>
#include <vector>

#include "entity.h"

// The scene: an ordered list of entities.
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

signals:
    void entityAdded(EntityId id, int index);
    void entityRemoved(EntityId id);
    void entityChanged(EntityId id); // name or transform

private:
    Entity *find(EntityId id);

    std::vector<Entity> m_entities;
    EntityId m_nextId = 1;
};
