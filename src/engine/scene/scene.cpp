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

void Scene::clear()
{
    while (!m_entities.empty())
        removeEntity(m_entities.back().id);
    m_nextId = 1;
    while (!m_materials.empty())
        removeMaterial(m_materials.back().id);
    m_nextMaterialId = 1;
}

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------

NodeGraph Material::defaultGraph()
{
    NodeGraph g;
    g.addNode(QStringLiteral("output"), QPointF(0, 0));
    return g;
}

QString Material::alphaModeName(AlphaMode mode)
{
    switch (mode) {
    case AlphaMode::Clip: return QStringLiteral("clip");
    case AlphaMode::Blend: return QStringLiteral("blend");
    default: return QStringLiteral("opaque");
    }
}

AlphaMode Material::alphaModeFromName(const QString &name)
{
    if (name == QLatin1String("clip"))
        return AlphaMode::Clip;
    if (name == QLatin1String("blend"))
        return AlphaMode::Blend;
    return AlphaMode::Opaque;
}

MaterialId Scene::addMaterial(Material prototype)
{
    prototype.id = m_nextMaterialId;
    insertMaterial(prototype);
    return prototype.id;
}

void Scene::insertMaterial(const Material &material)
{
    Q_ASSERT(material.id != NoMaterial && !this->material(material.id));
    m_materials.push_back(material);
    m_nextMaterialId = std::max(m_nextMaterialId, material.id + 1);
    emit materialAdded(material.id);
}

bool Scene::removeMaterial(MaterialId id)
{
    const auto it = std::find_if(m_materials.begin(), m_materials.end(), [id](const Material &m) { return m.id == id; });
    if (it == m_materials.end())
        return false;
    m_materials.erase(it);
    emit materialRemoved(id);
    return true;
}

const Material *Scene::material(MaterialId id) const
{
    for (const Material &m : m_materials)
        if (m.id == id)
            return &m;
    return nullptr;
}

void Scene::setMaterialGraph(MaterialId id, const NodeGraph &graph)
{
    for (Material &m : m_materials) {
        if (m.id == id && !(m.graph == graph)) {
            m.graph = graph;
            emit materialChanged(id);
        }
    }
}

void Scene::setMaterialAlpha(MaterialId id, AlphaMode mode, float cutoff)
{
    for (Material &m : m_materials) {
        if (m.id == id && (m.alphaMode != mode || m.alphaCutoff != cutoff)) {
            m.alphaMode = mode;
            m.alphaCutoff = cutoff;
            emit materialChanged(id);
        }
    }
}

void Scene::setMaterialName(MaterialId id, const QString &name)
{
    for (Material &m : m_materials) {
        if (m.id == id && m.name != name) {
            m.name = name;
            emit materialChanged(id);
        }
    }
}

void Scene::setEntityMaterial(EntityId id, int slot, MaterialId material)
{
    Entity *e = find(id);
    if (!e || e->material(slot) == material)
        return;
    e->setMaterial(slot, material);
    emit entityChanged(id);
}

QString Scene::uniqueMaterialName(const QString &base) const
{
    auto used = [this](const QString &name) {
        return std::any_of(m_materials.begin(), m_materials.end(), [&](const Material &m) { return m.name == name; });
    };
    if (!used(base))
        return base;
    for (int i = 2;; ++i) {
        const QString name = QStringLiteral("%1 %2").arg(base).arg(i);
        if (!used(name))
            return name;
    }
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

const Entity *Scene::sceneCamera() const
{
    for (const Entity &e : m_entities) {
        if (e.camera && e.camera->enabled)
            return &e;
    }
    return nullptr;
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
