#pragma once

#include "../panel.h"
#include "engine/scene/entity.h"

class NodeEditorView;
class QComboBox;
class QDoubleSpinBox;
class QPointF;
struct TextureValue;
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
//               (choosing one assigns it to the selected entity), + for a new one, rename;
//               a slot chooser when the entity's mesh has several submeshes (materials);
//               the material's alpha mode (opaque, clip with its cutoff, blend)
//   canvas      NodeEditorView on the material's graph
//   properties  values of the selected node's unlinked inputs (images: file, color/data)
//   status      why the material can't be drawn (graph or shader error, images that can't
//               be loaded), from the viewport
// Image files dropped on the canvas become Image Texture nodes (or the image of the node
// they're dropped on).
// Every change goes through the undo stack (material commands).
class NodesPanel : public Panel
{
    Q_OBJECT

public:
    NodesPanel(Scene *scene, Selection *selection, QUndoStack *undoStack, Viewport *viewport, Side side = Side::Right);

    MaterialId shownMaterial() const { return m_material; }

private:
    void syncFromSelection();      // the selected entity's material (in the current slot), if any
    void refreshSlots();           // slot combo of the selected entity
    void showMaterial(MaterialId id);
    void refreshMaterials();       // combo contents
    void refreshGraph();
    void refreshAlpha();
    void refreshStatus();
    void rebuildProperties();
    void newMaterial();
    void renameMaterial();
    void chooseMaterial(int comboIndex);
    void editValue(int node, const QString &port, const QVariant &value);
    void editTexture(int node, const QString &port, const TextureValue &value);
    void chooseImage(int node, const QString &port);
    void dropImages(const QStringList &paths, const QPointF &scenePos, int nodeId);

    Scene *m_scene;
    Selection *m_selection;
    QUndoStack *m_undo;
    Viewport *m_viewport;
    MaterialId m_material = NoMaterial;
    bool m_syncing = false;
    EntityId m_slotEntity = InvalidEntity; // entity m_slot refers to
    int m_slot = 0;                        // its material slot shown

    QComboBox *m_combo = nullptr;
    QComboBox *m_slotCombo = nullptr;
    QWidget *m_alphaRow = nullptr;
    QComboBox *m_alphaMode = nullptr;
    QWidget *m_cutoffLabel = nullptr;
    QDoubleSpinBox *m_cutoff = nullptr;
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
