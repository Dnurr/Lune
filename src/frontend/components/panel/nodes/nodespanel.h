#pragma once

#include "../panel.h"
#include "engine/scene/entity.h"

class NodeEditorView;
class QComboBox;
class QLabel;
class QSplitter;
class QToolButton;
class QUndoStack;
class QVBoxLayout;
class Scene;
class Selection;
class Viewport;

// Node editor for materials (the Shading workspace):
//   header      the material shown: the selected entity's, chosen from the scene's library
//               (choosing one assigns it to the selected entity), + for a new one, rename
//   canvas      NodeEditorView on the material's graph
//   properties  values of the selected node's unlinked inputs
//   status      why the material can't be drawn (graph or shader error), from the viewport
// Every change goes through the undo stack (material commands).
class NodesPanel : public Panel
{
    Q_OBJECT

public:
    NodesPanel(Scene *scene, Selection *selection, QUndoStack *undoStack, Viewport *viewport, Side side = Side::Right);

    MaterialId shownMaterial() const { return m_material; }

private:
    void syncFromSelection();      // the selected entity's material, if it has one
    void showMaterial(MaterialId id);
    void refreshMaterials();       // combo contents
    void refreshGraph();
    void refreshStatus();
    void rebuildProperties();
    void newMaterial();
    void renameMaterial();
    void chooseMaterial(int comboIndex);
    void editValue(int node, const QString &port, const QVariant &value);

    Scene *m_scene;
    Selection *m_selection;
    QUndoStack *m_undo;
    Viewport *m_viewport;
    MaterialId m_material = NoMaterial;
    bool m_syncing = false;

    QComboBox *m_combo = nullptr;
    QToolButton *m_new = nullptr;
    QToolButton *m_rename = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_hint = nullptr;
    QSplitter *m_splitter = nullptr;
    NodeEditorView *m_editor = nullptr;
    QWidget *m_properties = nullptr;
    QVBoxLayout *m_propertiesLayout = nullptr;
    int m_propertiesNode = 0;         // node whose inputs the properties show
    QString m_propertiesSignature;    // its unlinked inputs: rebuilt only when they change
};
