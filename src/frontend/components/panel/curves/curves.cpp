#include "curves.h"

#include <QContextMenuEvent>
#include <QHash>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QUndoStack>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "engine/scene/animation.h"
#include "engine/scene/scene.h"
#include "frontend/core/commands.h"
#include "frontend/core/selection.h"
#include "frontend/theme/theme.h"

namespace
{
constexpr double kRulerHeight = 20.0; // frame numbers at the top (scrub there)
constexpr double kPickRadius = 7.0;   // pixels around a key or handle end that grab it

// 1, 2, 5 x 10^n: the smallest step at least `minimum` long.
double niceStep(double minimum)
{
    if (minimum <= 0.0)
        return 1.0;
    const double decade = std::pow(10.0, std::floor(std::log10(minimum)));
    for (double m : { 1.0, 2.0, 5.0, 10.0 })
        if (m * decade >= minimum)
            return m * decade;
    return 10.0 * decade;
}

QString numberText(double v, double step)
{
    const int decimals = step >= 1.0 ? 0 : qBound(0, int(std::ceil(-std::log10(step))), 4);
    return QString::number(v, 'f', decimals);
}

QColor channelColor(PropertyType type, int index, int track)
{
    static const QColor xyz[] = { QColor(0xE5, 0x53, 0x4B), QColor(0x57, 0xAB, 0x5A), QColor(0x53, 0x9B, 0xF5),
                                  QColor(0xB0, 0xB0, 0xB0) };
    static const QColor others[] = { QColor(0xE3, 0xB3, 0x41), QColor(0x4F, 0xC1, 0xC9), QColor(0xC2, 0x7B, 0xE0),
                                     QColor(0xF0, 0x88, 0x3E) };
    if (type == PropertyType::Vector3 || type == PropertyType::Color)
        return xyz[qBound(0, index, 3)];
    return others[track % 4];
}

QString channelName(const AnimationTrack &t, PropertyType type, int index)
{
    QString name = t.component == QLatin1String("Transform") ? t.property : t.component + QStringLiteral(" · ") + t.property;
    if (type == PropertyType::Vector3)
        name += QStringLiteral(" ") + QString(QChar(u"XYZ"[index]));
    else if (type == PropertyType::Color)
        name += QStringLiteral(" ") + QString(QChar(u"RGBA"[index]));
    return name;
}
} // namespace

// ---------------------------------------------------------------------------
// CurveView
// ---------------------------------------------------------------------------

CurveView::CurveView(Scene *scene, QUndoStack *undo, QWidget *parent)
    : QWidget(parent)
    , m_scene(scene)
    , m_undo(undo)
{
    setObjectName("CurveView");
    setFocusPolicy(Qt::ClickFocus);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    connect(scene, &Scene::frameChanged, this, qOverload<>(&QWidget::update));
    connect(scene, &Scene::timelineChanged, this, qOverload<>(&QWidget::update));
    connect(scene, &Scene::entityChanged, this, [this](EntityId id) {
        if (id == m_entity) {
            dropInvalidSelection();
            update();
        }
    });
}

const Entity *CurveView::entity() const
{
    return m_scene->entity(m_entity);
}

void CurveView::setChannels(EntityId entity, std::vector<CurveChannel> channels)
{
    if (entity != m_entity)
        m_selection.clear();
    m_entity = entity;
    m_channels = std::move(channels);
    dropInvalidSelection();
    update();
}

void CurveView::setChannelVisible(int channel, bool visible)
{
    if (channel < 0 || channel >= int(m_channels.size()))
        return;
    m_channels[size_t(channel)].visible = visible;
    std::erase_if(m_selection, [&](const KeyRef &k) {
        const CurveChannel *c = channelOf(k);
        return !c || !c->visible;
    });
    update();
}

QRectF CurveView::plotRect() const
{
    return QRectF(0, kRulerHeight, width(), qMax(1.0, height() - kRulerHeight));
}

double CurveView::xOf(double frame) const
{
    return (frame - m_f0) / (m_f1 - m_f0) * width();
}

double CurveView::yOf(double value) const
{
    const QRectF r = plotRect();
    return r.top() + (m_v1 - value) / (m_v1 - m_v0) * r.height();
}

double CurveView::frameAt(double x) const
{
    return m_f0 + x / qMax(1, width()) * (m_f1 - m_f0);
}

double CurveView::valueAt(double y) const
{
    const QRectF r = plotRect();
    return m_v1 - (y - r.top()) / r.height() * (m_v1 - m_v0);
}

const CurveChannel *CurveView::channelOf(const KeyRef &k) const
{
    for (const CurveChannel &c : m_channels)
        if (c.track == k.track && c.index == k.index)
            return &c;
    return nullptr;
}

bool CurveView::isSelected(const KeyRef &k) const
{
    return std::find(m_selection.begin(), m_selection.end(), k) != m_selection.end();
}

void CurveView::dropInvalidSelection()
{
    const Entity *e = entity();
    std::erase_if(m_selection, [&](const KeyRef &k) {
        return !e || k.track >= int(e->animation.size()) || k.key >= int(e->animation[size_t(k.track)].keys.size())
            || !channelOf(k);
    });
}

QPointF CurveView::keyPos(const Entity &e, const KeyRef &k) const
{
    const CurveChannel *c = channelOf(k);
    const Keyframe &key = e.animation[size_t(k.track)].keys[size_t(k.key)];
    return { xOf(key.frame), yOf(Animation::channel(c->type, key.value, k.index)) };
}

std::optional<QPointF> CurveView::handlePos(const Entity &e, const KeyRef &k, int side) const
{
    const CurveChannel *c = channelOf(k);
    if (!c || !Animation::interpolates(c->type))
        return std::nullopt;
    const AnimationTrack &t = e.animation[size_t(k.track)];
    const auto &keys = t.keys;
    // A handle where the curve on that side is Bezier: the left one shapes the segment from the
    // previous key (its interpolation), the right one the segment to the next.
    const int neighbor = k.key + side;
    if (neighbor < 0 || neighbor >= int(keys.size()))
        return std::nullopt;
    const Interpolation segment = side > 0 ? keys[size_t(k.key)].interpolation : keys[size_t(neighbor)].interpolation;
    if (segment != Interpolation::Bezier)
        return std::nullopt;
    const double frame = keys[size_t(k.key)].frame;
    const double length = std::abs(keys[size_t(neighbor)].frame - frame) / 3.0; // as in a cubic Bezier
    const double slope = Animation::tangent(t, c->type, k.key, k.index);
    const double value = Animation::channel(c->type, keys[size_t(k.key)].value, k.index);
    return QPointF(xOf(frame + side * length), yOf(value + side * length * slope));
}

std::optional<CurveView::KeyRef> CurveView::keyUnder(const QPointF &pos) const
{
    const Entity *e = entity();
    if (!e)
        return std::nullopt;
    std::optional<KeyRef> best;
    double bestDistance = kPickRadius;
    for (const CurveChannel &c : m_channels) {
        if (!c.visible || c.track >= int(e->animation.size()))
            continue;
        for (int k = 0; k < int(e->animation[size_t(c.track)].keys.size()); ++k) {
            const KeyRef ref { c.track, k, c.index };
            const QPointF d = keyPos(*e, ref) - pos;
            const double distance = std::hypot(d.x(), d.y());
            if (distance <= bestDistance) {
                bestDistance = distance;
                best = ref;
            }
        }
    }
    return best;
}

bool CurveView::handleUnder(const QPointF &pos, KeyRef *key, int *side) const
{
    const Entity *e = entity();
    if (!e)
        return false;
    for (const KeyRef &k : m_selection) {
        for (int s : { -1, 1 }) {
            if (const auto h = handlePos(*e, k, s)) {
                const QPointF d = *h - pos;
                if (std::hypot(d.x(), d.y()) <= kPickRadius) {
                    *key = k;
                    *side = s;
                    return true;
                }
            }
        }
    }
    return false;
}

void CurveView::frameAll()
{
    const Entity *e = entity();
    const Timeline &tl = m_scene->timeline();
    double f0 = 1e30, f1 = -1e30, v0 = 1e30, v1 = -1e30;
    if (e) {
        for (const CurveChannel &c : m_channels) {
            if (!c.visible || c.track >= int(e->animation.size()))
                continue;
            const AnimationTrack &t = e->animation[size_t(c.track)];
            for (const Keyframe &k : t.keys) {
                f0 = std::min(f0, k.frame);
                f1 = std::max(f1, k.frame);
            }
            if (t.keys.empty())
                continue;
            // The curve between the keys too (hand tangents can go past them).
            const double a = t.keys.front().frame, b = t.keys.back().frame;
            const int samples = 64;
            for (int i = 0; i <= samples; ++i) {
                const double f = a + (b - a) * i / samples;
                const double v = Animation::channel(c.type, Animation::evaluate(t, c.type, f), c.index);
                v0 = std::min(v0, v);
                v1 = std::max(v1, v);
            }
        }
    }
    if (f0 > f1) { // nothing to show: the timeline's range, 0..1
        f0 = tl.start;
        f1 = tl.end;
        v0 = 0.0;
        v1 = 1.0;
    }
    if (f1 - f0 < 10.0) {
        const double mid = (f0 + f1) * 0.5;
        f0 = mid - 10.0;
        f1 = mid + 10.0;
    }
    if (v1 - v0 < 1e-3) {
        v0 -= 0.5;
        v1 += 0.5;
    }
    const double fm = (f1 - f0) * 0.06, vm = (v1 - v0) * 0.12;
    m_f0 = f0 - fm;
    m_f1 = f1 + fm;
    m_v0 = v0 - vm;
    m_v1 = v1 + vm;
    update();
}

void CurveView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
}

void CurveView::paintEvent(QPaintEvent *)
{
    const Theme::Palette &pal = Theme::currentPalette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF plot = plotRect();
    const QFontMetrics fm(font());

    // Plot background, darker outside the timeline's range.
    QColor bg = pal.surfaceContainerLowest;
    bg.setAlphaF(0.55f);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    const Timeline &tl = m_scene->timeline();
    QColor outside = Qt::black;
    outside.setAlphaF(0.18f);
    p.setBrush(outside);
    p.drawRect(QRectF(QPointF(0, plot.top()), QPointF(std::max(0.0, xOf(tl.start)), plot.bottom())));
    p.drawRect(QRectF(QPointF(std::min(double(width()), xOf(tl.end)), plot.top()), QPointF(width(), plot.bottom())));

    // Grid: frames (vertical) and values (horizontal), with numbers.
    QColor grid = pal.onSurface;
    grid.setAlphaF(0.07f);
    QColor gridStrong = pal.onSurface;
    gridStrong.setAlphaF(0.16f);
    QColor label = pal.onSurfaceVariant;
    const double frameStep = std::max(1.0, niceStep((m_f1 - m_f0) / std::max(1, width()) * 60.0));
    p.setFont(font());
    for (double f = std::ceil(m_f0 / frameStep) * frameStep; f <= m_f1; f += frameStep) {
        const double x = std::round(xOf(f)) + 0.5;
        p.setPen(QPen(grid, 1));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        p.setPen(label);
        const QString text = QString::number(std::lround(f));
        p.drawText(QPointF(x - fm.horizontalAdvance(text) / 2.0, fm.ascent() + 3.0), text);
    }
    const double valueStep = niceStep((m_v1 - m_v0) / std::max(1.0, plot.height()) * 36.0);
    for (double v = std::ceil(m_v0 / valueStep) * valueStep; v <= m_v1; v += valueStep) {
        const double y = std::round(yOf(v)) + 0.5;
        p.setPen(QPen(std::abs(v) < valueStep * 0.5 ? gridStrong : grid, 1));
        p.drawLine(QPointF(0, y), QPointF(width(), y));
        p.setPen(label);
        p.drawText(QPointF(6, y - 3), numberText(std::abs(v) < valueStep * 0.5 ? 0.0 : v, valueStep));
    }

    // Current frame.
    const double cx = std::round(xOf(m_scene->frame())) + 0.5;
    p.setPen(QPen(pal.primary, 1.5));
    p.drawLine(QPointF(cx, 0), QPointF(cx, height()));

    const Entity *e = entity();
    if (!e) {
        p.setPen(pal.onSurfaceVariant);
        p.drawText(plot, Qt::AlignCenter, tr("Select an animated entity: its curves show here.\n"
                                             "Animate a property with the diamond at the end of its Inspector row."));
        return;
    }
    if (m_channels.empty()) {
        p.setPen(pal.onSurfaceVariant);
        p.drawText(plot, Qt::AlignCenter, tr("No animation: insert keys with the diamonds in the Inspector."));
        return;
    }

    // Curves.
    p.save();
    p.setClipRect(plot);
    for (const CurveChannel &c : m_channels) {
        if (!c.visible || c.track >= int(e->animation.size()))
            continue;
        const AnimationTrack &t = e->animation[size_t(c.track)];
        QPainterPath path;
        for (int x = 0; x <= width(); x += 2) {
            const double v = Animation::channel(c.type, Animation::evaluate(t, c.type, frameAt(x)), c.index);
            const QPointF pt(x, yOf(v));
            if (x == 0)
                path.moveTo(pt);
            else
                path.lineTo(pt);
        }
        QColor col = c.color;
        p.setPen(QPen(col, 1.6));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    p.restore();

    // Handles of the selected keys, then every key.
    const QColor selectedColor(0xE3, 0xB3, 0x41); // KeyButton::keyColor()
    for (const KeyRef &k : m_selection) {
        const QPointF kp = keyPos(*e, k);
        for (int side : { -1, 1 }) {
            if (const auto h = handlePos(*e, k, side)) {
                QColor line = pal.onSurface;
                line.setAlphaF(0.55f);
                p.setPen(QPen(line, 1));
                p.drawLine(kp, *h);
                p.setPen(QPen(pal.surface, 1));
                p.setBrush(pal.onSurface);
                p.drawEllipse(*h, 3.5, 3.5);
            }
        }
    }
    for (const CurveChannel &c : m_channels) {
        if (!c.visible || c.track >= int(e->animation.size()))
            continue;
        for (int k = 0; k < int(e->animation[size_t(c.track)].keys.size()); ++k) {
            const KeyRef ref { c.track, k, c.index };
            const QPointF kp = keyPos(*e, ref);
            const bool sel = isSelected(ref);
            p.setPen(QPen(sel ? selectedColor : pal.surface, 1.2));
            p.setBrush(sel ? selectedColor : c.color);
            const double r = sel ? 4.5 : 3.8;
            p.drawRect(QRectF(kp.x() - r, kp.y() - r, 2 * r, 2 * r));
        }
    }

    // Box selection.
    if (m_drag == Drag::Box) {
        QColor fill = pal.primary;
        fill.setAlphaF(0.12f);
        p.setPen(QPen(pal.primary, 1, Qt::DashLine));
        p.setBrush(fill);
        p.drawRect(m_box.normalized());
    }
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

std::vector<AnimationTrack> CurveView::movedKeys(double frames, double value, std::vector<KeyRef> *selection) const
{
    std::vector<AnimationTrack> anim = m_dragStart.animation;
    std::vector<KeyRef> newSelection;
    for (int ti = 0; ti < int(anim.size()); ++ti) {
        auto &keys = anim[size_t(ti)].keys;
        const CurveChannel *any = nullptr;
        for (const CurveChannel &c : m_channels)
            if (c.track == ti)
                any = &c;
        if (!any)
            continue;
        const PropertyType type = any->type;
        // Which keys move, and which of their numbers change value.
        struct Item
        {
            Keyframe key;
            bool moved = false;
            std::vector<int> indices; // selected numbers of this key
        };
        std::vector<Item> items;
        for (int k = 0; k < int(keys.size()); ++k) {
            Item item { keys[size_t(k)], false, {} };
            for (const KeyRef &s : m_dragSelection)
                if (s.track == ti && s.key == k) {
                    item.moved = true;
                    item.indices.push_back(s.index);
                }
            if (item.moved) {
                item.key.frame = std::round(item.key.frame + frames);
                if (Animation::interpolates(type))
                    for (int index : item.indices)
                        item.key.value = Animation::withChannel(
                            type, item.key.value, index, Animation::channel(type, item.key.value, index) + value);
            }
            items.push_back(std::move(item));
        }
        // A moved key replaces a key it lands on.
        std::erase_if(items, [&](const Item &it) {
            if (it.moved)
                return false;
            for (const Item &m : items)
                if (m.moved && std::abs(m.key.frame - it.key.frame) < 0.01)
                    return true;
            return false;
        });
        std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.key.frame < b.key.frame; });
        keys.clear();
        for (int k = 0; k < int(items.size()); ++k) {
            keys.push_back(items[size_t(k)].key);
            for (int index : items[size_t(k)].indices)
                newSelection.push_back({ ti, k, index });
        }
    }
    if (selection)
        *selection = newSelection;
    return anim;
}

std::vector<AnimationTrack> CurveView::withHandle(const QPointF &pos) const
{
    std::vector<AnimationTrack> anim = m_dragStart.animation;
    const KeyRef &k = m_handleKey;
    const CurveChannel *c = channelOf(k);
    if (!c || k.track >= int(anim.size()) || k.key >= int(anim[size_t(k.track)].keys.size()))
        return anim;
    AnimationTrack &t = anim[size_t(k.track)];
    Keyframe &key = t.keys[size_t(k.key)];
    // The other numbers keep the tangents they have now.
    if (key.autoTangent) {
        for (int i = 0; i < Animation::channelCount(c->type); ++i)
            key.slope[size_t(i)] = Animation::tangent(t, c->type, k.key, i);
        key.autoTangent = false;
    }
    double df = frameAt(pos.x()) - key.frame;
    if (df * m_handleSide < 0.05)
        df = m_handleSide * 0.05; // the handle stays on its side
    const double dv = valueAt(pos.y()) - Animation::channel(c->type, key.value, k.index);
    key.slope[size_t(k.index)] = dv / df;
    return anim;
}

void CurveView::preview(const std::vector<AnimationTrack> &animation)
{
    const double frame = m_scene->frame();
    m_scene->modifyEntity(m_entity, [&](Entity &e) {
        e.animation = animation;
        Animation::applyTo(e, frame);
    });
    m_dragChanged = true;
}

void CurveView::commit(const QString &text)
{
    const Entity *e = entity();
    if (!e || !m_dragChanged)
        return;
    const std::vector<AnimationTrack> after = e->animation;
    if (after == m_dragStart.animation)
        return;
    m_undo->push(new SetAnimationCommand(m_scene, m_entity, m_dragStart, after, text));
}

void CurveView::edit(const std::vector<AnimationTrack> &animation, const QString &text)
{
    const Entity *e = entity();
    if (!e || animation == e->animation)
        return;
    m_undo->push(new SetAnimationCommand(m_scene, m_entity, animation, text));
}

void CurveView::deleteSelected()
{
    const Entity *e = entity();
    if (!e || m_selection.empty())
        return;
    std::vector<AnimationTrack> anim = e->animation;
    // Whole keys go (every number of a vector's key).
    for (int ti = int(anim.size()) - 1; ti >= 0; --ti) {
        auto &keys = anim[size_t(ti)].keys;
        for (int k = int(keys.size()) - 1; k >= 0; --k) {
            const bool selected = std::any_of(m_selection.begin(), m_selection.end(),
                                              [&](const KeyRef &s) { return s.track == ti && s.key == k; });
            if (selected)
                keys.erase(keys.begin() + k);
        }
        if (keys.empty())
            anim.erase(anim.begin() + ti);
    }
    m_selection.clear();
    edit(anim, tr("Delete keys"));
}

void CurveView::setInterpolation(Interpolation interpolation)
{
    const Entity *e = entity();
    if (!e)
        return;
    std::vector<AnimationTrack> anim = e->animation;
    for (const KeyRef &k : m_selection)
        anim[size_t(k.track)].keys[size_t(k.key)].interpolation = interpolation;
    edit(anim, tr("Change interpolation"));
}

void CurveView::setAutoTangents()
{
    const Entity *e = entity();
    if (!e)
        return;
    std::vector<AnimationTrack> anim = e->animation;
    for (const KeyRef &k : m_selection)
        anim[size_t(k.track)].keys[size_t(k.key)].autoTangent = true;
    edit(anim, tr("Automatic tangents"));
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool CurveView::event(QEvent *event)
{
    // Delete removes keys here, not the entity (the editor's shortcut).
    if (event->type() == QEvent::ShortcutOverride) {
        auto *key = static_cast<QKeyEvent *>(event);
        if ((key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) && !m_selection.empty()) {
            event->accept();
            return true;
        }
    }
    return QWidget::event(event);
}

void CurveView::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    m_pressPos = m_lastPos = pos;
    m_dragChanged = false;
    if (event->button() == Qt::MiddleButton) {
        m_drag = Drag::Pan;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton)
        return;
    if (pos.y() < kRulerHeight) {
        m_drag = Drag::Scrub;
        m_scene->setFrame(std::round(frameAt(pos.x())));
        return;
    }
    const Entity *e = entity();
    if (!e)
        return;
    KeyRef handle;
    int side = 1;
    if (handleUnder(pos, &handle, &side)) {
        m_drag = Drag::Handle;
        m_handleKey = handle;
        m_handleSide = side;
        m_dragStart = *e;
        return;
    }
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    if (const auto k = keyUnder(pos)) {
        if (shift) {
            if (isSelected(*k))
                std::erase(m_selection, *k);
            else
                m_selection.push_back(*k);
        } else if (!isSelected(*k)) {
            m_selection = { *k };
        }
        if (isSelected(*k)) {
            m_drag = Drag::Keys;
            m_dragStart = *e;
            m_dragSelection = m_selection;
        }
        update();
        return;
    }
    m_drag = Drag::Box;
    m_box = QRectF(pos, pos);
    if (!shift)
        m_selection.clear();
    update();
}

void CurveView::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();
    switch (m_drag) {
    case Drag::Pan: {
        const QPointF d = pos - m_lastPos;
        const double df = d.x() / qMax(1, width()) * (m_f1 - m_f0);
        const double dv = d.y() / plotRect().height() * (m_v1 - m_v0);
        m_f0 -= df;
        m_f1 -= df;
        m_v0 += dv;
        m_v1 += dv;
        update();
        break;
    }
    case Drag::Scrub: m_scene->setFrame(std::round(frameAt(pos.x()))); break;
    case Drag::Keys: {
        if ((pos - m_pressPos).manhattanLength() < 3 && !m_dragChanged)
            break;
        double frames = std::round(frameAt(pos.x()) - frameAt(m_pressPos.x()));
        double value = valueAt(pos.y()) - valueAt(m_pressPos.y());
        // Shift held after the press: one axis only, the one moved more.
        if (event->modifiers() & Qt::ShiftModifier) {
            if (std::abs(pos.x() - m_pressPos.x()) > std::abs(pos.y() - m_pressPos.y()))
                value = 0.0;
            else
                frames = 0.0;
        }
        std::vector<KeyRef> selection;
        preview(movedKeys(frames, value, &selection));
        m_selection = selection;
        break;
    }
    case Drag::Handle: preview(withHandle(pos)); break;
    case Drag::Box:
        m_box.setBottomRight(pos);
        update();
        break;
    case Drag::None: {
        KeyRef k;
        int side = 0;
        setCursor(handleUnder(pos, &k, &side) || keyUnder(pos) ? Qt::PointingHandCursor
                  : pos.y() < kRulerHeight                    ? Qt::SizeHorCursor
                                                              : Qt::ArrowCursor);
        break;
    }
    }
    m_lastPos = pos;
}

void CurveView::mouseReleaseEvent(QMouseEvent *event)
{
    const Drag drag = m_drag;
    m_drag = Drag::None;
    unsetCursor();
    switch (drag) {
    case Drag::Keys: commit(tr("Move keys")); break;
    case Drag::Handle: commit(tr("Change tangent")); break;
    case Drag::Box: {
        const Entity *e = entity();
        const QRectF box = m_box.normalized();
        if (e) {
            for (const CurveChannel &c : m_channels) {
                if (!c.visible || c.track >= int(e->animation.size()))
                    continue;
                for (int k = 0; k < int(e->animation[size_t(c.track)].keys.size()); ++k) {
                    const KeyRef ref { c.track, k, c.index };
                    if (box.contains(keyPos(*e, ref)) && !isSelected(ref))
                        m_selection.push_back(ref);
                }
            }
        }
        update();
        break;
    }
    default: break;
    }
    event->accept();
}

void CurveView::wheelEvent(QWheelEvent *event)
{
    const double factor = std::pow(0.999, event->angleDelta().y());
    const QPointF pos = event->position();
    const bool timeOnly = event->modifiers() & Qt::ControlModifier;
    const bool valuesOnly = event->modifiers() & Qt::ShiftModifier;
    if (!valuesOnly) {
        const double f = frameAt(pos.x());
        m_f0 = f + (m_f0 - f) * factor;
        m_f1 = f + (m_f1 - f) * factor;
    }
    if (!timeOnly) {
        const double v = valueAt(pos.y());
        m_v0 = v + (m_v0 - v) * factor;
        m_v1 = v + (m_v1 - v) * factor;
    }
    update();
    event->accept();
}

void CurveView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Delete:
    case Qt::Key_Backspace: deleteSelected(); break;
    case Qt::Key_Home:
    case Qt::Key_F: frameAll(); break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

void CurveView::contextMenuEvent(QContextMenuEvent *event)
{
    if (const auto k = keyUnder(event->pos()); k && !isSelected(*k)) {
        m_selection = { *k };
        update();
    }
    QMenu menu(this);
    const bool any = !m_selection.empty();
    QMenu *interp = menu.addMenu(tr("Interpolation"));
    interp->setEnabled(any);
    interp->addAction(tr("Constant"), this, [this] { setInterpolation(Interpolation::Constant); });
    interp->addAction(tr("Linear"), this, [this] { setInterpolation(Interpolation::Linear); });
    interp->addAction(tr("Bezier"), this, [this] { setInterpolation(Interpolation::Bezier); });
    menu.addAction(tr("Automatic Tangents"), this, &CurveView::setAutoTangents)->setEnabled(any);
    menu.addAction(tr("Delete Keys\tDelete"), this, &CurveView::deleteSelected)->setEnabled(any);
    menu.addSeparator();
    menu.addAction(tr("Frame All\tHome"), this, &CurveView::frameAll);
    menu.exec(event->globalPos());
}

// ---------------------------------------------------------------------------
// CurvesPanel
// ---------------------------------------------------------------------------

CurvesPanel::CurvesPanel(Scene *scene, Selection *selection, QUndoStack *undo, QWidget *parent)
    : Panel(QStringLiteral("Curves"), parent, Panel::Side::Bottom)
    , m_scene(scene)
    , m_selection(selection)
{
    setObjectName("CurvesPanel");
    setContentFills(true);

    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    m_list = new QListWidget(row);
    m_list->setObjectName("CurveChannels");
    m_list->setFixedWidth(190);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_view = new CurveView(scene, undo, row);
    layout->addWidget(m_list);
    layout->addWidget(m_view, 1);
    contentLayout()->addWidget(row);

    connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        m_view->setChannelVisible(m_list->row(item), item->checkState() == Qt::Checked);
    });
    connect(selection, &Selection::changed, this, [this] { rebuild(true); });
    connect(scene, &Scene::entityChanged, this, [this](EntityId id) {
        if (id == m_shown)
            rebuild(false);
    });
    rebuild(true);
}

void CurvesPanel::rebuild(bool frame)
{
    const Entity *e = m_scene->entity(m_selection->current());
    QStringList tracks;
    std::vector<CurveChannel> channels;
    if (e) {
        for (int ti = 0; ti < int(e->animation.size()); ++ti) {
            const AnimationTrack &t = e->animation[size_t(ti)];
            int ci = 0, pi = 0;
            if (!Animation::resolve(t, &ci, &pi))
                continue;
            const PropertyType type = components()[size_t(ci)].properties[size_t(pi)].type;
            tracks << t.component + QLatin1Char('/') + t.property;
            for (int i = 0; i < Animation::channelCount(type); ++i) {
                CurveChannel c;
                c.track = ti;
                c.index = i;
                c.component = ci;
                c.property = pi;
                c.type = type;
                c.name = channelName(t, type, i);
                c.color = channelColor(type, i, ti);
                channels.push_back(c);
            }
        }
    }
    const EntityId id = e ? e->id : InvalidEntity;
    if (id == m_shown && tracks == m_shownTracks && !frame) {
        // Same channels (only keys changed): keep the list as it is.
        return;
    }
    const bool newEntity = id != m_shown;
    // Keep the visibility of channels that stay.
    QHash<QString, bool> hidden;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->checkState() != Qt::Checked)
            hidden.insert(m_list->item(i)->text(), true);
    m_shown = id;
    m_shownTracks = tracks;

    const QSignalBlocker block(m_list);
    m_list->clear();
    for (CurveChannel &c : channels) {
        if (!newEntity && hidden.contains(c.name))
            c.visible = false;
        auto *item = new QListWidgetItem(c.name, m_list);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(c.visible ? Qt::Checked : Qt::Unchecked);
        item->setForeground(c.color);
    }
    m_view->setChannels(id, std::move(channels));
    if (frame || newEntity)
        m_view->frameAll();
}
