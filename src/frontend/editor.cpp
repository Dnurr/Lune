#include "editor.h"
#include <QAction>
#include <QMenuBar>
#include <QIcon>
#include <QVBoxLayout>

#include "components/panel/transform/transform.h"
#include "components/renderer/renderer.h"

Editor::Editor(QWidget *parent)
    : QMainWindow(parent)
    , m_viewport(new Viewport(this))
{
    setWindowTitle("Lune");
    resize(1280, 720);

    // m_viewport->setApi(QRhiWidget::Api::Vulkan); // optional: pick a backend (default: Direct3D 11 on Windows)
    setCentralWidget(m_viewport);

    // File actions
    // New
    QAction* newFile = new QAction(QIcon::fromTheme("document-new"), "New", this);
    newFile->setShortcut(QKeySequence::New);

    // Open
    QAction* openFile = new QAction(QIcon::fromTheme("document-open"), "Open", this);
    openFile->setShortcut(QKeySequence::Open);

    // Save
    QAction* saveFile = new QAction(QIcon::fromTheme("document-save"), "Save", this);
    saveFile->setShortcut(QKeySequence::Save);

    // Save as
    QAction* saveAs = new QAction(QIcon::fromTheme("document-save-as"), "Save as", this);
    saveAs->setShortcut(QKeySequence::SaveAs);

    // Future UI goes here: menuBar(), addToolBar(...), addDockWidget(...), statusBar()
    QList<QAction*> fileActions({
        newFile,
        openFile,
        saveFile,
        saveAs
    });

    this->menuBar()->addMenu("File")->addActions(fileActions);

    // Inspector: floating panel on top of the viewport, with a margin from the edges.
    auto *inspector = new Panel("Inspector", m_viewport, Panel::Side::Right);
    inspector->setMargin(12);
    inspector->setPanelWidth(280);

    auto *transformPanel = new TransformPanel(inspector);
    transformPanel->setMargin(12);
    transformPanel->setPanelWidth(200);

    inspector->contentLayout()->addWidget(transformPanel);

    // Assets: bottom panel, laid out between the left/right panels.
    auto *assets = new Panel("Assets", m_viewport, Panel::Side::Bottom);
    assets->setMargin(12);
    assets->setPanelHeight(200);
}

Editor::~Editor() = default;
