#include "nodespanel.h"

#include <QBoxLayout>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>
#include <QUndoStack>
#include <QVector3D>

#include "engine/material/materialcompiler.h"
#include "engine/material/nodelibrary.h"
#include "engine/scene/scene.h"
#include "frontend/components/viewport/viewport.h"
#include "frontend/components/widgets/draglabel.h"
#include "frontend/components/widgets/vector3.h"
#include "frontend/core/commands.h"
#include "frontend/core/selection.h"
#include "nodeeditor.h"

namespace
{
QString swatchStyle(const QColor &c)
{
    return QStringLiteral("QPushButton { background-color: %1; border: 1px solid rgba(255, 255, 255, 40);"
                          " border-radius: 6px; min-height: 20px; padding: 0; }")
        .arg(c.name());
}

QColor toColor(const QVector3D &v)
{
    return QColor::fromRgbF(qBound(0.0f, v.x(), 1.0f), qBound(0.0f, v.y(), 1.0f), qBound(0.0f, v.z(), 1.0f));
}
} // namespace

NodesPanel::NodesPanel(Scene *scene, Selection *selection, QUndoStack *undoStack, Viewport *viewport, Side side)
    : Panel("Nodes", viewport, side)
    , m_scene(scene)
    , m_selection(selection)
    , m_undo(undoStack)
    , m_viewport(viewport)
{
    setContentFills(true);
    auto *body = new QWidget(this);
    auto *column = new QVBoxLayout(body);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(6);

    auto *bar = new QHBoxLayout;
    bar->setSpacing(4);
    m_combo = new QComboBox(body);
    m_combo->setToolTip(QStringLiteral("Material of the selected entity"));
    m_new = new QToolButton(body);
    m_new->setText(QStringLiteral("+"));
    m_new->setToolTip(QStringLiteral("New material (given to the selected entity)"));
    m_rename = new QToolButton(body);
    m_rename->setText(QStringLiteral("Rename"));
    bar->addWidget(m_combo, 1);
    bar->addWidget(m_new);
    bar->addWidget(m_rename);
    column->addLayout(bar);

    m_status = new QLabel(body);
    m_status->setObjectName("NodesStatus");
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->hide();
    column->addWidget(m_status);

    m_hint = new QLabel(QStringLiteral("Select a mesh and press + to give it a node material,\n"
                                       "or pick an existing material above."),
                        body);
    m_hint->setObjectName("PropertyValue");
    m_hint->setAlignment(Qt::AlignCenter);
    column->addWidget(m_hint, 1);

    m_splitter = new QSplitter(Qt::Vertical, body);
    m_splitter->setChildrenCollapsible(false);
    m_editor = new NodeEditorView(m_splitter);
    m_properties = new QWidget(m_splitter);
    m_propertiesLayout = new QVBoxLayout(m_properties);
    m_propertiesLayout->setContentsMargins(0, 4, 0, 0);
    m_propertiesLayout->setSpacing(4);
    m_propertiesLayout->addStretch(1);
    m_splitter->addWidget(m_editor);
    m_splitter->addWidget(m_properties);
    m_splitter->setStretchFactor(0, 4);
    m_splitter->setStretchFactor(1, 1);
    column->addWidget(m_splitter, 1);
    contentLayout()->addWidget(body, 1);

    connect(m_new, &QToolButton::clicked, this, &NodesPanel::newMaterial);
    connect(m_rename, &QToolButton::clicked, this, &NodesPanel::renameMaterial);
    connect(m_combo, &QComboBox::activated, this, &NodesPanel::chooseMaterial);
    connect(m_editor, &NodeEditorView::edited, this, [this](const NodeGraph &after, const QString &text, const QString &key) {
        if (m_scene->material(m_material))
            m_undo->push(new SetMaterialGraphCommand(m_scene, m_material, after, text, key));
    });
    connect(m_editor, &NodeEditorView::nodeSelected, this, &NodesPanel::rebuildProperties);

    connect(m_selection, &Selection::changed, this, &NodesPanel::syncFromSelection);
    connect(m_scene, &Scene::entityChanged, this, [this](EntityId id) {
        if (id == m_selection->current())
            syncFromSelection(); // its material may have changed (assign, undo)
    });
    connect(m_scene, &Scene::materialAdded, this, &NodesPanel::refreshMaterials);
    connect(m_scene, &Scene::materialRemoved, this, [this](MaterialId id) {
        if (id == m_material)
            showMaterial(NoMaterial);
        refreshMaterials();
    });
    connect(m_scene, &Scene::materialChanged, this, [this](MaterialId id) {
        refreshMaterials(); // the name may have changed
        if (id == m_material)
            refreshGraph();
    });
    connect(m_viewport, &Viewport::materialErrorsChanged, this, &NodesPanel::refreshStatus);

    refreshMaterials();
    syncFromSelection();
}

void NodesPanel::syncFromSelection()
{
    const Entity *e = m_scene->entity(m_selection->current());
    if (e && e->material != m_material)
        showMaterial(e->material);
    else if (e && e->material == NoMaterial)
        showMaterial(NoMaterial);
    m_new->setEnabled(e && e->mesh);
    refreshMaterials();
}

void NodesPanel::showMaterial(MaterialId id)
{
    if (id != m_material)
        m_editor->clearGraph(); // another material: framed again when shown
    m_material = m_scene->material(id) ? id : NoMaterial;
    refreshGraph();
    refreshMaterials();
}

void NodesPanel::refreshMaterials()
{
    m_syncing = true;
    m_combo->clear();
    m_combo->addItem(QStringLiteral("None"), QVariant::fromValue(NoMaterial));
    for (const Material &m : m_scene->materials())
        m_combo->addItem(m.name, QVariant::fromValue(m.id));
    m_combo->setCurrentIndex(qMax(0, m_combo->findData(QVariant::fromValue(m_material))));
    m_rename->setEnabled(m_material != NoMaterial);
    m_syncing = false;
}

void NodesPanel::refreshGraph()
{
    const Material *m = m_scene->material(m_material);
    if (m)
        m_editor->setGraph(m->graph, &NodeLibrary::instance());
    else
        m_editor->clearGraph();
    m_hint->setVisible(!m);
    m_splitter->setVisible(m);
    rebuildProperties();
    refreshStatus();
}

void NodesPanel::refreshStatus()
{
    QString text = m_viewport->materialError(m_material);
    if (m_material != NoMaterial && !NodeLibrary::instance().errors().isEmpty())
        text += (text.isEmpty() ? QString() : QStringLiteral("\n")) + QStringLiteral("material_nodes.glsl: ")
            + NodeLibrary::instance().errors().join(QStringLiteral("\n"));
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
}

void NodesPanel::rebuildProperties()
{
    const Material *m = m_scene->material(m_material);
    const int nodeId = m ? m_editor->selectedNode() : 0;
    const GraphNode *node = m ? m->graph.node(nodeId) : nullptr;
    const NodeType *type = node ? NodeLibrary::instance().type(node->type) : nullptr;

    // Same node, same unlinked inputs: only the values changed, keep the widgets
    // (rebuilding them would interrupt a drag on a label).
    QString signature;
    if (type) {
        signature = QString::number(nodeId);
        for (const PortDesc &p : type->inputs)
            if (!(p.linkable && m->graph.linkTo(nodeId, p.name)))
                signature += QLatin1Char(',') + p.name;
    }
    if (signature == m_propertiesSignature && !signature.isEmpty()) {
        // Push the current values into the existing editors.
        for (QWidget *w : m_properties->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
            const QString port = w->property("port").toString();
            const PortDesc *p = type->input(port);
            if (!p)
                continue;
            const QVariant v = MaterialCompiler::inputValue(*node, *p);
            if (auto *spin = w->findChild<QDoubleSpinBox *>()) {
                QSignalBlocker block(spin);
                spin->setValue(v.toDouble());
            } else if (auto *vec = w->findChild<Vector3 *>()) {
                QSignalBlocker block(vec);
                vec->setValue(v.value<QVector3D>());
            } else if (auto *swatch = w->findChild<QPushButton *>()) {
                swatch->setStyleSheet(swatchStyle(toColor(v.value<QVector3D>())));
            }
        }
        return;
    }
    m_propertiesSignature = signature;
    m_propertiesNode = nodeId;

    while (QLayoutItem *item = m_propertiesLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    if (!type) {
        auto *none = new QLabel(m ? QStringLiteral("Select a node to edit its values.") : QString(), m_properties);
        none->setObjectName("PropertyValue");
        m_propertiesLayout->addWidget(none);
        m_propertiesLayout->addStretch(1);
        return;
    }
    auto *title = new QLabel(type->title, m_properties);
    title->setObjectName("PanelTitle");
    m_propertiesLayout->addWidget(title);

    for (const PortDesc &p : type->inputs) {
        if (p.linkable && m->graph.linkTo(nodeId, p.name))
            continue; // linked: its value comes from the link
        const QVariant value = MaterialCompiler::inputValue(*node, p);
        auto *row = new QWidget(m_properties);
        row->setProperty("port", p.name);
        auto *l = new QHBoxLayout(row);
        l->setContentsMargins(3, 0, 3, 0);
        l->setSpacing(6);
        const QString port = p.name;
        if (p.type == PortType::Float) {
            auto *label = new DragLabel(p.name, row);
            label->setMinimumWidth(70);
            auto *spin = new QDoubleSpinBox(row);
            spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
            spin->setDecimals(3);
            spin->setRange(p.hasRange ? p.min : -1e6, p.hasRange ? p.max : 1e6);
            spin->setKeyboardTracking(false);
            spin->setValue(value.toDouble());
            const double step = p.hasRange ? (p.max - p.min) / 200.0 : 0.01;
            connect(label, &DragLabel::dragged, spin, [spin, step](int dx, Qt::KeyboardModifiers mods) {
                spin->setValue(spin->value() + dx * step * dragFactor(mods));
            });
            connect(spin, &QDoubleSpinBox::valueChanged, this,
                    [this, nodeId, port](double x) { editValue(nodeId, port, float(x)); });
            l->addWidget(label);
            l->addWidget(spin, 1);
        } else if (p.color) {
            auto *label = new QLabel(p.name, row);
            label->setMinimumWidth(70);
            auto *swatch = new QPushButton(row);
            swatch->setCursor(Qt::PointingHandCursor);
            swatch->setStyleSheet(swatchStyle(toColor(value.value<QVector3D>())));
            connect(swatch, &QPushButton::clicked, this, [this, nodeId, port] {
                const Material *mat = m_scene->material(m_material);
                const GraphNode *n = mat ? mat->graph.node(nodeId) : nullptr;
                const NodeType *t = n ? NodeLibrary::instance().type(n->type) : nullptr;
                const PortDesc *pd = t ? t->input(port) : nullptr;
                if (!pd)
                    return;
                const QColor current = toColor(MaterialCompiler::inputValue(*n, *pd).value<QVector3D>());
                const QColor c = QColorDialog::getColor(current, this, port);
                if (c.isValid() && c != current)
                    editValue(nodeId, port, QVector3D(float(c.redF()), float(c.greenF()), float(c.blueF())));
            });
            l->addWidget(label);
            l->addWidget(swatch, 1);
        } else {
            auto *vec = new Vector3(p.name, row);
            vec->setValue(value.value<QVector3D>());
            connect(vec, &Vector3::valueChanged, this,
                    [this, nodeId, port](const QVector3D &x) { editValue(nodeId, port, x); });
            l->addWidget(vec, 1);
        }
        m_propertiesLayout->addWidget(row);
    }
    m_propertiesLayout->addStretch(1);
}

void NodesPanel::editValue(int node, const QString &port, const QVariant &value)
{
    const Material *m = m_scene->material(m_material);
    if (!m || !m->graph.node(node))
        return;
    NodeGraph after = m->graph;
    after.node(node)->values[port] = value;
    m_undo->push(new SetMaterialGraphCommand(m_scene, m_material, after, QStringLiteral("Edit node value"),
                                             QStringLiteral("value:%1:%2").arg(node).arg(port)));
}

void NodesPanel::newMaterial()
{
    const Entity *e = m_scene->entity(m_selection->current());
    Material proto;
    proto.name = m_scene->uniqueMaterialName(QStringLiteral("Material"));
    proto.graph = Material::defaultGraph();
    // Start from the entity's look: its color, roughness and metallic in the output node.
    if (e) {
        GraphNode *out = proto.graph.node(1);
        out->values[QStringLiteral("baseColor")] = QVector3D(float(e->displayColor.redF()), float(e->displayColor.greenF()),
                                                             float(e->displayColor.blueF()));
        out->values[QStringLiteral("roughness")] = e->roughness;
        out->values[QStringLiteral("metallic")] = e->metallic;
    }
    auto *command = new AddMaterialCommand(m_scene, proto, e && e->mesh ? e->id : InvalidEntity);
    m_undo->push(command);
    showMaterial(command->materialId());
}

void NodesPanel::renameMaterial()
{
    const Material *m = m_scene->material(m_material);
    if (!m)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Rename Material"), QStringLiteral("Name"),
                                               QLineEdit::Normal, m->name, &ok).trimmed();
    if (ok && !name.isEmpty() && name != m->name)
        m_undo->push(new RenameMaterialCommand(m_scene, m_material, name));
}

void NodesPanel::chooseMaterial(int comboIndex)
{
    if (m_syncing)
        return;
    const MaterialId id = m_combo->itemData(comboIndex).value<MaterialId>();
    const Entity *e = m_scene->entity(m_selection->current());
    if (e && e->mesh && e->material != id)
        m_undo->push(new AssignMaterialCommand(m_scene, e->id, id)); // shown through entityChanged
    else
        showMaterial(id); // nothing to assign to: just look at it
}
