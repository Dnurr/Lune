#pragma once

#include <QMainWindow>

#include "engine/scene/entity.h"

class AssetsPanel;
class CommandPalette;
class DisplaySettings;
class InspectorPanel;
class OutlinerPanel;
class QAction;
class QCloseEvent;
class QJsonObject;
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

    // Opens a scene file (no "save changes?" question). Used by File > Open.
    bool loadScene(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    // Tab with the mouse over the viewport (not over a panel) collapses / expands the panels.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void createMenus();
    void createPanels();

    // Editor layout (window geometry, panels, assets browser), saved in the user settings
    // when the editor closes and restored when it starts. Not part of the scene.
    void saveLayout() const;
    void restoreLayout();
    void setPanelsCollapsed(bool collapsed);
    void openAsset(const QString &path); // double-click in the assets browser
    // Files dropped on the viewport: meshes land where they're dropped, an .hdr becomes
    // the environment, a scene opens.
    void dropFiles(const QStringList &paths, const QPointF &pos);
    void useEnvironmentImage(const QString &path);
    void followScene(const QString &path); // assets root: the scene's folder, unless already inside the root

    void addEntity(const QString &name, const MeshPtr &mesh, const QVector3D &position = QVector3D());
    void addLight(Light::Type type);
    static Entity lightPrototype(Light::Type type); // name, transform and light of a new light
    void importMeshFile();
    void deleteSelected();

    // Scene files
    void createDefaultScene();
    void resetDocument(const QString &path);
    void newScene();
    void openScene();
    bool saveScene();
    bool saveSceneAs();
    bool writeScene(const QString &path);
    bool maybeSave(); // asks to save unsaved changes; false = the user cancelled
    QString documentName() const;
    QString lastDirectory() const;
    void updateTitle();
    QJsonObject editorState() const; // camera etc., stored in the scene file
    void applyEditorState(const QJsonObject &state);

    Scene *m_scene = nullptr;
    Selection *m_selection = nullptr;
    QUndoStack *m_undo = nullptr;

    Viewport *m_viewport = nullptr; // owned by Qt (central widget)
    CommandPalette *m_palette = nullptr;
    DisplaySettings *m_display = nullptr; // exposure + tonemapping of the viewport
    QMenu *m_addMenu = nullptr;
    QAction *m_collapsePanels = nullptr;
    OutlinerPanel *m_outliner = nullptr;
    InspectorPanel *m_inspector = nullptr;
    AssetsPanel *m_assets = nullptr;
    QString m_scenePath; // empty = never saved
};
