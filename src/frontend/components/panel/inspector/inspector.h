#pragma once

#include <QSet>
#include <QString>
#include <functional>
#include <vector>

#include "../panel.h"
#include "engine/scene/entity.h"

class QUndoStack;
class QVariant;
class Scene;
class Selection;

// Inspector built automatically from the component descriptors (engine/scene/components.h):
// one nested section per component the selected entity has, one row per property,
// with an editor chosen by the property type (Vector3, number, color, checkbox, text), and
// a small heading over each group of rows. Optional components get a Remove button, and
// Add Component lists the ones the entity can take. Edits become undoable commands
// (SetPropertyCommand, SetComponentCommand). Hidden while nothing is selected.
class InspectorPanel : public Panel
{
    Q_OBJECT

public:
    InspectorPanel(Scene *scene, Selection *selection, QUndoStack *undoStack, QWidget *parent = nullptr);

private:
    struct Row
    {
        int component;
        int property;
        std::function<void(const QVariant &)> show; // put a value in the editor
    };

    void rebuild();  // selection (or the entity's set of components) changed
    void refresh();  // values changed
    void edit(int component, int property, const QVariant &value);
    std::vector<bool> presentComponents(const Entity &e) const;

    Scene *m_scene;
    Selection *m_selection;
    QUndoStack *m_undo;

    EntityId m_shown = InvalidEntity;
    std::vector<bool> m_shownComponents;
    std::vector<Panel *> m_sections;
    class QPushButton *m_addButton = nullptr; // Add Component (when the entity can take one)
    QSet<QString> m_openGroups; // "Component/group" of the property groups shown open
    std::vector<Row> m_rows;
    bool m_updating = false; // true while refresh() writes into the editors
};
