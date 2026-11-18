#include "inspector.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QStyle>
#include <QUndoStack>
#include <memory>

#include "engine/scene/animation.h"
#include "engine/scene/components.h"
#include "engine/scene/scene.h"
#include "frontend/components/widgets/draglabel.h"
#include "frontend/components/widgets/keybutton.h"
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
// The wheel over a number or a list that isn't being edited scrolls the panel instead of
// changing the value (it's easy to change one by accident while scrolling).
class WheelGuard : public QObject
{
public:
    using QObject::QObject;

    static void install(QWidget *w)
    {
        static WheelGuard guard;
        w->setFocusPolicy(Qt::StrongFocus); // no focus from the wheel
        w->installEventFilter(&guard);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *w = qobject_cast<QWidget *>(watched);
        if (event->type() == QEvent::Wheel && w && !w->hasFocus() && w->parentWidget()) {
            QCoreApplication::sendEvent(w->parentWidget(), event); // on to the scroll area
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
};
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
    connect(m_scene, &Scene::frameChanged, this, &InspectorPanel::refresh); // keys on this frame
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
    if (m_addButton) {
        m_addButton->hide();
        m_addButton->deleteLater();
        m_addButton = nullptr;
    }

    const Entity *e = m_scene->entity(m_selection->current());
    m_shown = e ? e->id : InvalidEntity;
    setPanelVisible(e != nullptr);
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

        // Groups (e.g. the effects of Post Process): a heading that opens and closes the
        // group's rows, with the group's "enabled" switch in it when it has one.
        QString group;
        QBoxLayout *rows = section->contentLayout(); // where the rows go: the section, or the open group
        for (int pi = 0; pi < int(comp.properties.size()); ++pi) {
            const PropertyDesc &prop = comp.properties[size_t(pi)];
            Row row { ci, pi, {} };
            if (prop.group != group) {
                group = prop.group;
                rows = section->contentLayout();
                if (!group.isEmpty()) {
                    auto *heading = new QWidget(section);
                    auto *hl = new QHBoxLayout(heading);
                    hl->setContentsMargins(0, 6, 3, 0);
                    auto *toggle = new QToolButton(heading);
                    toggle->setObjectName("PropertyGroup");
                    toggle->setText(group.toUpper());
                    toggle->setCheckable(true);
                    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                    toggle->setCursor(Qt::PointingHandCursor);
                    toggle->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); // the switch goes to the right
                    hl->addWidget(toggle, 1);
                    section->contentLayout()->addWidget(heading);

                    auto *body = new QWidget(section);
                    auto *bl = new QVBoxLayout(body);
                    bl->setContentsMargins(0, 0, 0, 0);
                    bl->setSpacing(section->contentLayout()->spacing());
                    section->contentLayout()->addWidget(body);
                    rows = bl;

                    const QString key = comp.name + QLatin1Char('/') + group;
                    const bool open = m_openGroups.contains(key);
                    toggle->setChecked(open);
                    toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
                    body->setVisible(open);
                    connect(toggle, &QToolButton::toggled, this, [this, toggle, body, key](bool on) {
                        toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
                        body->setVisible(on);
                        if (on)
                            m_openGroups.insert(key);
                        else
                            m_openGroups.remove(key);
                    });

                    // The group's switch goes in the heading.
                    if (prop.type == PropertyType::Bool && prop.name == QLatin1String("enabled")) {
                        auto *check = new QCheckBox(heading);
                        check->setToolTip(QStringLiteral("On / off"));
                        check->setEnabled(!prop.readOnly);
                        hl->addWidget(check);
                        connect(check, &QCheckBox::toggled, this, [this, ci, pi](bool on) { edit(ci, pi, on); });
                        row.show = [check](const QVariant &x) { check->setChecked(x.toBool()); };
                        addKeyButton(hl, heading, row);
                        m_rows.push_back(std::move(row));
                        continue;
                    }
                }
            }

            switch (prop.type) {
            case PropertyType::Vector3: {
                auto *v = new Vector3(prop.name, section);
                for (QDoubleSpinBox *coordinate : v->findChildren<QDoubleSpinBox *>())
                    WheelGuard::install(coordinate);
                v->setDragStep(prop.dragStep);
                v->setEnabled(!prop.readOnly);
                connect(v, &Vector3::valueChanged, this,
                        [this, ci, pi](const QVector3D &x) { edit(ci, pi, QVariant::fromValue(x)); });
                row.show = [v](const QVariant &x) { v->setValue(x.value<QVector3D>()); };
                labelHints.push_back([v] { return v->labelWidthHint(); });
                labelSetters.push_back([v](int w) { v->setLabelWidth(w); });
                auto *w = new QWidget(section); // the vector and its key button
                auto *l = new QHBoxLayout(w);
                l->setContentsMargins(0, 0, 3, 0);
                l->setSpacing(2);
                v->setParent(w);
                l->addWidget(v, 1);
                addKeyButton(l, w, row);
                rows->addWidget(w);
                break;
            }
            case PropertyType::Float: {
                auto *w = new QWidget(section);
                auto *l = rowLayout(w);
                auto *label = new DragLabel(prop.name, w);
                auto *spin = new QDoubleSpinBox(w);
                WheelGuard::install(spin);
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
                row.field = spin;
                addKeyButton(l, w, row);
                alignLabel(label);
                rows->addWidget(w);
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
                addKeyButton(l, w, row);
                alignLabel(label);
                rows->addWidget(w);
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
                addKeyButton(l, w, row);
                alignLabel(label);
                rows->addWidget(w);
                break;
            }
            case PropertyType::Enum: {
                auto *w = new QWidget(section);
                auto *l = rowLayout(w);
                auto *label = new QLabel(prop.name, w);
                auto *combo = new QComboBox(w);
                WheelGuard::install(combo);
                for (const QString &id : prop.options) // "point" -> "Point"
                    combo->addItem(id.left(1).toUpper() + id.mid(1));
                combo->setEnabled(!prop.readOnly);
                l->addWidget(label);
                l->addWidget(combo, 1);
                connect(combo, &QComboBox::currentIndexChanged, this, [this, ci, pi](int index) {
                    if (index >= 0)
                        edit(ci, pi, index);
                });
                row.show = [combo](const QVariant &x) { combo->setCurrentIndex(x.toInt()); };
                row.field = combo;
                addKeyButton(l, w, row);
                alignLabel(label);
                rows->addWidget(w);
                break;
            }
            case PropertyType::File: {
                // File name as a button that opens the file dialog, plus a button to clear it.
                auto *w = new QWidget(section);
                auto *l = rowLayout(w);
                auto *label = new QLabel(prop.name, w);
                auto *pick = new QPushButton(w);
                pick->setObjectName("FileButton");
                pick->setCursor(Qt::PointingHandCursor);
                pick->setEnabled(!prop.readOnly);
                auto *clear = new QPushButton(QStringLiteral("×"), w); // multiplication sign
                clear->setObjectName("FileClearButton");
                clear->setCursor(Qt::PointingHandCursor);
                clear->setToolTip(QStringLiteral("Remove"));
                clear->setFixedWidth(24);
                l->addWidget(label);
                l->addWidget(pick, 1);
                l->addWidget(clear);
                const QString title = prop.name;
                const QString filter = prop.fileFilter;
                connect(pick, &QPushButton::clicked, this, [this, ci, pi, pick, title, filter] {
                    const QString current = pick->property("path").toString();
                    const QString path = QFileDialog::getOpenFileName(
                        this, QStringLiteral("Choose ") + title, current.isEmpty() ? QString() : QFileInfo(current).absolutePath(),
                        filter);
                    if (!path.isEmpty() && path != current)
                        edit(ci, pi, QDir::cleanPath(path));
                });
                connect(clear, &QPushButton::clicked, this, [this, ci, pi] { edit(ci, pi, QString()); });
                row.show = [pick, clear](const QVariant &x) {
                    const QString path = x.toString();
                    pick->setProperty("path", path);
                    const QString name = path.isEmpty() ? QStringLiteral("None") : QFileInfo(path).fileName();
                    // Long names are elided, the full path is in the tooltip.
                    pick->setText(pick->fontMetrics().elidedText(name, Qt::ElideMiddle, 150));
                    pick->setToolTip(path.isEmpty() ? QStringLiteral("Choose a file") : QDir::toNativeSeparators(path));
                    clear->setVisible(!path.isEmpty());
                };
                addKeyButton(l, w, row);
                alignLabel(label);
                rows->addWidget(w);
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
                addKeyButton(l, w, row);
                alignLabel(label);
                rows->addWidget(w);
                break;
            }
            }
            m_rows.push_back(std::move(row));
        }

        if (comp.remove) { // optional component: can be taken away again
            auto *remove = new QPushButton(QStringLiteral("Remove ") + comp.name, section);
            remove->setObjectName("RemoveComponentButton");
            remove->setCursor(Qt::PointingHandCursor);
            const EntityId id = e->id;
            connect(remove, &QPushButton::clicked, this, [this, id, ci] {
                m_undo->push(new SetComponentCommand(m_scene, id, ci, false));
            });
            section->contentLayout()->addWidget(remove);
        }

        int width = 0;
        for (const auto &hint : labelHints)
            width = qMax(width, hint());
        for (const auto &set : labelSetters)
            set(width);
    }

    // Optional components this entity can take (e.g. Post Process on a camera).
    auto *menu = new QMenu(this);
    for (int ci = 0; ci < int(all.size()); ++ci) {
        if (!all[size_t(ci)].addable(*e))
            continue;
        const EntityId id = e->id;
        menu->addAction(all[size_t(ci)].name, this, [this, id, ci] {
            m_undo->push(new SetComponentCommand(m_scene, id, ci, true));
        });
    }
    if (!menu->isEmpty()) {
        m_addButton = new QPushButton(QStringLiteral("Add Component"), this);
        m_addButton->setObjectName("AddComponentButton");
        m_addButton->setCursor(Qt::PointingHandCursor);
        m_addButton->setMenu(menu);
        menu->setParent(m_addButton, menu->windowFlags()); // goes away with the button
        contentLayout()->addWidget(m_addButton);
    } else {
        delete menu;
    }

    refresh();
}

void InspectorPanel::refresh()
{
    const Entity *e = m_scene->entity(m_shown);
    if (!e)
        return;
    m_updating = true;
    const double frame = m_scene->frame();
    for (const Row &row : m_rows) {
        row.show(components()[size_t(row.component)].get(*e, row.property));
        if (!row.key)
            continue;
        const AnimationTrack *t = Animation::track(*e, row.component, row.property);
        const auto state = !t ? KeyButton::State::None
                           : Animation::keyAt(*t, frame) >= 0 ? KeyButton::State::Key
                                                                : KeyButton::State::Animated;
        row.key->setState(state);
        if (row.field) {
            static const char *names[] = { "", "animated", "key" };
            const QString anim = QString::fromLatin1(names[int(state)]);
            if (row.field->property("anim").toString() != anim) {
                row.field->setProperty("anim", anim);
                row.field->style()->unpolish(row.field); // the style sheet colors it by "anim"
                row.field->style()->polish(row.field);
            }
        }
    }
    m_updating = false;
}

void InspectorPanel::addKeyButton(QBoxLayout *layout, QWidget *parent, Row &row)
{
    const PropertyDesc &prop = components()[size_t(row.component)].properties[size_t(row.property)];
    auto *key = new KeyButton(parent);
    if (!Animation::animatable(prop)) {
        // A blank of the same size keeps the editors' right edges lined up.
        auto *blank = new QWidget(parent);
        blank->setFixedSize(key->sizeHint());
        delete key;
        layout->addWidget(blank);
        return;
    }
    const int ci = row.component, pi = row.property;
    connect(key, &KeyButton::clicked, this, [this, ci, pi] { toggleKey(ci, pi); });
    connect(key, &KeyButton::contextMenuRequested, this,
            [this, ci, pi](const QPoint &pos) { showKeyMenu(ci, pi, pos); });
    layout->addWidget(key);
    row.key = key;
}

void InspectorPanel::toggleKey(int component, int property)
{
    const Entity *e = m_scene->entity(m_shown);
    if (!e)
        return;
    const double frame = m_scene->frame();
    const AnimationTrack *t = Animation::track(*e, component, property);
    const QString name = components()[size_t(component)].properties[size_t(property)].name;
    if (t && Animation::keyAt(*t, frame) >= 0)
        m_undo->push(new SetAnimationCommand(m_scene, m_shown, Animation::withoutKey(e->animation, component, property, frame),
                                             QStringLiteral("Delete key of %1").arg(name)));
    else
        m_undo->push(new SetAnimationCommand(
            m_scene, m_shown,
            Animation::withKey(e->animation, component, property, frame, components()[size_t(component)].get(*e, property)),
            QStringLiteral("Insert key of %1").arg(name)));
}

void InspectorPanel::showKeyMenu(int component, int property, const QPoint &globalPos)
{
    const Entity *e = m_scene->entity(m_shown);
    if (!e)
        return;
    const EntityId id = m_shown;
    const double frame = m_scene->frame();
    const PropertyDesc &prop = components()[size_t(component)].properties[size_t(property)];
    const AnimationTrack *t = Animation::track(*e, component, property);
    const bool onKey = t && Animation::keyAt(*t, frame) >= 0;

    QMenu menu(this);
    menu.addAction(onKey ? QStringLiteral("Delete Key") : QStringLiteral("Insert Key"), this,
                   [this, component, property] { toggleKey(component, property); });
    if (t && Animation::interpolates(prop.type)) {
        // The interpolation of the key on this frame, or of the one before it.
        int current = 0;
        for (int k = 0; k < int(t->keys.size()); ++k)
            if (t->keys[size_t(k)].frame <= frame + 0.01)
                current = k;
        const Interpolation now = t->keys[size_t(current)].interpolation;
        QMenu *interp = menu.addMenu(QStringLiteral("Interpolation"));
        const std::pair<Interpolation, const char *> choices[] = {
            { Interpolation::Constant, "Constant" }, { Interpolation::Linear, "Linear" }, { Interpolation::Bezier, "Bezier" }
        };
        for (const auto &[mode, label] : choices) {
            const Interpolation m = mode;
            QAction *a = interp->addAction(QString::fromLatin1(label), this, [this, id, component, property, frame, m] {
                if (const Entity *en = m_scene->entity(id))
                    m_undo->push(new SetAnimationCommand(
                        m_scene, id, Animation::withInterpolation(en->animation, component, property, frame, m),
                        QStringLiteral("Change interpolation")));
            });
            a->setCheckable(true);
            a->setChecked(mode == now);
        }
    }
    if (t) {
        menu.addSeparator();
        const QString name = prop.name;
        menu.addAction(QStringLiteral("Clear Animation"), this, [this, id, component, property, name] {
            if (const Entity *en = m_scene->entity(id))
                m_undo->push(new SetAnimationCommand(m_scene, id, Animation::withoutTrack(en->animation, component, property),
                                                     QStringLiteral("Clear animation of %1").arg(name)));
        });
    }
    menu.exec(globalPos);
}

void InspectorPanel::edit(int component, int property, const QVariant &value)
{
    if (m_updating || m_shown == InvalidEntity)
        return;
    m_undo->push(new SetPropertyCommand(m_scene, m_shown, component, property, value));
}
