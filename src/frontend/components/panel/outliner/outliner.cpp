#include "outliner.h"

#include <QBoxLayout>
#include <QEvent>
#include <QListWidget>
#include <QTimer>

#include "engine/scene/scene.h"
#include "frontend/core/selection.h"

namespace
{
constexpr int kIdRole = Qt::UserRole;

EntityId idOf(const QListWidgetItem *item)
{
    return item ? item->data(kIdRole).value<EntityId>() : InvalidEntity;
}
} // namespace

OutlinerPanel::OutlinerPanel(Scene *scene, Selection *selection, QWidget *parent)
    : Panel("Scene", parent, Panel::Side::Left)
    , m_scene(scene)
    , m_selection(selection)
{
    m_list = new QListWidget(this);
    m_list->setObjectName("Outliner");
    m_list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contentLayout()->addWidget(m_list);

    for (const Entity &e : m_scene->entities())
        m_list->addItem(makeItem(e));
    syncSelection();

    // Model -> list
    connect(m_scene, &Scene::entityAdded, this, [this](EntityId id, int index) {
        if (const Entity *e = m_scene->entity(id)) {
            m_syncing = true;
            m_list->insertItem(index, makeItem(*e));
            m_syncing = false;
            syncSelection();
        }
    });
    connect(m_scene, &Scene::entityRemoved, this, [this](EntityId id) {
        m_syncing = true;
        delete itemFor(id);
        m_syncing = false;
    });
    connect(m_scene, &Scene::entityChanged, this, [this](EntityId id) {
        QListWidgetItem *item = itemFor(id);
        const Entity *e = m_scene->entity(id);
        if (item && e && item->text() != e->name) {
            m_syncing = true;
            item->setText(e->name);
            m_syncing = false;
        }
    });
    connect(m_selection, &Selection::changed, this, &OutlinerPanel::syncSelection);

    // List -> editor
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current) {
        if (m_syncing)
            return;
        if (m_focusing) {
            syncSelection(); // Qt picked an item on its own: show the real selection instead
            return;
        }
        m_selection->select(idOf(current));
    });
    // When a list view gets the focus with no current item (e.g. back from a dialog),
    // Qt makes the first item current. That's not the user selecting it: ignore it.
    m_list->installEventFilter(this);
    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        if (m_syncing)
            return;
        const EntityId id = idOf(item);
        const Entity *e = m_scene->entity(id);
        const QString name = item->text().trimmed();
        if (!e)
            return;
        if (name.isEmpty() || name == e->name) {
            m_syncing = true;
            item->setText(e->name); // rejected: put the old name back
            m_syncing = false;
            return;
        }
        emit renameRequested(id, name);
    });
}

QListWidgetItem *OutlinerPanel::makeItem(const Entity &entity) const
{
    auto *item = new QListWidgetItem(entity.name);
    item->setData(kIdRole, QVariant::fromValue(entity.id));
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    return item;
}

QListWidgetItem *OutlinerPanel::itemFor(EntityId id) const
{
    for (int i = 0; i < m_list->count(); ++i) {
        if (idOf(m_list->item(i)) == id)
            return m_list->item(i);
    }
    return nullptr;
}

bool OutlinerPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_list && event->type() == QEvent::FocusIn) {
        // The list's focusInEvent runs right after this filter: flag it until it's done.
        m_focusing = true;
        QTimer::singleShot(0, this, [this] { m_focusing = false; });
    }
    return Panel::eventFilter(watched, event);
}

void OutlinerPanel::syncSelection()
{
    m_syncing = true;
    QListWidgetItem *item = itemFor(m_selection->current());
    m_list->setCurrentItem(item);
    if (!item)
        m_list->clearSelection();
    m_syncing = false;
}
