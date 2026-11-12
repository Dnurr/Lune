#pragma once

#include "../panel.h"
#include "engine/scene/entity.h"

class QListWidget;
class QListWidgetItem;
class Scene;
class Selection;

// List of the scene's entities. Click to select, double-click (or F2) to rename.
// It follows the Scene and the Selection; renames go out as a signal so the editor
// can make them undoable.
class OutlinerPanel : public Panel
{
    Q_OBJECT

public:
    OutlinerPanel(Scene *scene, Selection *selection, QWidget *parent = nullptr);

signals:
    void renameRequested(EntityId id, const QString &name);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QListWidgetItem *itemFor(EntityId id) const;
    QListWidgetItem *makeItem(const Entity &entity) const;
    void syncSelection();

    Scene *m_scene;
    Selection *m_selection;
    QListWidget *m_list = nullptr;
    bool m_syncing = false;  // true while the list is updated from the model
    bool m_focusing = false; // true while the list handles a FocusIn
};
