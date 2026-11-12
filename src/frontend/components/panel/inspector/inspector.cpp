#include "inspector.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QUndoStack>

#include "engine/scene/components.h"
#include "engine/scene/scene.h"
#include "frontend/components/widgets/draglabel.h"
#include "frontend/components/widgets/vector3.h"
#include "frontend/core/commands.h"
#include "frontend/core/selection.h"

namespace
{
// Label + editor on one line, laid out like a Vector3 row.
QHBoxLayout *rowLayout(QWidget *row)
{
    auto *l = new QHBoxLayout(row);
    l->setContentsMargins(3, 0, 3, 3);
    l->setSpacing(5);
    return l;
}

QString swatchStyle(const QColor &c)
{
    return QStringLiteral("QPushButton { background-color: %1; border: 1px solid rgba(255, 255, 255, 40);"
                          " border-radius: 6px; min-height: 20px; padding: 0; }")
        .arg(c.name());
}
} // namespace

InspectorPanel::InspectorPanel(Scene *scene, Selection *selection, QUndoStack *undoStack, QWidget *parent)
    : Panel("Inspector", parent, Panel::Side::Right)
    , m_scene(scene)
    , m_selection(selection)
    , m_undo(undoStack)
{
    connect(m_selection, &Selection::changed, this, &InspectorPanel::rebuild);
    connect(m_scene, &Scene::entityChanged, this, [this](EntityId id) {
        if (id != m_shown)
            return;
        const Entity *e = m_scene->entity(id);
        if (e && presentComponents(*e) != m_shownComponents)
            rebuild();
        else
            refresh();
    });
    rebuild();
}

std::vector<bool> InspectorPanel::presentComponents(const Entity &e) const
{
    std::vector<bool> present;
    for (const ComponentDesc &c : components())
        present.push_back(c.present(e));
    return present;
}

void InspectorPanel::rebuild()
{
    // deleteLater: rebuild() may run inside a signal coming from one of these widgets.
    for (Panel *s : m_sections) {
        s->hide();
        s->deleteLater();
    }
    m_sections.clear();
    m_rows.clear();

    const Entity *e = m_scene->entity(m_selection->current());
    m_shown = e ? e->id : InvalidEntity;
    setVisible(e != nullptr);
    if (!e)
        return;
    m_shownComponents = presentComponents(*e);

    const auto &all = components();
    for (int ci = 0; ci < int(all.size()); ++ci) {
        const ComponentDesc &comp = all[size_t(ci)];
        if (!m_shownComponents[size_t(ci)])
            continue;

        auto *section = new Panel(comp.name.toUpper(), this);
        section->setMargin(12);
        contentLayout()->addWidget(section);
        m_sections.push_back(section);

        // Every row's label gets the width of the widest one, so the editors line up.
        std::vector<std::function<int()>> labelHints;
        std::vector<std::function<void(int)>> labelSetters;
        auto alignLabel = [&](QLabel *label) {
            labelHints.push_back([label] { return label->sizeHint().width(); });
            labelSetters.push_back([label](int w) { label->setFixedWidth(w); });
        };

        for (int pi = 0; pi < int(comp.properties.size()); ++pi) {
            const PropertyDesc &prop = comp.properties[size_t(pi)];
            Row row { ci, pi, {} };

            switch (prop.type) {
            case PropertyType::Vector3: {
                auto *v = new Vector3(prop.name, section);
                v->setDragStep(prop.dragStep);
                v->setEnabled(!prop.readOnly);
                connect(v, &Vector3::valueChanged, this,
                        [this, ci, pi](const QVector3D &x) { edit(ci, pi, QVariant::fromValue(x)); });
                row.show = [v](const QVariant &x) { v->setValue(x.value<QVector3D>()); };
                labelHints.push_back([v] { return v->labelWidthHint(); });
                labelSetters.push_back([v](int w) { v->setLabelWidth(w); });
                section->contentLayout()->addWidget(v);
                break;
            }
            case PropertyType::Float: {
                auto *w = new QWidget(section);
                auto *l = rowLayout(w);
                auto *label = new DragLabel(prop.name, w);
                auto *spin = new QDoubleSpinBox(w);
                spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
                spin->setRange(prop.minimum, prop.maximum);
                spin->setDecimals(3);
                spin->setKeyboardTracking(false);
                spin->setEnabled(!prop.readOnly);
                l->addWidget(label);
                l->addWidget(spin, 1);
                const double step = prop.dragStep;
                connect(label, &DragLabel::dragged, spin, [spin, step](int dx, Qt::KeyboardModifiers mods) {
                    spin->setValue(spin->value() + dx * step * dragFactor(mods));
                });
                connect(spin, &QDoubleSpinBox::valueChanged, this, [this, ci, pi](double x) { edit(ci, pi, x); });
                row.show = [spin](const QVariant &x) { spin->setValue(x.toDouble()); };
                alignLabel(label);
                section->contentLayout()->addWidget(w);
                break;
            }
            case PropertyType::Color: {
                auto *w = new QWidget(section);
                auto *l = rowLayout(w);
                auto *label = new QLabel(prop.name, w);
                auto *swatch = new QPushButton(w);
                swatch->setCursor(Qt::PointingHandCursor);
                swatch->setEnabled(!prop.readOnly);
                l->addWidget(label);
                l->addWidget(swatch, 1);
                const QString title = prop.name;
                connect(swatch, &QPushButton::clicked, this, [this, ci, pi, swatch, title] {
                    const QColor current = swatch->property("color").value<QColor>();
                    const QColor c = QColorDialog::getColor(current, this, title);
                    if (c.isValid() && c != current)
                        edit(ci, pi, c);
                });
                row.show = [swatch](const QVariant &x) {
                    const QColor c = x.value<QColor>();
                    swatch->setProperty("color", c);
                    swatch->setStyleSheet(swatchStyle(c));
                    swatch->setToolTip(c.name().toUpper());
                };
                alignLabel(label);
                section->contentLayout()->addWidget(w);
                break;
            }
            case PropertyType::Bool: {
                auto *w = new QWidget(section);
                auto *l = rowLayout(w);
                auto *label = new QLabel(prop.name, w);
                auto *check = new QCheckBox(w);
                check->setEnabled(!prop.readOnly);
                l->addWidget(label);
                l->addWidget(check, 1);
                connect(check, &QCheckBox::toggled, this, [this, ci, pi](bool on) { edit(ci, pi, on); });
                row.show = [check](const QVariant &x) { check->setChecked(x.toBool()); };
                alignLabel(label);
                section->contentLayout()->addWidget(w);
                break;
            }
            case PropertyType::Text: {
                auto *w = new QWidget(section);
                auto *l = rowLayout(w);
                auto *label = new QLabel(prop.name, w);
                auto *value = new QLabel(w);
                value->setObjectName("PropertyValue");
                value->setTextInteractionFlags(Qt::TextSelectableByMouse);
                l->addWidget(label);
                l->addWidget(value, 1);
                row.show = [value](const QVariant &x) { value->setText(x.toString()); };
                alignLabel(label);
                section->contentLayout()->addWidget(w);
                break;
            }
            }
            m_rows.push_back(std::move(row));
        }

        int width = 0;
        for (const auto &hint : labelHints)
            width = qMax(width, hint());
        for (const auto &set : labelSetters)
            set(width);
    }

    refresh();
}

void InspectorPanel::refresh()
{
    const Entity *e = m_scene->entity(m_shown);
    if (!e)
        return;
    m_updating = true;
    for (const Row &row : m_rows)
        row.show(components()[size_t(row.component)].get(*e, row.property));
    m_updating = false;
}

void InspectorPanel::edit(int component, int property, const QVariant &value)
{
    if (m_updating || m_shown == InvalidEntity)
        return;
    m_undo->push(new SetPropertyCommand(m_scene, m_shown, component, property, value));
}
