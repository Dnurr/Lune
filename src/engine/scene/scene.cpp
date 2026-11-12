#include "scene.h"

#include <algorithm>

Scene::Scene(QObject *parent)
    : QObject(parent)
{
}

EntityId Scene::createEntity(Entity prototype, int index)
{
    prototype.id = m_nextId; // insertEntity advances m_nextId
    insertEntity(prototype, index);
    return prototype.id;
}

EntityId Scene::createEntity(const QString &name, const Transform &transform, int index)
{
    Entity e;
    e.name = name;
    e.transform = transform;
    return createEntity(e, index);
}

void Scene::insertEntity(const Entity &entity, int index)
{
    Q_ASSERT(entity.id != InvalidEntity && !this->entity(entity.id));
    if (index < 0 || index > int(m_entities.size()))
        index = int(m_entities.size());
    m_entities.insert(m_entities.begin() + index, entity);
    m_nextId = std::max(m_nextId, entity.id + 1);
    emit entityAdded(entity.id, index);
}

bool Scene::removeEntity(EntityId id)
{
    const int i = indexOf(id);
    if (i < 0)
        return false;
    m_entities.erase(m_entities.begin() + i);
    emit entityRemoved(id);
    return true;
}

const Entity *Scene::entity(EntityId id) const
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : &m_entities[i];
}

Entity *Scene::find(EntityId id)
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : &m_entities[i];
}

int Scene::indexOf(EntityId id) const
{
    for (int i = 0; i < int(m_entities.size()); ++i) {
        if (m_entities[i].id == id)
            return i;
    }
    return -1;
}

void Scene::setName(EntityId id, const QString &name)
{
    Entity *e = find(id);
    if (!e || e->name == name)
        return;
    e->name = name;
    emit entityChanged(id);
}

void Scene::setTransform(EntityId id, const Transform &transform)
{
    Entity *e = find(id);
    if (!e || e->transform == transform)
        return;
    e->transform = transform;
    emit entityChanged(id);
}

void Scene::modifyEntity(EntityId id, const std::function<void(Entity &)> &change)
{
    Entity *e = find(id);
    if (!e)
        return;
    change(*e);
    Q_ASSERT(e->id == id);
    emit entityChanged(id);
}

QString Scene::uniqueName(const QString &base) const
{
    auto taken = [this](const QString &n) {
        return std::any_of(m_entities.begin(), m_entities.end(), [&](const Entity &e) { return e.name == n; });
    };
    if (!taken(base))
        return base;
    for (int i = 2;; ++i) {
        const QString n = QStringLiteral("%1 %2").arg(base).arg(i);
        if (!taken(n))
            return n;
    }
}
