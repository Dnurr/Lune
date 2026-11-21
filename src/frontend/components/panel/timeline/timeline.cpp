#include "timeline.h"

#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSpinBox>
#include <QToolButton>
#include <QUndoStack>
#include <cmath>

#include "engine/scene/animation.h"
#include "engine/scene/scene.h"
#include "frontend/components/widgets/draglabel.h"
#include "frontend/core/commands.h"
#include "frontend/core/playback.h"
#include "frontend/core/selection.h"
#include "frontend/theme/icons.h"
#include "frontend/theme/theme.h"

namespace
{
constexpr int kRulerPad = 12; // pixels between the ruler's ends and the first / last frame

// Playback button icons. An SVG in assets/icons with the same name wins (Icons); otherwise
// they're drawn here, tinted like the SVG icons (normal, hovered, checked).
enum class Glyph
{
    Start,
    StepBack,
    Play,
    Pause,
    StepForward,
    End,
};

QPixmap drawGlyph(Glyph glyph, const QColor &color, qreal dpr)
{
    const int s = 16;
    QPixmap pm(QSize(s, s) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    auto triangle = [&](qreal x0, qreal x1) { // pointing from x0 to x1, vertically centered
        QPainterPath path;
        path.moveTo(x0, 3.5);
        path.lineTo(x1, 8.0);
        path.lineTo(x0, 12.5);
        path.closeSubpath();
        p.drawPath(path);
    };
    switch (glyph) {
    case Glyph::Play: triangle(4.5, 12.5); break;
    case Glyph::Pause:
        p.drawRoundedRect(QRectF(4.0, 3.5, 3.0, 9.0), 0.8, 0.8);
        p.drawRoundedRect(QRectF(9.0, 3.5, 3.0, 9.0), 0.8, 0.8);
        break;
    case Glyph::StepForward: triangle(4.5, 10.5); p.drawRect(QRectF(10.5, 3.5, 1.8, 9.0)); break;
    case Glyph::StepBack: triangle(11.5, 5.5); p.drawRect(QRectF(3.7, 3.5, 1.8, 9.0)); break;
    case Glyph::End: triangle(2.5, 7.5); triangle(7.5, 12.5); p.drawRect(QRectF(12.5, 3.5, 1.8, 9.0)); break;
    case Glyph::Start: triangle(13.5, 8.5); triangle(8.5, 3.5); p.drawRect(QRectF(1.7, 3.5, 1.8, 9.0)); break;
    }
    return pm;
}

QIcon glyphIcon(const char *name, Glyph glyph, qreal dpr)
{
    if (Icons::exists(QString::fromLatin1(name)))
        return Icons::get(QString::fromLatin1(name));
    const Theme::Palette &c = Theme::currentPalette();
    QIcon icon;
    icon.addPixmap(drawGlyph(glyph, c.onSurfaceVariant, dpr), QIcon::Normal, QIcon::Off);
    icon.addPixmap(drawGlyph(glyph, c.onSurface, dpr), QIcon::Active, QIcon::Off);
    icon.addPixmap(drawGlyph(glyph, c.onSecondaryContainer, dpr), QIcon::Normal, QIcon::On);
    icon.addPixmap(drawGlyph(glyph, c.onSecondaryContainer, dpr), QIcon::Active, QIcon::On);
    return icon;
}

// Spin boxes take the wheel only once focused; otherwise it goes on to the panel.
class WheelGuard : public QObject
{
public:
    using QObject::QObject;
    static void install(QWidget *w)
    {
        static WheelGuard guard;
        w->setFocusPolicy(Qt::StrongFocus);
        w->installEventFilter(&guard);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *w = qobject_cast<QWidget *>(watched);
        if (event->type() == QEvent::Wheel && w && !w->hasFocus() && w->parentWidget()) {
            QCoreApplication::sendEvent(w->parentWidget(), event);
            return true;
        }
        return QObject::eventFilter(watched, event);
    }
};

// Tick spacing (in frames) for numbered ticks at least `minPixels` apart: 1, 2, 5, 10, 20...
int tickStep(double pixelsPerFrame, double minPixels)
{
    for (int decade = 1;; decade *= 10) {
        for (int m : { 1, 2, 5 }) {
            if (m * decade * pixelsPerFrame >= minPixels || decade > 100000)
                return m * decade;
        }
    }
}
} // namespace

// ---------------------------------------------------------------------------
// TimelineRuler
// ---------------------------------------------------------------------------

TimelineRuler::TimelineRuler(Scene *scene, Playback *playback, Selection *selection, QWidget *parent)
    : QWidget(parent)
    , m_scene(scene)
    , m_playback(playback)
    , m_selection(selection)
{
    setObjectName("TimelineRuler");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::SizeHorCursor);
    connect(playback, &Playback::frameChanged, this, qOverload<>(&QWidget::update));
    connect(scene, &Scene::timelineChanged, this, qOverload<>(&QWidget::update));
    connect(scene, &Scene::entityChanged, this, qOverload<>(&QWidget::update)); // its keys
    connect(selection, &Selection::changed, this, qOverload<>(&QWidget::update));
}

double TimelineRuler::xOf(double frame) const
{
    const Timeline &t = m_scene->timeline();
    const double span = qMax(1, t.end - t.start);
    return kRulerPad + (frame - t.start) / span * qMax(1, width() - 2 * kRulerPad);
}

int TimelineRuler::frameAt(int x) const
{
    const Timeline &t = m_scene->timeline();
    const double span = qMax(1, t.end - t.start);
    const double f = t.start + (x - kRulerPad) / double(qMax(1, width() - 2 * kRulerPad)) * span;
    return qBound(t.start, int(std::lround(f)), t.end);
}

void TimelineRuler::setCachedFrames(const QList<QPair<int, int>> &ranges)
{
    if (ranges == m_cached)
        return;
    m_cached = ranges;
    update();
}

void TimelineRuler::paintEvent(QPaintEvent *)
{
    const Timeline &t = m_scene->timeline();
    const Theme::Palette &c = Theme::currentPalette();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Track: a faint band behind the ticks.
    QColor track = c.onSurface;
    track.setAlphaF(0.06f);
    p.setPen(Qt::NoPen);
    p.setBrush(track);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);

    const double pxPerFrame = (width() - 2.0 * kRulerPad) / qMax(1, t.end - t.start);

    // The fluid cache: a band along the bottom under the frames shown without simulating (half
    // a frame on each side, so one frame is a sliver and a run is one bar).
    if (!m_cached.isEmpty()) {
        QColor band = c.primary;
        band.setAlphaF(0.55f);
        p.setBrush(band);
        for (const auto &[first, last] : m_cached) {
            if (last < t.start || first > t.end)
                continue;
            const double x0 = xOf(qMax(first, t.start)) - pxPerFrame * 0.5;
            const double x1 = xOf(qMin(last, t.end)) + pxPerFrame * 0.5;
            p.drawRoundedRect(QRectF(x0, height() - 4.0, qMax(2.0, x1 - x0), 3.0), 1.5, 1.5);
        }
        p.setBrush(Qt::NoBrush);
    }
    const int major = tickStep(pxPerFrame, 52.0);
    int minor = 0; // ticks without numbers between the numbered ones
    for (int candidate : { major / 5, major / 2, 1 }) {
        if (candidate >= 1 && major % candidate == 0 && candidate * pxPerFrame >= 6.0) {
            minor = candidate;
            break;
        }
    }

    const QFontMetrics fm(font());
    QColor tickColor = c.onSurfaceVariant;
    tickColor.setAlphaF(0.55f);
    QColor minorColor = c.onSurfaceVariant;
    minorColor.setAlphaF(0.28f);
    const int h = height();
    const int step = minor > 0 ? minor : major;
    // Ticks on multiples of the step (frame 0, 10, 20... rather than the start's offsets).
    int first = int(std::ceil(double(t.start) / step)) * step;
    for (int f = first; f <= t.end; f += step) {
        const double x = std::round(xOf(f)) + 0.5;
        const bool numbered = f % major == 0;
        p.setPen(QPen(numbered ? tickColor : minorColor, 1.0));
        p.drawLine(QPointF(x, h - (numbered ? 11.0 : 6.0)), QPointF(x, h - 2.0));
        if (numbered) {
            p.setPen(c.onSurfaceVariant);
            const QString label = QString::number(f);
            p.drawText(QPointF(x - fm.horizontalAdvance(label) / 2.0, fm.ascent() + 3.0), label);
        }
    }

    // Keyframes of the selected entity, on the ticks' line.
    if (const Entity *e = m_scene->entity(m_selection->current())) {
        const double current = m_playback->frame();
        const double y = h - 9.0;
        const double r = 4.5;
        for (double f : Animation::keyFrames(*e)) {
            if (f < t.start || f > t.end)
                continue;
            const double x = std::round(xOf(f)) + 0.5;
            const bool here = std::abs(f - current) < 0.01;
            QPainterPath diamond;
            diamond.moveTo(x, y - r);
            diamond.lineTo(x + r, y);
            diamond.lineTo(x, y + r);
            diamond.lineTo(x - r, y);
            diamond.closeSubpath();
            QColor fill = here ? QColor(0xE3, 0xB3, 0x41) : c.onSurface; // KeyButton::keyColor()
            if (!here)
                fill.setAlphaF(0.85f);
            QColor edge = c.surface;
            edge.setAlphaF(0.8f);
            p.setPen(QPen(edge, 1.0));
            p.setBrush(fill);
            p.drawPath(diamond);
        }
    }

    // Current frame: a line through the ruler and its number in a tag at the top.
    const double fx = std::round(xOf(m_playback->frame())) + 0.5;
    p.setPen(QPen(c.primary, 2.0));
    p.drawLine(QPointF(fx, 2.0), QPointF(fx, h - 2.0));
    const QString label = QString::number(std::lround(m_playback->frame()));
    const double tagW = fm.horizontalAdvance(label) + 12.0;
    const double tagH = fm.height() + 2.0;
    const double tagX = qBound(1.0, fx - tagW / 2.0, width() - tagW - 1.0);
    p.setPen(Qt::NoPen);
    p.setBrush(c.primary);
    p.drawRoundedRect(QRectF(tagX, 1.0, tagW, tagH), 5, 5);
    p.setPen(c.onPrimary);
    p.drawText(QRectF(tagX, 1.0, tagW, tagH), Qt::AlignCenter, label);
}

void TimelineRuler::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        event->accept();
        return;
    }
    m_scrubbing = true;
    m_playback->pause();
    m_playback->setFrame(frameAt(event->position().toPoint().x()));
    event->accept();
}

void TimelineRuler::mouseMoveEvent(QMouseEvent *event)
{
    if (m_scrubbing)
        m_playback->setFrame(frameAt(event->position().toPoint().x()));
    event->accept();
}

void TimelineRuler::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_scrubbing = false;
    event->accept();
}

// ---------------------------------------------------------------------------
// TimelinePanel
// ---------------------------------------------------------------------------

TimelinePanel::TimelinePanel(Scene *scene, Playback *playback, Selection *selection, QUndoStack *undo, QWidget *parent)
    : Panel(QStringLiteral("Timeline"), parent, Panel::Side::Footer)
    , m_scene(scene)
    , m_playback(playback)
    , m_undo(undo)
{
    setObjectName("TimelinePanel");
    setTitleVisible(false);
    setContentFills(true);
    setPanelHeight(56);

    auto *row = new QWidget;
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    const qreal dpr = devicePixelRatioF();
    auto button = [&](const char *icon, Glyph glyph, const QString &tip) {
        auto *b = new QToolButton(row);
        b->setObjectName("TimelineButton");
        b->setIcon(glyphIcon(icon, glyph, dpr));
        b->setIconSize(QSize(16, 16));
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(b);
        return b;
    };
    connect(button("timeline-start", Glyph::Start, tr("Go to the start (Shift+Left)")), &QToolButton::clicked,
            playback, &Playback::goToStart);
    connect(button("timeline-step-back", Glyph::StepBack, tr("Previous frame (Left)")), &QToolButton::clicked, this,
            [this] { m_playback->step(-1); });
    m_play = button("timeline-play", Glyph::Play, tr("Play / pause (Space)"));
    m_play->setCheckable(true);
    connect(m_play, &QToolButton::clicked, playback, &Playback::togglePlaying);
    connect(button("timeline-step-forward", Glyph::StepForward, tr("Next frame (Right)")), &QToolButton::clicked, this,
            [this] { m_playback->step(1); });
    connect(button("timeline-end", Glyph::End, tr("Go to the end (Shift+Right)")), &QToolButton::clicked, playback,
            &Playback::goToEnd);

    m_frame = new QSpinBox(row);
    m_frame->setObjectName("TimelineFrame");
    m_frame->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_frame->setAlignment(Qt::AlignCenter);
    m_frame->setFixedWidth(60);
    m_frame->setToolTip(tr("Current frame"));
    m_frame->setKeyboardTracking(false);
    WheelGuard::install(m_frame);
    connect(m_frame, &QSpinBox::valueChanged, this, [this](int f) { m_playback->setFrame(f); });
    layout->addSpacing(6);
    layout->addWidget(m_frame);
    layout->addSpacing(6);

    m_ruler = new TimelineRuler(scene, playback, selection, row);
    layout->addWidget(m_ruler, 1);
    layout->addSpacing(10);

    // Range and rate: labels scrub (drag), fields take typed values.
    auto field = [&](const QString &name, QAbstractSpinBox *spin, double step) {
        auto *label = new DragLabel(name, row);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setAlignment(Qt::AlignCenter);
        spin->setKeyboardTracking(false);
        WheelGuard::install(spin);
        connect(label, &DragLabel::dragged, spin, [spin, step](int dx, Qt::KeyboardModifiers mods) {
            if (auto *i = qobject_cast<QSpinBox *>(spin))
                i->setValue(i->value() + int(std::lround(dx * step * dragFactor(mods))));
            else if (auto *d = qobject_cast<QDoubleSpinBox *>(spin))
                d->setValue(d->value() + dx * step * dragFactor(mods));
        });
        layout->addWidget(label);
        layout->addWidget(spin);
        layout->addSpacing(6);
    };
    m_start = new QSpinBox(row);
    m_start->setRange(-100000, 100000);
    m_start->setFixedWidth(64);
    field(tr("Start"), m_start, 0.5);
    m_end = new QSpinBox(row);
    m_end->setRange(-100000, 100000);
    m_end->setFixedWidth(64);
    field(tr("End"), m_end, 0.5);
    m_fps = new QDoubleSpinBox(row);
    m_fps->setRange(Timeline::MinFps, Timeline::MaxFps);
    m_fps->setDecimals(2);
    m_fps->setFixedWidth(64);
    field(tr("FPS"), m_fps, 0.1);

    connect(m_start, &QSpinBox::valueChanged, this,
            [this](int v) { editTimeline(v, qMax(v, m_scene->timeline().end), m_scene->timeline().fps); });
    connect(m_end, &QSpinBox::valueChanged, this,
            [this](int v) { editTimeline(qMin(v, m_scene->timeline().start), v, m_scene->timeline().fps); });
    connect(m_fps, &QDoubleSpinBox::valueChanged, this,
            [this](double v) { editTimeline(m_scene->timeline().start, m_scene->timeline().end, v); });

    contentLayout()->addWidget(row);

    connect(scene, &Scene::timelineChanged, this, &TimelinePanel::syncTimeline);
    connect(playback, &Playback::frameChanged, this, &TimelinePanel::syncFrame);
    connect(playback, &Playback::playingChanged, this, &TimelinePanel::syncPlaying);
    syncTimeline();
    syncFrame();
    syncPlaying();
}

void TimelinePanel::editTimeline(int start, int end, double fps)
{
    Timeline t = m_scene->timeline();
    t.start = start;
    t.end = end;
    t.fps = fps;
    if (t.valid() != m_scene->timeline())
        m_undo->push(new SetTimelineCommand(m_scene, t));
}

void TimelinePanel::syncTimeline()
{
    const Timeline &t = m_scene->timeline();
    const QSignalBlocker b1(m_start), b2(m_end), b3(m_fps), b4(m_frame);
    m_start->setValue(t.start);
    m_end->setValue(t.end);
    m_fps->setValue(t.fps);
    m_frame->setRange(t.start, t.end);
    m_frame->setValue(int(std::lround(m_playback->frame())));
}

void TimelinePanel::setCachedFrames(const QList<QPair<int, int>> &ranges)
{
    m_ruler->setCachedFrames(ranges);
}

void TimelinePanel::syncFrame()
{
    const QSignalBlocker block(m_frame);
    m_frame->setValue(int(std::lround(m_playback->frame())));
}

void TimelinePanel::syncPlaying()
{
    const bool playing = m_playback->isPlaying();
    m_play->setChecked(playing);
    m_play->setIcon(playing ? glyphIcon("timeline-pause", Glyph::Pause, devicePixelRatioF())
                            : glyphIcon("timeline-play", Glyph::Play, devicePixelRatioF()));
}
