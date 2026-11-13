#include "editor.h"

#include <QAction>
#include <QActionGroup>
#include <QCursor>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QBoxLayout>
#include <QCloseEvent>
#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenuBar>
#include <QUndoStack>

#include "components/palette/commandpalette.h"
#include "components/panel/inspector/inspector.h"
#include "components/panel/outliner/outliner.h"
#include "components/viewport/displaymenu.h"
#include "components/viewport/viewport.h"
#include "core/commands.h"
#include "core/displayhistory.h"
#include "core/displaysettings.h"
#include "core/selection.h"
#include "engine/io/meshimport.h"
#include "engine/io/sceneio.h"
#include "engine/scene/primitives.h"
#include "engine/scene/scene.h"

Editor::Editor(QWidget *parent)
    : QMainWindow(parent)
    , m_scene(new Scene(this))
    , m_selection(new Selection(this))
    , m_undo(new QUndoStack(this))
    , m_viewport(new Viewport(this))
    , m_display(new DisplaySettings(this))
{
    resize(1280, 720);

    // m_viewport->setApi(QRhiWidget::Api::Vulkan); // optional: pick a backend (default: Direct3D 11 on Windows)
    setCentralWidget(m_viewport);

    m_viewport->setEditorContext(m_scene, m_selection, m_undo);
    m_viewport->setDisplaySettings(m_display);
    // Display changes go on the undo stack too (and so mark the scene as modified).
    new DisplayHistory(m_display, m_undo, this);
    createDefaultScene();

    createMenus();
    createPanels();

    // A deleted entity can't stay selected (commands handle it; this covers any other removal).
    connect(m_scene, &Scene::entityRemoved, this, [this](EntityId id) {
        if (m_selection->current() == id)
            m_selection->clear();
    });

    // "Modified" = the undo history moved away from the last save.
    connect(m_undo, &QUndoStack::cleanChanged, this, &Editor::updateTitle);
    updateTitle();
}

Editor::~Editor() = default;

void Editor::createMenus()
{
    // File
    auto *newFile = new QAction(QIcon::fromTheme("document-new"), "New", this);
    newFile->setShortcut(QKeySequence::New);
    connect(newFile, &QAction::triggered, this, &Editor::newScene);
    auto *openFile = new QAction(QIcon::fromTheme("document-open"), "Open...", this);
    openFile->setShortcut(QKeySequence::Open);
    connect(openFile, &QAction::triggered, this, &Editor::openScene);
    auto *saveFile = new QAction(QIcon::fromTheme("document-save"), "Save", this);
    saveFile->setShortcut(QKeySequence::Save);
    connect(saveFile, &QAction::triggered, this, &Editor::saveScene);
    auto *saveAs = new QAction(QIcon::fromTheme("document-save-as"), "Save As...", this);
    saveAs->setShortcut(QKeySequence::SaveAs);
    connect(saveAs, &QAction::triggered, this, &Editor::saveSceneAs);
    auto *importAction = new QAction("Import Mesh...", this);
    importAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    connect(importAction, &QAction::triggered, this, &Editor::importMeshFile);
    QMenu *file = menuBar()->addMenu("File");
    file->addActions({ newFile, openFile, saveFile, saveAs });
    file->addSeparator();
    file->addAction(importAction);

    // Edit: undo/redo text follows the history ("Undo Change position", ...)
    QAction *undo = m_undo->createUndoAction(this, "Undo");
    undo->setShortcut(QKeySequence::Undo);
    QAction *redo = m_undo->createRedoAction(this, "Redo");
    redo->setShortcuts({ QKeySequence(QKeySequence::Redo), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z) });


    auto *del = new QAction("Delete", this);
    del->setShortcut(QKeySequence::Delete);
    connect(del, &QAction::triggered, this, &Editor::deleteSelected);
    connect(m_selection, &Selection::changed, del, [del](EntityId id) { del->setEnabled(id != InvalidEntity); });
    del->setEnabled(!m_selection->isEmpty());

    // Gizmo tools (W / E / R)
    auto *tools = new QActionGroup(this);
    auto tool = [&](const QString &text, Qt::Key key, Gizmo::Mode mode) {
        auto *a = new QAction(text, tools);
        a->setShortcut(QKeySequence(key));
        a->setCheckable(true);
        a->setChecked(m_viewport->gizmoMode() == mode);
        connect(a, &QAction::triggered, this, [this, mode] { m_viewport->setGizmoMode(mode); });
        return a;
    };
    QAction *move = tool("Move", Qt::Key_W, Gizmo::Mode::Translate);
    QAction *rotate = tool("Rotate", Qt::Key_E, Gizmo::Mode::Rotate);
    QAction *scale = tool("Scale", Qt::Key_R, Gizmo::Mode::Scale);

    QMenu *edit = menuBar()->addMenu("Edit");
    edit->addActions({ undo, redo });
    edit->addSeparator();
    edit->addActions({ move, rotate, scale });
    edit->addSeparator();
    edit->addAction(del);
    edit->addSeparator();

    // Command palette: listed in Edit for discoverability, but not inside the palette itself.
    auto *palette = new QAction("Command Palette", this);
    palette->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_K));
    palette->setProperty("paletteHidden", true);
    connect(palette, &QAction::triggered, this, [this] { m_palette->open(); });
    edit->addAction(palette);

    // View
    auto *stats = new QAction("Stats Overlay", this);
    stats->setShortcut(QKeySequence(Qt::Key_F12));
    stats->setCheckable(true);
    connect(stats, &QAction::toggled, m_viewport, &Viewport::setStatsVisible);
    QMenu *view = menuBar()->addMenu("View");
    view->addAction(stats);
    view->addSeparator();

    // Display: tonemapping operator (one checked at a time) and exposure reset.
    // Same settings as the Display menu in the viewport corner; both stay in sync.
    auto *tonemappers = new QActionGroup(this);
    const QStringList names = DisplaySettings::tonemapperNames();
    for (int i = 0; i < names.size(); ++i) {
        auto *a = new QAction(QStringLiteral("Tonemap: %1").arg(names[i]), tonemappers);
        a->setCheckable(true);
        a->setChecked(m_display->tonemapper() == i);
        connect(a, &QAction::triggered, this, [this, i] { m_display->setTonemapper(i); });
        connect(m_display, &DisplaySettings::changed, a, [this, a, i] { a->setChecked(m_display->tonemapper() == i); });
        view->addAction(a);
    }
    auto *resetExposure = new QAction("Reset Exposure", this);
    connect(resetExposure, &QAction::triggered, this, [this] { m_display->setExposure(0.0); });
    view->addAction(resetExposure);

    // Add: also opens at the mouse cursor with Shift+A
    m_addMenu = menuBar()->addMenu("Add");
    m_addMenu->addAction("Empty", this, [this] { addEntity(QStringLiteral("Empty"), nullptr); });
    m_addMenu->addSeparator();
    m_addMenu->addAction("Cube", this, [this] { addEntity(QStringLiteral("Cube"), Primitives::cube()); });
    m_addMenu->addAction("Sphere", this, [this] { addEntity(QStringLiteral("Sphere"), Primitives::sphere()); });
    m_addMenu->addAction("Plane", this, [this] { addEntity(QStringLiteral("Plane"), Primitives::plane()); });
    m_addMenu->addSeparator();
    m_addMenu->addAction("Directional Light", this, [this] { addLight(Light::Directional); });
    m_addMenu->addAction("Point Light", this, [this] { addLight(Light::Point); });
    m_addMenu->addSeparator();
    m_addMenu->addAction(importAction);

    auto *addAtCursor = new QAction(this);
    addAtCursor->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_A));
    connect(addAtCursor, &QAction::triggered, this, [this] { m_addMenu->popup(QCursor::pos()); });
    addAction(addAtCursor); // window-wide shortcut, not shown in any menu
}

void Editor::createPanels()
{
    // Scene outliner (left)
    auto *outliner = new OutlinerPanel(m_scene, m_selection, m_viewport);
    outliner->setMargin(12);
    outliner->setPanelWidth(220);
    connect(outliner, &OutlinerPanel::renameRequested, this, [this](EntityId id, const QString &name) {
        m_undo->push(new RenameEntityCommand(m_scene, id, name));
    });

    // Inspector (right): built from the component descriptors, hidden while nothing is selected.
    auto *inspector = new InspectorPanel(m_scene, m_selection, m_undo, m_viewport);
    inspector->setMargin(12);
    inspector->setPanelWidth(280);

    // Assets (bottom)
    auto *assets = new Panel("Assets", m_viewport, Panel::Side::Bottom);
    assets->setMargin(12);
    assets->setPanelHeight(200);

    // Created last so it sits above the panels.
    // Display dropdown in the top-right corner of the free viewport area.
    new DisplayMenu(m_display, m_viewport);

    m_palette = new CommandPalette(this, m_viewport);
}

void Editor::addEntity(const QString &name, const MeshPtr &mesh)
{
    Entity e;
    e.name = m_scene->uniqueName(name);
    e.mesh = mesh;
    m_undo->push(new AddEntityCommand(m_scene, m_selection, e));
}

Entity Editor::lightPrototype(Light::Type type)
{
    Entity e;
    e.light = Light {};
    e.light->type = type;
    if (type == Light::Directional) {
        e.name = QStringLiteral("Sun");
        // Above the scene, tilted like the old built-in key light (only the rotation matters).
        e.transform.position = QVector3D(0.0f, 3.0f, 0.0f);
        e.transform.rotation = QVector3D(21.0f, 0.0f, -29.0f);
    } else {
        e.name = QStringLiteral("Point Light");
        e.transform.position = QVector3D(0.0f, 2.0f, 0.0f);
        e.light->intensity = 10.0f; // falls off with distance squared: 2.5 at the grid
    }
    return e;
}

void Editor::addLight(Light::Type type)
{
    Entity e = lightPrototype(type);
    e.name = m_scene->uniqueName(e.name);
    m_undo->push(new AddEntityCommand(m_scene, m_selection, e));
}

void Editor::importMeshFile()
{
    const QString path = QFileDialog::getOpenFileName(this, "Import Mesh", lastDirectory(), meshFileFilter());
    if (path.isEmpty())
        return;
    QString error;
    const MeshPtr mesh = importMesh(path, &error);
    if (!mesh) {
        QMessageBox::warning(this, "Import Mesh", error);
        return;
    }
    addEntity(mesh->name.isEmpty() ? QFileInfo(path).completeBaseName() : mesh->name, mesh);
}

// ---------------------------------------------------------------------------
// Scene files
// ---------------------------------------------------------------------------

void Editor::createDefaultScene()
{
    // Created directly (not a command), so it isn't in the undo history.
    Entity cube;
    cube.name = QStringLiteral("Cube");
    cube.transform.position = QVector3D(0.0f, 0.5f, 0.0f); // resting on the grid
    cube.mesh = Primitives::cube();
    m_selection->select(m_scene->createEntity(cube));
    m_scene->createEntity(lightPrototype(Light::Directional));
}

void Editor::resetDocument(const QString &path)
{
    // The history refers to the previous scene: it can't be undone into the new one.
    m_undo->clear();
    m_undo->setClean();
    m_scenePath = path;
    updateTitle();
}

void Editor::newScene()
{
    if (!maybeSave())
        return;
    m_selection->clear();
    m_scene->clear();
    createDefaultScene();
    m_viewport->setCamera(OrbitCamera {});
    m_display->reset();
    resetDocument(QString());
}

void Editor::openScene()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, "Open Scene", lastDirectory(), SceneIO::FileFilter);
    if (!path.isEmpty())
        loadScene(path);
}

bool Editor::loadScene(const QString &path)
{
    QJsonObject editorState;
    QStringList warnings;
    QString error;
    m_selection->clear();
    if (!SceneIO::load(*m_scene, path, &editorState, &warnings, &error)) {
        QMessageBox::warning(this, "Open Scene", error);
        return false;
    }
    applyEditorState(editorState);
    resetDocument(path);
    if (!warnings.isEmpty())
        QMessageBox::warning(this, "Open Scene", "The scene was opened with some problems:\n\n" + warnings.join('\n'));
    return true;
}

bool Editor::saveScene()
{
    if (m_scenePath.isEmpty())
        return saveSceneAs();
    return writeScene(m_scenePath);
}

bool Editor::saveSceneAs()
{
    const QString suggested = m_scenePath.isEmpty() ? QStringLiteral("untitled.lune") : m_scenePath;
    QString path = QFileDialog::getSaveFileName(this, "Save Scene", suggested, SceneIO::FileFilter);
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += ".lune";
    return writeScene(path);
}

bool Editor::writeScene(const QString &path)
{
    QString error;
    if (!SceneIO::save(*m_scene, path, editorState(), &error)) {
        QMessageBox::warning(this, "Save Scene", error);
        return false;
    }
    m_scenePath = path;
    m_undo->setClean();
    updateTitle();
    return true;
}

bool Editor::maybeSave()
{
    if (m_undo->isClean())
        return true;
    const auto answer = QMessageBox::question(
        this, "Lune", QStringLiteral("Save changes to %1?").arg(documentName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return saveScene();
    return answer == QMessageBox::Discard;
}

QString Editor::documentName() const
{
    return m_scenePath.isEmpty() ? QStringLiteral("Untitled") : QFileInfo(m_scenePath).fileName();
}

QString Editor::lastDirectory() const
{
    return m_scenePath.isEmpty() ? QString() : QFileInfo(m_scenePath).absolutePath();
}

void Editor::updateTitle()
{
    // [*] is replaced by "*" while the window is marked as modified.
    setWindowTitle(documentName() + QStringLiteral("[*] - Lune"));
    setWindowModified(!m_undo->isClean());
}

QJsonObject Editor::editorState() const
{
    const OrbitCamera &c = m_viewport->camera();
    QJsonObject camera;
    camera["target"] = QJsonArray { c.target.x(), c.target.y(), c.target.z() };
    camera["yaw"] = c.yaw;
    camera["pitch"] = c.pitch;
    camera["distance"] = c.distance;
    QJsonObject display;
    display["exposure"] = m_display->exposure();
    display["tonemapper"] = DisplaySettings::tonemapperIds().value(m_display->tonemapper());
    QJsonObject state;
    state["camera"] = camera;
    state["display"] = display;
    return state;
}

void Editor::applyEditorState(const QJsonObject &state)
{
    OrbitCamera c;
    const QJsonObject camera = state["camera"].toObject();
    const QJsonArray t = camera["target"].toArray();
    if (t.size() == 3)
        c.target = QVector3D(float(t[0].toDouble()), float(t[1].toDouble()), float(t[2].toDouble()));
    c.yaw = float(camera["yaw"].toDouble(c.yaw));
    c.pitch = qBound(-89.0f, float(camera["pitch"].toDouble(c.pitch)), 89.0f);
    c.distance = qBound(0.5f, float(camera["distance"].toDouble(c.distance)), 500.0f);
    m_viewport->setCamera(c);

    // Missing or unknown values (older files, operators removed later): defaults.
    const QJsonObject display = state["display"].toObject();
    m_display->setExposure(display["exposure"].toDouble(0.0));
    const int tonemapper = DisplaySettings::tonemapperIds().indexOf(display["tonemapper"].toString());
    m_display->setTonemapper(tonemapper >= 0 ? tonemapper : DisplaySettings::None);
}

void Editor::closeEvent(QCloseEvent *event)
{
    if (maybeSave())
        event->accept();
    else
        event->ignore();
}

void Editor::deleteSelected()
{
    if (!m_selection->isEmpty())
        m_undo->push(new DeleteEntityCommand(m_scene, m_selection, m_selection->current()));
}
