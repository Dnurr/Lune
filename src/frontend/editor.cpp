#include "editor.h"

#include <QAction>
#include <QBoxLayout>
#include <QIcon>
#include <QMenuBar>
#include <QUndoStack>

#include "components/panel/outliner/outliner.h"
#include "components/panel/transform/transform.h"
#include "components/viewport/viewport.h"
#include "core/commands.h"
#include "core/selection.h"
#include "engine/scene/scene.h"

Editor::Editor(QWidget *parent)
    : QMainWindow(parent)
    , m_scene(new Scene(this))
    , m_selection(new Selection(this))
    , m_undo(new QUndoStack(this))
    , m_viewport(new Viewport(this))
{
    setWindowTitle("Lune");
    resize(1280, 720);

    // m_viewport->setApi(QRhiWidget::Api::Vulkan); // optional: pick a backend (default: Direct3D 11 on Windows)
    setCentralWidget(m_viewport);

    // Starting content: created directly (not a command), so it isn't in the undo history.
    m_selection->select(m_scene->createEntity(QStringLiteral("Entity")));

    createMenus();
    createPanels();

    // A deleted entity can't stay selected (commands handle it; this covers any other removal).
    connect(m_scene, &Scene::entityRemoved, this, [this](EntityId id) {
        if (m_selection->current() == id)
            m_selection->clear();
    });
    connect(m_selection, &Selection::changed, this, &Editor::refreshInspector);
    connect(m_scene, &Scene::entityChanged, this, [this](EntityId id) {
        if (id == m_selection->current())
            refreshInspector();
    });
    refreshInspector();
}

Editor::~Editor() = default;

void Editor::createMenus()
{
    // File (not connected yet: saving/loading the scene comes later)
    auto *newFile = new QAction(QIcon::fromTheme("document-new"), "New", this);
    newFile->setShortcut(QKeySequence::New);
    auto *openFile = new QAction(QIcon::fromTheme("document-open"), "Open", this);
    openFile->setShortcut(QKeySequence::Open);
    auto *saveFile = new QAction(QIcon::fromTheme("document-save"), "Save", this);
    saveFile->setShortcut(QKeySequence::Save);
    auto *saveAs = new QAction(QIcon::fromTheme("document-save-as"), "Save as", this);
    saveAs->setShortcut(QKeySequence::SaveAs);
    menuBar()->addMenu("File")->addActions({ newFile, openFile, saveFile, saveAs });

    // Edit: undo/redo text follows the history ("Undo Change position", ...)
    QAction *undo = m_undo->createUndoAction(this, "Undo");
    undo->setShortcut(QKeySequence::Undo);
    QAction *redo = m_undo->createRedoAction(this, "Redo");
    redo->setShortcuts({ QKeySequence(QKeySequence::Redo), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z) });

    auto *add = new QAction("Add Entity", this);
    add->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_A));
    connect(add, &QAction::triggered, this, &Editor::addEntity);

    auto *del = new QAction("Delete", this);
    del->setShortcut(QKeySequence::Delete);
    connect(del, &QAction::triggered, this, &Editor::deleteSelected);
    connect(m_selection, &Selection::changed, del, [del](EntityId id) { del->setEnabled(id != InvalidEntity); });
    del->setEnabled(!m_selection->isEmpty());

    QMenu *edit = menuBar()->addMenu("Edit");
    edit->addActions({ undo, redo });
    edit->addSeparator();
    edit->addActions({ add, del });
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

    // Inspector (right): only visible while something is selected.
    m_inspector = new Panel("Inspector", m_viewport, Panel::Side::Right);
    m_inspector->setMargin(12);
    m_inspector->setPanelWidth(280);

    m_transformPanel = new TransformPanel(m_inspector);
    m_transformPanel->setMargin(12);
    m_transformPanel->setPanelWidth(200);
    m_inspector->contentLayout()->addWidget(m_transformPanel);
    connect(m_transformPanel, &TransformPanel::edited, this, [this](const Transform &t, TransformField field) {
        const EntityId id = m_selection->current();
        if (id != InvalidEntity)
            m_undo->push(new SetTransformCommand(m_scene, id, t, field));
    });

    // Assets (bottom)
    auto *assets = new Panel("Assets", m_viewport, Panel::Side::Bottom);
    assets->setMargin(12);
    assets->setPanelHeight(200);
}

void Editor::refreshInspector()
{
    const Entity *e = m_scene->entity(m_selection->current());
    m_inspector->setVisible(e != nullptr);
    if (e)
        m_transformPanel->setTransform(e->transform);
}

void Editor::addEntity()
{
    m_undo->push(new AddEntityCommand(m_scene, m_selection, m_scene->uniqueName(QStringLiteral("Entity"))));
}

void Editor::deleteSelected()
{
    if (!m_selection->isEmpty())
        m_undo->push(new DeleteEntityCommand(m_scene, m_selection, m_selection->current()));
}
