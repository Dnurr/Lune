#pragma once

#include <QHash>
#include <QList>
#include <QJsonObject>
#include <QMainWindow>

#include "engine/scene/entity.h"
#include "frontend/core/rendersettings.h"

class AssetsPanel;
class CommandPalette;
class DisplaySettings;
class InspectorPanel;
class NodesPanel;
class Panel;
class Playback;
class TimelinePanel;
class CurvesPanel;
class QToolButton;
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

    // Workspaces: named panel arrangements, switched with the tabs in the menu bar.
    //   layout   scene outliner, inspector, assets
    //   shading  node editor on the right, assets; the scene centered in the free area
    // Each one remembers its own panels (sides, sizes); see saveLayout().
    void createWorkspaceTabs();
    void switchWorkspace(const QString &name);
    QJsonObject captureWorkspace() const;          // the panels as they are now
    void applyWorkspace(const QJsonObject &state); // turn panels on/off, sides, sizes
    static QJsonObject defaultWorkspace(const QString &name);
    QHash<QString, Panel *> workspacePanels() const;
    void openAsset(const QString &path); // double-click in the assets browser
    // Files dropped on the viewport: meshes land where they're dropped, an .hdr becomes
    // the environment, a scene opens.
    void dropFiles(const QStringList &paths, const QPointF &pos);
    void useEnvironmentImage(const QString &path);
    void followScene(const QString &path); // assets root: the scene's folder, unless already inside the root

    void addEntity(const QString &name, const MeshPtr &mesh, const QVector3D &position = QVector3D());
    // Imports a mesh file as a new entity, with its materials (glTF) added to the scene and
    // put in its slots: one undo step. `restOnGround`: its lowest point at `position`.
    // False and `error` set if the file can't be loaded.
    bool addModel(const QString &path, QString *error, const QVector3D &position = QVector3D(), bool restOnGround = false);
    void addLight(Light::Type type);
    // A camera where the viewport looks from (with a Post Process, every effect off).
    void addCamera();
    // Moves the scene camera to the viewport's view (one undo step); adds one if there's none.
    void alignCameraToView();
    // The viewport's passepartout and camera icons follow the render size.
    void updateRenderAspect();
    static Entity lightPrototype(Light::Type type); // name, transform and light of a new light
    void importMeshFile();
    void deleteSelected();

    // Render menu: the final image (F4), or every pass of the render settings (F6), saved
    // as PNG files and shown in a result window.
    void renderImages(bool allPasses);
    // Render Video (F5): every frame of the timeline from the scene camera, as PNG files in
    // "<scene>_video" in the render folder, then an MP4 next to them when ffmpeg is installed.
    void renderVideo();
    void editRenderSettings();

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
    QList<QWidget *> m_viewportMenus; // Display and camera menus: hidden with the panels (Tab)
    OutlinerPanel *m_outliner = nullptr;
    InspectorPanel *m_inspector = nullptr;
    AssetsPanel *m_assets = nullptr;
    NodesPanel *m_nodes = nullptr;
    Playback *m_playback = nullptr;      // the frame shown, playing
    TimelinePanel *m_timeline = nullptr; // footer: playback and frame range
    CurvesPanel *m_curves = nullptr;     // bottom, a tab with Assets: the curve editor
    QString m_workspace = QStringLiteral("layout");
    QHash<QString, QJsonObject> m_workspaces; // saved state of the workspaces not shown
    QHash<QString, QToolButton *> m_workspaceTabs;
    QString m_scenePath; // empty = never saved
    RenderSettings m_renderSettings; // saved in the scene's editor state
};
