#pragma once

#include <QMainWindow>

#include "engine/scene/mesh.h"

class CommandPalette;
class QMenu;
class QUndoStack;
class Scene;
class Selection;
class Viewport;

// Main window. Owns the editor state (scene, selection, undo history) and connects
// the panels to it:
//   - panels show the scene / selection and emit what the user asked for
//   - the editor turns those requests into commands on the undo stack
//   - commands change the Scene, whose signals update the panels again
class Editor : public QMainWindow
{
    Q_OBJECT

public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    Viewport *viewport() const { return m_viewport; }
    Scene *scene() const { return m_scene; }
    Selection *selection() const { return m_selection; }
    QUndoStack *undoStack() const { return m_undo; }

private:
    void createMenus();
    void createPanels();

    void addEntity(const QString &name, const MeshPtr &mesh);
    void importObj();
    void deleteSelected();

    Scene *m_scene = nullptr;
    Selection *m_selection = nullptr;
    QUndoStack *m_undo = nullptr;

    Viewport *m_viewport = nullptr; // owned by Qt (central widget)
    CommandPalette *m_palette = nullptr;
    QMenu *m_addMenu = nullptr;
};
