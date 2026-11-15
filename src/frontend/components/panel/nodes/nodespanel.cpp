#include "nodespanel.h"

#include <QBoxLayout>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QToolButton>
#include <QUndoStack>
#include <QVector2D>
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

// Node type of the Image Texture node and its image input (material_nodes.glsl).
const QString kImageTextureType = QStringLiteral("image_texture");

// A guess for a new image: maps whose name says they hold data, not color, are read linear.
bool looksLikeData(const QString &path)
{
    static const QStringList words { QStringLiteral("normal"), QStringLiteral("nrm"), QStringLiteral("rough"),
                                     QStringLiteral("metal"), QStringLiteral("occlusion"), QStringLiteral("_ao"),
                                     QStringLiteral("height"), QStringLiteral("disp"), QStringLiteral("orm"),
                                     QStringLiteral("mask") };
    const QString name = QFileInfo(path).completeBaseName().toLower();
    return std::any_of(words.begin(), words.end(), [&](const QString &w) { return name.contains(w); });
}

QString imageName(const QString &image)
{
    const int hash = image.lastIndexOf(QLatin1String("#image"));
    if (hash > 0)
        return QStringLiteral("%1 #%2").arg(QFileInfo(image.left(hash)).fileName(), image.mid(hash + 6));
    return QFileInfo(image).fileName();
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

    m_slotCombo = new QComboBox(body);
    m_slotCombo->setToolTip(QStringLiteral("Material slot: the part of the mesh whose material is shown"));
    m_slotCombo->hide();
    column->addWidget(m_slotCombo);

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
    connect(m_slotCombo, &QComboBox::activated, this, [this](int index) {
        if (m_syncing)
            return;
        m_slot = index;
        syncFromSelection();
    });
    connect(m_editor, &NodeEditorView::filesDropped, this, &NodesPanel::dropImages);
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
    connect(m_viewport, &Viewport::materialErrorsChanged, this, [this] {
        refreshStatus();
        m_propertiesSignature.clear(); // image rows show load errors
        rebuildProperties();
    });

    refreshMaterials();
    syncFromSelection();
}

void NodesPanel::syncFromSelection()
{
    const Entity *e = m_scene->entity(m_selection->current());
    const EntityId id = e ? e->id : InvalidEntity;
    if (id != m_slotEntity) {
        m_slotEntity = id;
        m_slot = 0;
    }
    const int slotCount = e && e->mesh ? int(e->mesh->submeshes.size()) : 0;
    m_slot = qBound(0, m_slot, qMax(0, slotCount - 1));
    refreshSlots();
    if (e && e->material(m_slot) != m_material)
        showMaterial(e->material(m_slot));
    else if (e && e->material(m_slot) == NoMaterial)
        showMaterial(NoMaterial);
    m_new->setEnabled(e && e->mesh);
    refreshMaterials();
}

void NodesPanel::refreshSlots()
{
    const Entity *e = m_scene->entity(m_selection->current());
    const int slotCount = e && e->mesh ? int(e->mesh->submeshes.size()) : 0;
    m_syncing = true;
    m_slotCombo->clear();
    for (int i = 0; i < slotCount; ++i) {
        const QString name = e->mesh->submeshes[size_t(i)].name;
        m_slotCombo->addItem(name.isEmpty() ? QStringLiteral("Slot %1").arg(i + 1)
                                            : QStringLiteral("Slot %1: %2").arg(i + 1).arg(name));
    }
    m_slotCombo->setCurrentIndex(m_slot);
    m_slotCombo->setVisible(slotCount > 1);
    m_syncing = false;
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
    // Images of the material that can't be loaded.
    if (const Material *m = m_scene->material(m_material)) {
        const NodeLibrary &library = NodeLibrary::instance();
        for (const GraphNode &n : m->graph.nodes) {
            const NodeType *type = library.type(n.type);
            for (const PortDesc &p : type ? type->inputs : std::vector<PortDesc>()) {
                if (p.type != PortType::Texture)
                    continue;
                const QString image = TextureValue::fromVariant(n.values.value(p.name), p.linear).image;
                const QString error = image.isEmpty() ? QString() : m_viewport->textureError(image);
                if (!error.isEmpty())
                    text += (text.isEmpty() ? QString() : QStringLiteral("\n")) + error;
            }
        }
    }
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
        for (const PortDesc &p : type->inputs) {
            if (!(p.linkable && m->graph.linkTo(nodeId, p.name)))
                signature += QLatin1Char(',') + p.name;
            if (p.type == PortType::Texture) { // image rows show the file: rebuilt when it changes
                const TextureValue t = TextureValue::fromVariant(node->values.value(p.name), p.linear);
                signature += QLatin1Char('=') + t.image + (t.linear ? QStringLiteral("|data") : QString());
            }
        }
    }
    if (signature == m_propertiesSignature && !signature.isEmpty()) {
        // Push the current values into the existing editors.
        for (QWidget *w : m_properties->findChildren<QWidget *>(Qt::FindDirectChildrenOnly)) {
            const QString port = w->property("port").toString();
            const PortDesc *p = type->input(port);
            if (!p)
                continue;
            const QVariant v = MaterialCompiler::inputValue(*node, *p);
            const QList<QDoubleSpinBox *> spins = w->findChildren<QDoubleSpinBox *>();
            if (p->type == PortType::Vec2 && spins.size() == 2) {
                const QVector2D xy = v.value<QVector2D>();
                QSignalBlocker blockX(spins[0]), blockY(spins[1]);
                spins[0]->setValue(xy.x());
                spins[1]->setValue(xy.y());
            } else if (auto *spin = spins.value(0)) {
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
        if (!p.hasValue() && p.type != PortType::Texture)
            continue; // computed by the shader (expr()): nothing to edit
        const QVariant value = MaterialCompiler::inputValue(*node, p);
        auto *row = new QWidget(m_properties);
        row->setProperty("port", p.name);
        auto *l = new QHBoxLayout(row);
        l->setContentsMargins(3, 0, 3, 0);
        l->setSpacing(6);
        const QString port = p.name;
        if (p.type == PortType::Texture) {
            // Image: the file (click to choose), color or data, remove.
            const TextureValue t = TextureValue::fromVariant(node->values.value(p.name), p.linear);
            auto *label = new QLabel(p.name, row);
            label->setMinimumWidth(70);
            auto *file = new QPushButton(t.image.isEmpty() ? QStringLiteral("Choose image...") : imageName(t.image), row);
            file->setCursor(Qt::PointingHandCursor);
            const QString error = t.image.isEmpty() ? QString() : m_viewport->textureError(t.image);
            file->setToolTip(error.isEmpty() ? (t.image.isEmpty() ? QStringLiteral("Choose an image (or drop one on the canvas)") : t.image)
                                             : t.image + QStringLiteral("\n") + error);
            if (!error.isEmpty())
                file->setText(QStringLiteral("! ") + file->text());
            connect(file, &QPushButton::clicked, this, [this, nodeId, port] { chooseImage(nodeId, port); });
            auto *space = new QComboBox(row);
            space->addItem(QStringLiteral("Color"));
            space->addItem(QStringLiteral("Data"));
            space->setToolTip(QStringLiteral("Color: an sRGB image (base color, emission)\n"
                                             "Data: values used as they are (normal, roughness, metallic, occlusion maps)"));
            space->setCurrentIndex(t.linear ? 1 : 0);
            connect(space, &QComboBox::activated, this, [this, nodeId, port, t](int index) {
                TextureValue changed = t;
                changed.linear = index == 1;
                if (changed != t)
                    editTexture(nodeId, port, changed);
            });
            l->addWidget(label);
            l->addWidget(file, 1);
            l->addWidget(space);
            if (!t.image.isEmpty()) {
                auto *clear = new QToolButton(row);
                clear->setText(QStringLiteral("x"));
                clear->setToolTip(QStringLiteral("Remove the image"));
                connect(clear, &QToolButton::clicked, this, [this, nodeId, port, t] {
                    TextureValue changed = t;
                    changed.image.clear();
                    editTexture(nodeId, port, changed);
                });
                l->addWidget(clear);
            }
        } else if (p.type == PortType::Vec2) {
            auto *label = new DragLabel(p.name, row);
            label->setMinimumWidth(70);
            l->addWidget(label);
            const QVector2D xy = value.value<QVector2D>();
            QDoubleSpinBox *spins[2];
            for (int c = 0; c < 2; ++c) {
                auto *spin = new QDoubleSpinBox(row);
                spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
                spin->setDecimals(3);
                spin->setRange(p.hasRange ? p.min : -1e6, p.hasRange ? p.max : 1e6);
                spin->setKeyboardTracking(false);
                spin->setValue(c == 0 ? xy.x() : xy.y());
                spins[c] = spin;
                l->addWidget(spin, 1);
            }
            auto push = [this, nodeId, port, spins] {
                editValue(nodeId, port, QVector2D(float(spins[0]->value()), float(spins[1]->value())));
            };
            for (QDoubleSpinBox *spin : spins)
                connect(spin, &QDoubleSpinBox::valueChanged, this, push);
            connect(label, &DragLabel::dragged, this, [spins](int dx, Qt::KeyboardModifiers mods) {
                const double step = 0.01 * dragFactor(mods); // both components: e.g. a uniform scale
                QSignalBlocker block(spins[0]);
                spins[0]->setValue(spins[0]->value() + dx * step);
                spins[1]->setValue(spins[1]->value() + dx * step);
            });
        } else if (p.type == PortType::Float) {
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

void NodesPanel::editTexture(int node, const QString &port, const TextureValue &value)
{
    const Material *m = m_scene->material(m_material);
    if (!m || !m->graph.node(node))
        return;
    NodeGraph after = m->graph;
    after.node(node)->values[port] = value.toVariant();
    m_undo->push(new SetMaterialGraphCommand(m_scene, m_material, after, QStringLiteral("Set image")));
}

void NodesPanel::chooseImage(int node, const QString &port)
{
    const Material *m = m_scene->material(m_material);
    const GraphNode *n = m ? m->graph.node(node) : nullptr;
    const NodeType *type = n ? NodeLibrary::instance().type(n->type) : nullptr;
    const PortDesc *p = type ? type->input(port) : nullptr;
    if (!p)
        return;
    TextureValue t = TextureValue::fromVariant(n->values.value(port), p->linear);
    const QString start = t.image.isEmpty() || t.image.contains(QLatin1String("#image")) ? QString() : QFileInfo(t.image).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose Image"), start,
                                                      QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.tga *.webp);;All files (*)"));
    if (path.isEmpty() || path == t.image)
        return;
    if (t.image.isEmpty())
        t.linear = p->linear || looksLikeData(path); // first image: guess color or data from the name
    t.image = path;
    editTexture(node, port, t);
}

void NodesPanel::dropImages(const QStringList &paths, const QPointF &scenePos, int nodeId)
{
    const Material *m = m_scene->material(m_material);
    if (!m || paths.isEmpty())
        return;
    const NodeLibrary &library = NodeLibrary::instance();
    NodeGraph after = m->graph;

    // On a node with an image input: that's its new image (the first file).
    if (GraphNode *n = after.node(nodeId)) {
        const NodeType *type = library.type(n->type);
        for (const PortDesc &p : type ? type->inputs : std::vector<PortDesc>()) {
            if (p.type != PortType::Texture)
                continue;
            TextureValue t = TextureValue::fromVariant(n->values.value(p.name), p.linear);
            if (t.image.isEmpty())
                t.linear = p.linear || looksLikeData(paths.first());
            t.image = paths.first();
            editTexture(n->id, p.name, t);
            return;
        }
    }

    // Elsewhere: an Image Texture node per file, stacked from the drop point.
    const NodeType *imageType = library.type(kImageTextureType);
    if (!imageType || !imageType->input(QStringLiteral("image")))
        return;
    QPointF at = scenePos;
    for (const QString &path : paths) {
        const int id = after.addNode(kImageTextureType, at);
        after.node(id)->values[QStringLiteral("image")] = TextureValue { path, looksLikeData(path) }.toVariant();
        at += QPointF(24, 110);
    }
    m_undo->push(new SetMaterialGraphCommand(m_scene, m_material, after,
                                             paths.size() == 1 ? QStringLiteral("Add image texture")
                                                               : QStringLiteral("Add image textures")));
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
    auto *command = new AddMaterialCommand(m_scene, proto, e && e->mesh ? e->id : InvalidEntity, m_slot);
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
    if (e && e->mesh && e->material(m_slot) != id)
        m_undo->push(new AssignMaterialCommand(m_scene, e->id, m_slot, id)); // shown through entityChanged
    else
        showMaterial(id); // nothing to assign to: just look at it
}
