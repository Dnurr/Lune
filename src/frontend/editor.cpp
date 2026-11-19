#include "editor.h"

#include <QAction>
#include <QActionGroup>
#include <QCursor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QBoxLayout>
#include <QCloseEvent>
#include <QAbstractSpinBox>
#include <QButtonGroup>
#include <QToolButton>
#include <QApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenuBar>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QUrl>
#include <cmath>
#include <QSettings>
#include <QStandardPaths>
#include <QTextEdit>
#include <QToolTip>
#include <QUndoStack>

#include "components/palette/commandpalette.h"
#include "components/render/renderresultdialog.h"
#include "components/render/rendersettingsdialog.h"
#include "components/viewport/shadingmenu.h"
#include "engine/render/imagerender.h"
#include "engine/scene/animation.h"
#include "components/panel/assets/assets.h"
#include "components/panel/nodes/nodespanel.h"
#include "components/panel/inspector/inspector.h"
#include "components/panel/outliner/outliner.h"
#include "components/viewport/cameramenu.h"
#include "components/viewport/displaymenu.h"
#include "components/viewport/viewport.h"
#include "components/panel/curves/curves.h"
#include "components/panel/timeline/timeline.h"
#include "core/commands.h"
#include "core/playback.h"
#include "core/displayhistory.h"
#include "core/displaysettings.h"
#include "core/selection.h"
#include "engine/io/meshimport.h"
#include "engine/io/sceneio.h"
#include "engine/scene/components.h"
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

    // The frame shown: its scene time drives the renderer (animated effects, later keyframes).
    m_playback = new Playback(m_scene, this);
    auto followTime = [this] { m_viewport->setTime(m_playback->seconds()); };
    connect(m_playback, &Playback::frameChanged, this, followTime);
    connect(m_scene, &Scene::timelineChanged, this, followTime);
    // Animated properties take their values at the new frame.
    connect(m_scene, &Scene::frameChanged, this, [this] { Animation::apply(*m_scene); });

    createMenus();
    createPanels();
    createWorkspaceTabs();
    restoreLayout();
    qApp->installEventFilter(this); // Tab over the viewport, wherever the focus is

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
    // Move / rotate along the world axes or the entity's own (scale always uses its own).
    auto *localAxes = new QAction("Local Axes", this);
    localAxes->setShortcut(QKeySequence(Qt::Key_T));
    localAxes->setCheckable(true);
    localAxes->setChecked(m_viewport->gizmoSpace() == Gizmo::Space::Local);
    connect(localAxes, &QAction::toggled, this, [this](bool on) {
        m_viewport->setGizmoSpace(on ? Gizmo::Space::Local : Gizmo::Space::Global);
    });

    QMenu *edit = menuBar()->addMenu("Edit");
    edit->addActions({ undo, redo });
    edit->addSeparator();
    edit->addActions({ move, rotate, scale, localAxes });
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
    // Tab is handled in eventFilter (only with the mouse over the viewport), so it's
    // shown in the menu but not set as the action's shortcut.
    m_collapsePanels = new QAction("Hide Panels\tTab", this);
    m_collapsePanels->setCheckable(true);
    connect(m_collapsePanels, &QAction::toggled, this, &Editor::setPanelsCollapsed);
    // Camera: orbit around the target, or fly (also in the bar at the top of the viewport).
    auto *freeCamera = new QAction("Free Camera", this);
    freeCamera->setCheckable(true);
    connect(freeCamera, &QAction::toggled, this, [this](bool on) {
        m_viewport->setCameraMode(on ? CameraMode::Free : CameraMode::Orbit);
    });
    connect(m_viewport, &Viewport::cameraModeChanged, freeCamera,
            [freeCamera](CameraMode mode) { freeCamera->setChecked(mode == CameraMode::Free); });

    // Editor overlays (grid lines, light icons, gizmo, selection outline): off for clean
    // screenshots. Shift+Alt+Z, as in Blender.
    auto *overlays = new QAction("Overlays", this);
    overlays->setShortcut(QKeySequence(Qt::SHIFT | Qt::ALT | Qt::Key_Z));
    overlays->setCheckable(true);
    overlays->setChecked(true);
    connect(overlays, &QAction::toggled, m_viewport, &Viewport::setOverlaysVisible);

    // Scene camera: look through it (Numpad 0, as in Blender), or move it to the view.
    auto *cameraView = new QAction("Camera View", this);
    cameraView->setShortcut(QKeySequence(Qt::KeypadModifier | Qt::Key_0));
    cameraView->setCheckable(true);
    connect(cameraView, &QAction::triggered, this, [this, cameraView](bool on) {
        m_viewport->setCameraView(on);
        if (on && !m_viewport->cameraView()) {
            cameraView->setChecked(false);
            QToolTip::showText(m_viewport->mapToGlobal(m_viewport->rect().center()),
                               QStringLiteral("The scene has no camera: Add > Camera"), m_viewport);
        }
    });
    connect(m_viewport, &Viewport::cameraViewChanged, cameraView, &QAction::setChecked);
    auto *alignCamera = new QAction("Align Camera to View", this);
    alignCamera->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::KeypadModifier | Qt::Key_0));
    connect(alignCamera, &QAction::triggered, this, &Editor::alignCameraToView);

    QMenu *view = menuBar()->addMenu("View");
    view->addAction(m_collapsePanels);
    view->addAction(cameraView);
    view->addAction(alignCamera);
    view->addAction(freeCamera);
    view->addAction(overlays);
    view->addAction(stats);

    // Playback (Timeline panel). Text fields and spin boxes keep their keys (Space, arrows):
    // they take them before the shortcuts do.
    QMenu *playback = view->addMenu("Playback");
    auto playbackAction = [&](const char *text, QKeySequence key, auto slot) {
        auto *a = playback->addAction(QString::fromLatin1(text), this, slot);
        a->setShortcut(key);
        return a;
    };
    playbackAction("Play / Pause", QKeySequence(Qt::Key_Space), [this] { m_playback->togglePlaying(); });
    playbackAction("Next Frame", QKeySequence(Qt::Key_Right), [this] { m_playback->step(1); });
    playbackAction("Previous Frame", QKeySequence(Qt::Key_Left), [this] { m_playback->step(-1); });
    playbackAction("Go to Start", QKeySequence(Qt::SHIFT | Qt::Key_Left), [this] {
        m_playback->pause();
        m_playback->goToStart();
    });
    playbackAction("Go to End", QKeySequence(Qt::SHIFT | Qt::Key_Right), [this] {
        m_playback->pause();
        m_playback->goToEnd();
    });

    // Shading (also in the bar at the top left of the viewport); both stay in sync.
    QMenu *shading = view->addMenu("Shading");
    auto *shadingGroup = new QActionGroup(this);
    const std::pair<Shading, const char *> shadingModes[] = { { Shading::Wireframe, "Wireframe" }, { Shading::Clay, "Clay" },
                                                             { Shading::Unlit, "Unlit" }, { Shading::Preview, "Preview" },
                                                             { Shading::Rendered, "Rendered" } };
    for (const auto &[mode, name] : shadingModes) {
        auto *a = new QAction(name, shadingGroup);
        a->setCheckable(true);
        shading->addAction(a);
        connect(a, &QAction::triggered, this, [this, mode] {
            ViewSettings v = m_viewport->viewSettings();
            v.shading = mode;
            m_viewport->setViewSettings(v);
        });
        const Shading m = mode;
        auto sync = [this, a, m] { a->setChecked(m_viewport->viewSettings().shading == m); };
        connect(m_viewport, &Viewport::viewSettingsChanged, a, sync);
        sync();
    }
    shading->addSeparator();
    auto *sky = shading->addAction("Environment Background");
    sky->setCheckable(true);
    connect(sky, &QAction::triggered, this, [this](bool on) {
        ViewSettings v = m_viewport->viewSettings();
        v.skybox = on;
        m_viewport->setViewSettings(v);
    });
    auto syncSky = [this, sky] { sky->setChecked(m_viewport->viewSettings().skybox); };
    connect(m_viewport, &Viewport::viewSettingsChanged, sky, syncSky);
    syncSky();
    auto *post = shading->addAction("Post Processing");
    post->setCheckable(true);
    connect(post, &QAction::triggered, this, [this](bool on) {
        ViewSettings v = m_viewport->viewSettings();
        v.post = on;
        m_viewport->setViewSettings(v);
    });
    auto syncPost = [this, post] { post->setChecked(m_viewport->viewSettings().post); };
    connect(m_viewport, &Viewport::viewSettingsChanged, post, syncPost);
    syncPost();
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

    // Render: images of the scene seen by the viewport's camera, without editor overlays
    QMenu *render = menuBar()->addMenu("Render");
    auto *renderImage = render->addAction("Render Image", this, [this] { renderImages(false); });
    renderImage->setShortcut(QKeySequence(Qt::Key_F4));
    auto *renderVideo = render->addAction("Render Video", this, &Editor::renderVideo);
    renderVideo->setShortcut(QKeySequence(Qt::Key_F5));
    auto *renderPasses = render->addAction("Render Passes", this, [this] { renderImages(true); });
    renderPasses->setShortcut(QKeySequence(Qt::Key_F6));
    render->addSeparator();
    render->addAction("Render Settings...", this, &Editor::editRenderSettings);

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
    m_addMenu->addAction("Environment", this, [this] {
        Entity e;
        e.name = m_scene->uniqueName(QStringLiteral("Environment"));
        e.environment = Environment {};
        m_undo->push(new AddEntityCommand(m_scene, m_selection, e));
    });
    m_addMenu->addAction("Camera", this, &Editor::addCamera);
    m_addMenu->addAction("Particle Emitter", this, [this] {
        Entity e;
        e.name = m_scene->uniqueName(QStringLiteral("Emitter"));
        e.emitter = ParticleEmitter {};
        m_undo->push(new AddEntityCommand(m_scene, m_selection, e));
    });
    QMenu *forces = m_addMenu->addMenu("Force");
    auto addForce = [this](const QString &name, ParticleForce force, QVector3D rotation = {}) {
        Entity e;
        e.name = m_scene->uniqueName(name);
        e.transform.rotation = rotation;
        e.force = force;
        m_undo->push(new AddEntityCommand(m_scene, m_selection, e));
    };
    forces->addAction("Gravity", this, [addForce] { addForce(QStringLiteral("Gravity"), ParticleForce {}); });
    forces->addAction("Wind", this, [addForce] {
        ParticleForce f;
        f.type = ParticleForce::Wind;
        f.strength = 3.0f;
        addForce(QStringLiteral("Wind"), f, QVector3D(0.0f, 0.0f, -90.0f)); // blowing along +X
    });
    forces->addAction("Turbulence", this, [addForce] {
        ParticleForce f;
        f.type = ParticleForce::Turbulence;
        f.strength = 2.0f;
        f.frequency = 0.5f;
        addForce(QStringLiteral("Turbulence"), f);
    });
    forces->addAction("Attractor", this, [addForce] {
        ParticleForce f;
        f.type = ParticleForce::Attractor;
        f.strength = 5.0f;
        f.radius = 3.0f;
        addForce(QStringLiteral("Attractor"), f);
    });
    forces->addAction("Vortex", this, [addForce] {
        ParticleForce f;
        f.type = ParticleForce::Vortex;
        f.strength = 3.0f;
        f.radius = 3.0f;
        f.pull = 1.0f;
        f.lift = 1.0f;
        f.noise = 0.5f;
        addForce(QStringLiteral("Vortex"), f);
    });
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
    m_outliner = new OutlinerPanel(m_scene, m_selection, m_viewport);
    m_outliner->setMargin(12);
    m_outliner->setPanelWidth(220);
    connect(m_outliner, &OutlinerPanel::renameRequested, this, [this](EntityId id, const QString &name) {
        m_undo->push(new RenameEntityCommand(m_scene, id, name));
    });

    // Inspector (right): built from the component descriptors, hidden while nothing is selected.
    m_inspector = new InspectorPanel(m_scene, m_selection, m_undo, m_viewport);
    m_inspector->setMargin(12);
    m_inspector->setPanelWidth(300);

    // Assets (bottom): folder tree + contents
    m_assets = new AssetsPanel(m_viewport, Panel::Side::Bottom);
    m_assets->setMargin(12);
    m_assets->setPanelHeight(220);
    m_assets->setStripGroup(QStringLiteral("bottom"));
    connect(m_assets, &AssetsPanel::fileActivated, this, &Editor::openAsset);

    // Curves (bottom, next to Assets): the two work like tabs, the one not in use is a strip.
    m_curves = new CurvesPanel(m_scene, m_selection, m_undo, m_viewport);
    m_curves->setMargin(12);
    m_curves->setPanelHeight(220);
    m_curves->setStripGroup(QStringLiteral("bottom"));
    m_curves->setStrip(true);

    // Nodes (right, Shading workspace): materials as node graphs.
    m_nodes = new NodesPanel(m_scene, m_selection, m_undo, m_viewport, Panel::Side::Right);
    m_nodes->setMargin(12);
    m_nodes->setPanelWidth(620);
    m_nodes->setPanelEnabled(false); // turned on by the Shading workspace

    // Timeline (footer): under everything, the full width of the window.
    m_timeline = new TimelinePanel(m_scene, m_playback, m_selection, m_undo, m_viewport);
    m_timeline->setMargin(12);

    // Drag and drop onto the viewport (from the Assets panel or the file manager).
    m_viewport->setDropSuffixes({ "obj", "gltf", "glb", "hdr", "lune" });
    connect(m_viewport, &Viewport::filesDropped, this, &Editor::dropFiles);

    // Created last so it sits above the panels.
    // Display dropdown in the top-right corner of the free viewport area.
    m_viewportMenus.append(new DisplayMenu(m_display, m_viewport));
    // Orbit / Free camera, top center.
    m_viewportMenus.append(new CameraMenu(m_viewport));
    // Shading mode and background, top left.
    m_viewportMenus.append(new ShadingMenu(m_viewport));

    m_palette = new CommandPalette(this, m_viewport);
}

void Editor::addEntity(const QString &name, const MeshPtr &mesh, const QVector3D &position)
{
    Entity e;
    e.name = m_scene->uniqueName(name);
    e.mesh = mesh;
    e.transform.position = position;
    m_undo->push(new AddEntityCommand(m_scene, m_selection, e));
}

bool Editor::addModel(const QString &path, QString *error, const QVector3D &position, bool restOnGround)
{
    const ImportedModel model = importModel(path, error);
    if (!model.mesh)
        return false;
    Entity e;
    e.name = m_scene->uniqueName(model.mesh->name.isEmpty() ? QFileInfo(path).completeBaseName() : model.mesh->name);
    e.mesh = model.mesh;
    e.transform.position = position;
    if (restOnGround)
        e.transform.position.setY(position.y() - model.mesh->boundsMin.y());

    const bool withMaterials = std::any_of(model.materials.begin(), model.materials.end(),
                                           [](const Material &m) { return !m.name.isEmpty(); });
    if (withMaterials)
        m_undo->beginMacro(QStringLiteral("Import %1").arg(QFileInfo(path).fileName()));
    for (size_t slot = 0; slot < model.materials.size(); ++slot) {
        if (model.materials[slot].name.isEmpty())
            continue; // a submesh without material
        Material m = model.materials[slot];
        m.name = m_scene->uniqueMaterialName(m.name);
        auto *command = new AddMaterialCommand(m_scene, m);
        m_undo->push(command); // redo() runs now: the id is known
        e.setMaterial(int(slot), command->materialId());
    }
    m_undo->push(new AddEntityCommand(m_scene, m_selection, e));
    if (withMaterials)
        m_undo->endMacro();
    return true;
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

void Editor::addCamera()
{
    const OrbitCamera view = m_viewport->camera();
    Entity e;
    e.name = m_scene->uniqueName(QStringLiteral("Camera"));
    e.transform.position = view.eye();
    e.transform.rotation = view.sceneCameraRotation();
    e.camera = Camera {};
    e.postProcess = PostProcess {};
    m_undo->push(new AddEntityCommand(m_scene, m_selection, e));
}

void Editor::alignCameraToView()
{
    const Entity *cam = m_scene->sceneCamera();
    if (!cam) {
        addCamera();
        return;
    }
    if (m_viewport->cameraView())
        return; // already looking through it
    const OrbitCamera view = m_viewport->camera();
    Transform t = cam->transform;
    t.position = view.eye();
    t.rotation = view.sceneCameraRotation(cam->transform.rotation);
    m_undo->push(new SetTransformCommand(m_scene, cam->id, cam->transform, t, TransformField::Rotation));
}

void Editor::updateRenderAspect()
{
    const float aspect = m_renderSettings.viewportSize ? 0.0f : float(m_renderSettings.width) / float(m_renderSettings.height);
    m_viewport->setRenderAspect(aspect);
}

void Editor::importMeshFile()
{
    const QString path = QFileDialog::getOpenFileName(this, "Import Mesh", lastDirectory(), meshFileFilter());
    if (path.isEmpty())
        return;
    QString error;
    if (!addModel(path, &error))
        QMessageBox::warning(this, "Import Mesh", error);
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
    m_viewport->setCameraMode(CameraMode::Orbit);
    m_display->reset();
    m_viewport->setViewSettings(ViewSettings {});
    m_renderSettings = RenderSettings();
    updateRenderAspect();
    m_playback->pause();
    m_playback->goToStart();
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
    followScene(path);
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
    followScene(path);
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
    camera["mode"] = m_viewport->cameraMode() == CameraMode::Free ? QStringLiteral("free") : QStringLiteral("orbit");
    camera["flySpeed"] = m_viewport->flySpeed();
    QJsonObject display;
    display["exposure"] = m_display->exposure();
    display["tonemapper"] = DisplaySettings::tonemapperIds().value(m_display->tonemapper());
    const ViewSettings &v = m_viewport->viewSettings();
    QJsonObject shading;
    static const char *shadingIds[] = { "wireframe", "clay", "unlit", "preview", "rendered" };
    shading["mode"] = QString::fromLatin1(shadingIds[int(v.shading)]);
    shading["skybox"] = v.skybox;
    shading["post"] = v.post;
    QJsonObject state;
    state["camera"] = camera;
    state["display"] = display;
    state["shading"] = shading;
    state["render"] = m_renderSettings.toJson();
    state["timeline"] = QJsonObject { { "frame", m_playback->frame() } };
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
    m_viewport->setCameraMode(camera["mode"].toString() == QLatin1String("free") ? CameraMode::Free : CameraMode::Orbit);
    m_viewport->setFlySpeed(float(camera["flySpeed"].toDouble(4.0)));

    // Missing or unknown values (older files, operators removed later): defaults.
    const QJsonObject display = state["display"].toObject();
    m_display->setExposure(display["exposure"].toDouble(0.0));
    const int tonemapper = DisplaySettings::tonemapperIds().indexOf(display["tonemapper"].toString());
    m_display->setTonemapper(tonemapper >= 0 ? tonemapper : DisplaySettings::None);

    const QJsonObject shading = state["shading"].toObject();
    static const QStringList shadingIds = { "wireframe", "clay", "unlit", "preview", "rendered" };
    ViewSettings v;
    const int mode = shadingIds.indexOf(shading["mode"].toString());
    v.shading = mode >= 0 ? Shading(mode) : Shading::Rendered;
    v.skybox = shading["skybox"].toBool(true);
    v.post = shading["post"].toBool(true);
    m_viewport->setViewSettings(v);
    m_renderSettings = RenderSettings::fromJson(state["render"].toObject());
    updateRenderAspect();
    m_playback->pause();
    m_playback->setFrame(state["timeline"].toObject()["frame"].toDouble(m_scene->timeline().start));
    Animation::apply(*m_scene); // also when the frame didn't change
}

void Editor::renderVideo()
{
    const Timeline timeline = m_scene->timeline();
    const QSize size = m_renderSettings.imageSize(m_viewport->size() * m_viewport->devicePixelRatio());
    const QString base = m_scenePath.isEmpty() ? QStringLiteral("untitled") : QFileInfo(m_scenePath).completeBaseName();
    const QString folder = QDir(m_renderSettings.outputFolder(m_scenePath)).filePath(base + QStringLiteral("_video"));
    if (!QDir().mkpath(folder)) {
        QMessageBox::warning(this, "Render Video", QStringLiteral("Can't create the folder %1.").arg(QDir::toNativeSeparators(folder)));
        return;
    }
    m_playback->pause();

    // A copy of the scene goes from frame to frame, so the editor's stays where it is.
    Scene scene;
    for (const Material &m : m_scene->materials())
        scene.insertMaterial(m);
    for (const Entity &e : m_scene->entities())
        scene.insertEntity(e);
    scene.setTimeline(timeline);

    QString error;
    ImageRenderSession session;
    if (!session.begin(m_viewport->rhiBackend(), window()->windowHandle(), size, float(m_display->exposure()),
                       m_display->tonemapper(), &error)) {
        QMessageBox::warning(this, "Render Video", error);
        return;
    }
    ViewSettings view = imagePasses().front().view; // the final image
    view.skybox = m_viewport->viewSettings().skybox;
    view.post = m_viewport->viewSettings().post;

    const int count = timeline.frameCount();
    QProgressDialog progress(QStringLiteral("Rendering frame 1 of %1...").arg(count), QStringLiteral("Cancel"), 0, count, this);
    progress.setWindowTitle("Render Video");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setValue(0);
    QElapsedTimer clock;
    clock.start();
    int done = 0;
    for (int f = timeline.start; f <= timeline.end; ++f) {
        if (progress.wasCanceled())
            break;
        scene.setFrame(f);
        Animation::apply(scene);
        // From the scene camera at this frame (it may be animated); without one, the viewport's.
        OrbitCamera camera = m_viewport->camera();
        if (const Entity *cam = scene.sceneCamera())
            camera = OrbitCamera::fromSceneCamera(cam->transform, *cam->camera);
        const QImage image = session.render(scene, camera, view, float(timeline.seconds(f)), &error);
        const QString path = QDir(folder).filePath(RenderSettings::fileName(base, f));
        if (image.isNull() || !image.save(path)) {
            progress.cancel();
            QMessageBox::warning(this, "Render Video",
                                 image.isNull() ? error : QStringLiteral("Can't write %1.").arg(QDir::toNativeSeparators(path)));
            return;
        }
        ++done;
        const double perFrame = clock.elapsed() / 1000.0 / done;
        const int left = int(std::lround(perFrame * (count - done)));
        progress.setLabelText(QStringLiteral("Rendering frame %1 of %2 (%3 s per frame, about %4:%5 left)...")
                                  .arg(qMin(done + 1, count))
                                  .arg(count)
                                  .arg(perFrame, 0, 'f', 2)
                                  .arg(left / 60)
                                  .arg(left % 60, 2, 10, QLatin1Char('0')));
        progress.setValue(done);
        QCoreApplication::processEvents();
    }
    session.end();
    progress.reset();

    // The MP4, when ffmpeg is installed (H.264, even sizes for yuv420p).
    QString video;
    QString encodeNote;
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (done == count && !ffmpeg.isEmpty()) {
        video = QDir(folder).filePath(base + QStringLiteral(".mp4"));
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        const QString pattern = QDir(folder).filePath(base + QStringLiteral("_frame%04d.png"));
        process.start(ffmpeg, { "-y", "-loglevel", "error", "-framerate", QString::number(timeline.fps), "-start_number",
                                QString::number(timeline.start), "-i", pattern, "-vf", "scale=trunc(iw/2)*2:trunc(ih/2)*2",
                                "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "18", video });
        QProgressDialog encoding(QStringLiteral("Encoding %1...").arg(QFileInfo(video).fileName()), QString(), 0, 0, this);
        encoding.setWindowTitle("Render Video");
        encoding.setWindowModality(Qt::WindowModal);
        encoding.setMinimumDuration(0);
        encoding.show();
        while (!process.waitForFinished(50))
            QCoreApplication::processEvents();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            encodeNote = QStringLiteral("\n\nffmpeg couldn't make the video:\n") + QString::fromLocal8Bit(process.readAll()).trimmed();
            video.clear();
        }
    } else if (done == count) {
        encodeNote = QStringLiteral("\n\nInstall ffmpeg (for example: winget install ffmpeg) to get an MP4 as well.");
    }

    QMessageBox box(this);
    box.setWindowTitle("Render Video");
    box.setIcon(done == count ? QMessageBox::Information : QMessageBox::Warning);
    QString text = done == count ? QStringLiteral("Rendered %1 frames in %2 s.").arg(count).arg(clock.elapsed() / 1000.0, 0, 'f', 1)
                                 : QStringLiteral("Stopped after %1 of %2 frames.").arg(done).arg(count);
    text += QStringLiteral("\n\nFrames: %1").arg(QDir::toNativeSeparators(folder));
    if (!video.isEmpty())
        text += QStringLiteral("\nVideo: %1").arg(QDir::toNativeSeparators(video));
    box.setText(text + encodeNote);
    QPushButton *open = box.addButton(video.isEmpty() ? QStringLiteral("Open Folder") : QStringLiteral("Play Video"),
                                      QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();
    if (box.clickedButton() == open)
        QDesktopServices::openUrl(QUrl::fromLocalFile(video.isEmpty() ? folder : video));
}

void Editor::editRenderSettings()
{
    const QSize viewportPixels = m_viewport->size() * m_viewport->devicePixelRatio();
    RenderSettingsDialog dialog(m_renderSettings, viewportPixels, m_scenePath, this);
    if (dialog.exec() == QDialog::Accepted)
        m_renderSettings = dialog.settings();
    updateRenderAspect();
}

void Editor::renderImages(bool allPasses)
{
    std::vector<const ImagePass *> passes;
    if (allPasses) {
        for (const QString &id : m_renderSettings.passes)
            if (const ImagePass *p = imagePass(id))
                passes.push_back(p);
        if (passes.empty()) {
            QMessageBox::information(this, "Render Passes", "No passes are selected: choose them in Render > Render Settings.");
            return;
        }
    } else {
        passes.push_back(&imagePasses().front()); // the final image
    }

    ImageRenderRequest request;
    request.size = m_renderSettings.imageSize(m_viewport->size() * m_viewport->devicePixelRatio());
    request.exposure = float(m_display->exposure());
    request.tonemapper = m_display->tonemapper();
    request.skybox = m_viewport->viewSettings().skybox; // what the viewport shows
    request.post = m_viewport->viewSettings().post;
    request.seconds = m_playback->seconds(); // the frame shown in the Timeline
    for (const ImagePass *p : passes)
        request.views.push_back(p->view);

    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // From the scene camera; without one, from where the viewport looks.
    OrbitCamera camera = m_viewport->camera();
    if (const Entity *cam = m_scene->sceneCamera())
        camera = OrbitCamera::fromSceneCamera(cam->transform, *cam->camera);
    const std::vector<QImage> images = ::renderImages(m_viewport->rhiBackend(), window()->windowHandle(), *m_scene,
                                                      camera, request, &error);
    QApplication::restoreOverrideCursor();
    if (images.size() != passes.size()) {
        QMessageBox::warning(this, "Render", error.isEmpty() ? QStringLiteral("Rendering failed.") : error);
        return;
    }

    const QString folder = m_renderSettings.outputFolder(m_scenePath);
    if (!QDir().mkpath(folder)) {
        QMessageBox::warning(this, "Render", QStringLiteral("Can't create the folder %1.").arg(QDir::toNativeSeparators(folder)));
        return;
    }
    const QString base = m_scenePath.isEmpty() ? QStringLiteral("untitled") : QFileInfo(m_scenePath).completeBaseName();
    QStringList titles, paths;
    for (size_t i = 0; i < passes.size(); ++i) {
        const QString path = QDir(folder).filePath(
            RenderSettings::fileName(base, int(std::lround(m_playback->frame())), allPasses ? passes[i]->id : QString()));
        if (!images[i].save(path)) {
            QMessageBox::warning(this, "Render", QStringLiteral("Can't write %1.").arg(QDir::toNativeSeparators(path)));
            return;
        }
        titles.append(passes[i]->title);
        paths.append(path);
    }
    auto *result = new RenderResultDialog(images, titles, paths, this);
    result->show();
}

void Editor::closeEvent(QCloseEvent *event)
{
    if (maybeSave()) {
        saveLayout();
        event->accept();
    } else {
        event->ignore();
    }
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

namespace
{
constexpr int kLayoutVersion = 2; // 2: workspaces
const QStringList kWorkspaces = { QStringLiteral("layout"), QStringLiteral("shading") };
} // namespace

QHash<QString, Panel *> Editor::workspacePanels() const
{
    return { { QStringLiteral("scene"), m_outliner },
             { QStringLiteral("inspector"), m_inspector },
             { QStringLiteral("assets"), m_assets },
             { QStringLiteral("nodes"), m_nodes },
             { QStringLiteral("timeline"), m_timeline },
             { QStringLiteral("curves"), m_curves } };
}

QJsonObject Editor::captureWorkspace() const
{
    QJsonObject panels;
    const auto all = workspacePanels();
    for (auto it = all.cbegin(); it != all.cend(); ++it)
        panels[it.key()] = it.value()->layoutState();
    return panels;
}

void Editor::applyWorkspace(const QJsonObject &state)
{
    const auto all = workspacePanels();
    for (auto it = all.cbegin(); it != all.cend(); ++it)
        if (state.contains(it.key()))
            it.value()->restoreLayoutState(state[it.key()].toObject());
}

QJsonObject Editor::defaultWorkspace(const QString &name)
{
    auto panel = [](const char *side, int width, int height, bool enabled, bool strip = false) {
        QJsonObject p;
        p["side"] = QString::fromLatin1(side);
        p["width"] = width;
        p["height"] = height;
        p["enabled"] = enabled;
        p["strip"] = strip;
        return p;
    };
    const bool shading = name == QLatin1String("shading");
    QJsonObject panels;
    panels["scene"] = panel("left", 220, 220, !shading);
    panels["inspector"] = panel("right", 300, 220, !shading);
    panels["assets"] = panel("bottom", 220, 220, true);
    panels["nodes"] = panel("right", 620, 220, shading);
    panels["timeline"] = panel("footer", 0, 56, true);
    panels["curves"] = panel("bottom", 220, 220, true, true);
    return panels;
}

void Editor::createWorkspaceTabs()
{
    // Tabs on the right end of the menu bar.
    auto *bar = new QWidget(menuBar());
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(0, 2, 8, 2);
    row->setSpacing(2);
    auto *group = new QButtonGroup(bar);
    group->setExclusive(true);
    const QStringList titles = { QStringLiteral("Layout"), QStringLiteral("Shading") };
    for (int i = 0; i < kWorkspaces.size(); ++i) {
        auto *tab = new QToolButton(bar);
        tab->setObjectName("WorkspaceTab");
        tab->setText(titles[i]);
        tab->setCheckable(true);
        tab->setFocusPolicy(Qt::NoFocus);
        tab->setCursor(Qt::PointingHandCursor);
        tab->setChecked(kWorkspaces[i] == m_workspace);
        group->addButton(tab);
        row->addWidget(tab);
        const QString name = kWorkspaces[i];
        connect(tab, &QToolButton::clicked, this, [this, name] { switchWorkspace(name); });
        m_workspaceTabs.insert(name, tab);
    }
    menuBar()->setCornerWidget(bar, Qt::TopRightCorner);
    for (const QString &name : kWorkspaces)
        m_workspaces.insert(name, defaultWorkspace(name));
}

void Editor::switchWorkspace(const QString &name)
{
    if (!kWorkspaces.contains(name))
        return;
    if (name != m_workspace) {
        m_workspaces[m_workspace] = captureWorkspace();
        m_workspace = name;
        applyWorkspace(m_workspaces.value(name, defaultWorkspace(name)));
    }
    if (QToolButton *tab = m_workspaceTabs.value(name))
        tab->setChecked(true);
    m_viewport->setCenterInFreeArea(name == QLatin1String("shading"));
}

void Editor::saveLayout() const
{
    QJsonObject workspaces;
    for (const QString &name : kWorkspaces)
        workspaces[name] = name == m_workspace ? captureWorkspace() : m_workspaces.value(name, defaultWorkspace(name));
    QJsonObject layout;
    layout["version"] = kLayoutVersion;
    layout["workspace"] = m_workspace;
    layout["workspaces"] = workspaces;
    layout["collapsed"] = Panel::isCollapsed(m_viewport);
    layout["assets"] = m_assets->viewState();

    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("editor/layout", QJsonDocument(layout).toJson(QJsonDocument::Compact));
    settings.setValue("assets/root", m_assets->rootPath());
}

void Editor::restoreLayout()
{
    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());

    const QString root = settings.value("assets/root").toString();
    if (!root.isEmpty())
        m_assets->setRootPath(root);

    const QJsonObject layout = QJsonDocument::fromJson(settings.value("editor/layout").toByteArray()).object();
    if (layout["version"].toInt() != kLayoutVersion) {
        switchWorkspace(m_workspace); // nothing saved yet, or an older layout: defaults
        return;
    }
    const QJsonObject workspaces = layout["workspaces"].toObject();
    for (const QString &name : kWorkspaces)
        if (workspaces.contains(name))
            m_workspaces.insert(name, workspaces[name].toObject());
    const QString current = layout["workspace"].toString();
    m_workspace = kWorkspaces.contains(current) ? current : kWorkspaces.first();
    applyWorkspace(m_workspaces.value(m_workspace));
    switchWorkspace(m_workspace);
    m_assets->restoreViewState(layout["assets"].toObject());
    m_collapsePanels->setChecked(layout["collapsed"].toBool());
}

void Editor::setPanelsCollapsed(bool collapsed)
{
    Panel::setCollapsed(m_viewport, collapsed);
    for (QWidget *menu : m_viewportMenus)
        menu->setVisible(!collapsed);
    m_collapsePanels->setChecked(collapsed); // when called from Tab
}

bool Editor::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Tab && key->modifiers() == Qt::NoModifier && isActiveWindow()) {
            // Text fields keep Tab (moving to the next field); otherwise it belongs to the
            // viewport when the mouse is over it and not over a panel.
            const QWidget *focus = QApplication::focusWidget();
            const bool typing = qobject_cast<const QLineEdit *>(focus) || qobject_cast<const QAbstractSpinBox *>(focus)
                || qobject_cast<const QTextEdit *>(focus) || qobject_cast<const QPlainTextEdit *>(focus);
            if (!typing && QApplication::widgetAt(QCursor::pos()) == m_viewport) {
                if (!key->isAutoRepeat())
                    setPanelsCollapsed(!Panel::isCollapsed(m_viewport));
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Assets
// ---------------------------------------------------------------------------

void Editor::followScene(const QString &path)
{
    const QString folder = QDir::cleanPath(QFileInfo(path).absolutePath());
    const QString root = m_assets->rootPath();
    if (folder != root && !folder.startsWith(root + '/'))
        m_assets->setRootPath(folder);
}

void Editor::openAsset(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("lune")) {
        if (maybeSave())
            loadScene(path);
        return;
    }
    if (meshFileFilter().contains(QStringLiteral("*.") + suffix)) {
        QString error;
        if (!addModel(path, &error))
            QMessageBox::warning(this, "Import Mesh", error);
    }
    if (suffix == QLatin1String("hdr"))
        useEnvironmentImage(path);
    // Other images: dragged onto the node editor (Image Texture nodes).
}

void Editor::dropFiles(const QStringList &paths, const QPointF &pos)
{
    // A scene replaces everything: alone, it opens; mixed with other files, it's skipped.
    if (paths.size() == 1 && QFileInfo(paths.first()).suffix().toLower() == QLatin1String("lune")) {
        if (maybeSave())
            loadScene(paths.first());
        return;
    }
    bool onGround = false;
    const QVector3D point = m_viewport->dropPoint(pos, &onGround);
    QStringList errors;
    m_undo->beginMacro(QStringLiteral("Drop files"));
    for (const QString &path : paths) {
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (suffix == QLatin1String("hdr")) {
            useEnvironmentImage(path);
            continue;
        }
        if (suffix == QLatin1String("lune"))
            continue;
        // On the ground: resting on it (its lowest point at the drop point).
        QString error;
        if (!addModel(path, &error, point, onGround))
            errors.append(error);
    }
    m_undo->endMacro();
    if (!errors.isEmpty())
        QMessageBox::warning(this, "Import Mesh", errors.join('\n'));
}

void Editor::useEnvironmentImage(const QString &path)
{
    // The selected environment, else the first one in the scene, else a new one.
    const Entity *target = m_scene->entity(m_selection->current());
    if (!target || !target->environment) {
        target = nullptr;
        for (const Entity &e : m_scene->entities()) {
            if (e.environment) {
                target = &e;
                break;
            }
        }
    }
    if (!target) {
        Entity e;
        e.name = m_scene->uniqueName(QStringLiteral("Environment"));
        e.environment = Environment {};
        e.environment->image = path;
        m_undo->push(new AddEntityCommand(m_scene, m_selection, e));
        return;
    }
    const auto &all = components();
    for (int ci = 0; ci < int(all.size()); ++ci) {
        if (all[ci].name == QLatin1String("Environment")) {
            m_undo->push(new SetPropertyCommand(m_scene, target->id, ci, 0, path)); // 0 = image
            m_selection->select(target->id);
            return;
        }
    }
}

void Editor::deleteSelected()
{
    if (!m_selection->isEmpty())
        m_undo->push(new DeleteEntityCommand(m_scene, m_selection, m_selection->current()));
}
