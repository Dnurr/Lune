#include "viewport.h"

#include <QChildEvent>
#include <QElapsedTimer>
#include <rhi/qrhi.h>
#include <QMouseEvent>
#include <QUndoStack>
#include <QWheelEvent>

#include "engine/scene/picking.h"
#include "engine/scene/scene.h"
#include "frontend/core/commands.h"
#include "frontend/core/displaysettings.h"
#include "frontend/core/selection.h"
#include "lighticons.h"
#include "statsoverlay.h"

Viewport::Viewport(QWidget *parent)
    : QRhiWidget(parent)
{
    setMouseTracking(true); // gizmo hover highlight
    // Clicking the viewport takes the keyboard focus away from text fields
    // (so Ctrl+Z undoes the scene, and the command palette closes).
    setFocusPolicy(Qt::ClickFocus);

    m_stats = new StatsOverlay(this);
}

Viewport::~Viewport() = default;

void Viewport::resize(int x, int y)
{
    QRhiWidget::resize(x, y);
}

void Viewport::update(std::function<void()> f)
{
    if (f)
    {
        f();
    }
    QRhiWidget::update();
}

void Viewport::redraw()
{
    QRhiWidget::update();
}

void Viewport::setEditorContext(Scene *scene, Selection *selection, QUndoStack *undoStack)
{
    m_scene = scene;
    m_selection = selection;
    m_undo = undoStack;

    // Anything that changes what's on screen schedules a new frame.
    connect(m_scene, &Scene::entityAdded, this, &Viewport::redraw);
    connect(m_scene, &Scene::entityRemoved, this, &Viewport::redraw);
    connect(m_scene, &Scene::entityChanged, this, &Viewport::redraw);
    connect(m_selection, &Selection::changed, this, [this] {
        if (m_gizmo.isDragging()) {
            m_gizmo.endDrag(); // the dragged entity went away (e.g. undo during the drag)
        }
        m_gizmo.setHovered(Gizmo::None);
        redraw();
    });
}

void Viewport::setCamera(const OrbitCamera &camera)
{
    m_camera = camera;
    redraw();
}

void Viewport::setDisplaySettings(DisplaySettings *settings)
{
    m_display = settings;
    connect(m_display, &DisplaySettings::changed, this, &Viewport::redraw);
    redraw();
}

void Viewport::setStatsVisible(bool visible)
{
    m_stats->setVisible(visible);
    if (visible)
        m_stats->raise();
    redraw(); // starts the continuous rendering (see render())
}

bool Viewport::isStatsVisible() const
{
    return m_stats->isVisible();
}

void Viewport::setGizmoMode(Gizmo::Mode mode)
{
    if (m_gizmo.isDragging())
        return;
    m_gizmo.setMode(mode);
    m_gizmo.setHovered(Gizmo::None);
    redraw();
}

// ---------------------------------------------------------------------------
// Rendering (delegated to the engine)
// ---------------------------------------------------------------------------

void Viewport::initialize(QRhiCommandBuffer *)
{
    // First frame, resize, or a new QRhi (e.g. the widget moved to another window).
    m_renderer.initialize(rhi(), renderTarget());
}

void Viewport::render(QRhiCommandBuffer *cb)
{
    QElapsedTimer cpu;
    cpu.start();
    updateGlassRects();

    std::vector<OverlayVertex> overlay;
    if (m_scene)
        LightIcons::append(*m_scene, m_camera, QSizeF(size()), m_selection ? m_selection->current() : InvalidEntity,
                           overlay);
    if (layoutGizmo()) // after the icons: drawn on top of them
        m_gizmo.appendGeometry(overlay);
    m_renderer.setOverlay(std::move(overlay), float(devicePixelRatio()));
    if (m_display)
        m_renderer.setDisplayParams(float(m_display->exposure()), m_display->tonemapper());

    const EntityId selected = m_selection ? m_selection->current() : InvalidEntity;
    m_renderer.render(cb, renderTarget(), m_camera, m_scene, selected);

    if (m_stats->isVisible()) {
        // CPU time to prepare and record this frame (the GPU work happens later).
        const double cpuMs = double(cpu.nsecsElapsed()) / 1e6;
        const int entities = m_scene ? int(m_scene->entities().size()) : 0;
        m_stats->frameRendered(cpuMs, m_renderer.lastFrameStats(), m_renderer.backendName(),
                               renderTarget()->pixelSize(), entities);
        QRhiWidget::update(); // keep rendering: FPS only means something with continuous frames
    }
}

void Viewport::releaseResources()
{
    m_renderer.release();
}

void Viewport::updateGlassRects()
{
    const qreal dpr = devicePixelRatio();
    QList<QRectF> rects;
    float radius = 12.0f;
    for (QObject *o : children()) {
        auto *w = qobject_cast<QWidget *>(o);
        if (!w || !w->isVisible() || !w->property("glass").toBool())
            continue;
        const QVariant r = w->property("glassRadius");
        if (r.isValid())
            radius = r.toFloat();
        const QRect g = w->geometry();
        rects.append(QRectF(g.x() * dpr, g.y() * dpr, g.width() * dpr, g.height() * dpr));
    }
    m_renderer.setGlassRects(rects, float(radius * dpr));
}

bool Viewport::layoutGizmo()
{
    if (!m_scene || !m_selection)
        return false;
    const Entity *e = m_scene->entity(m_selection->current());
    if (!e)
        return false;
    m_gizmo.layout(m_camera, QSizeF(size()), e->transform);
    return m_gizmo.isVisible();
}

// ---------------------------------------------------------------------------
// Glass widgets
// ---------------------------------------------------------------------------

void Viewport::childEvent(QChildEvent *event)
{
    if (event->added() && event->child()->isWidgetType())
        event->child()->installEventFilter(this);
    else if (event->removed())
        redraw();
    QRhiWidget::childEvent(event);
}

bool Viewport::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Move:
    case QEvent::Resize:
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::DynamicPropertyChange:
        if (watched->parent() == this)
            redraw(); // glass rectangles changed
        break;
    default:
        break;
    }
    return QRhiWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Mouse
// ---------------------------------------------------------------------------

void Viewport::mousePressEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();

    // Right click while dragging the gizmo: cancel, put the transform back.
    if (m_gizmo.isDragging() && event->button() == Qt::RightButton) {
        m_scene->setTransform(m_selection->current(), m_gizmo.dragStart());
        m_gizmo.endDrag();
        redraw();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton && m_scene && m_selection) {
        // Gizmo handle first, then picking.
        if (layoutGizmo()) {
            const int handle = m_gizmo.hitTest(pos);
            if (handle != Gizmo::None) {
                m_gizmo.beginDrag(handle, pos, m_scene->entity(m_selection->current())->transform);
                redraw();
                event->accept();
                return;
            }
        }
        // Light icons sit on top of the scene, so they are tested before the meshes.
        if (const EntityId light = LightIcons::hitTest(*m_scene, m_camera, QSizeF(size()), pos)) {
            m_selection->select(light);
            event->accept();
            return;
        }
        QVector3D origin, direction;
        m_camera.screenRay(pos, QSizeF(size()), &origin, &direction);
        m_selection->select(pickEntity(*m_scene, origin, direction)); // nothing hit = deselect
        event->accept();
        return;
    }

    if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        m_dragButton = event->button();
        m_lastMouse = pos.toPoint();
        event->accept();
        return;
    }
    QRhiWidget::mousePressEvent(event);
}

void Viewport::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->position();

    if (m_gizmo.isDragging()) {
        // Live preview: the scene is changed directly, the undo step is pushed on release.
        m_scene->setTransform(m_selection->current(), m_gizmo.dragTo(pos));
        event->accept();
        return;
    }

    if (m_dragButton == Qt::NoButton) {
        // Hover highlight of the gizmo handles.
        const int handle = layoutGizmo() ? m_gizmo.hitTest(pos) : Gizmo::None;
        if (handle != m_gizmo.hovered()) {
            m_gizmo.setHovered(handle);
            redraw();
        }
        QRhiWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint p = pos.toPoint();
    const QPoint d = p - m_lastMouse;
    m_lastMouse = p;
    if (m_dragButton == Qt::RightButton)
        m_camera.orbit(-d.x() * 0.3f, d.y() * 0.3f);
    else
        m_camera.pan(float(d.x()), float(d.y()));
    redraw();
    event->accept();
}

void Viewport::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_gizmo.isDragging() && event->button() == Qt::LeftButton) {
        const EntityId id = m_selection->current();
        const Transform start = m_gizmo.dragStart();
        const TransformField field = m_gizmo.field();
        m_gizmo.endDrag();
        if (const Entity *e = m_scene->entity(id); e && !(e->transform == start))
            m_undo->push(new SetTransformCommand(m_scene, id, start, e->transform, field));
        redraw();
        event->accept();
        return;
    }
    if (event->button() == m_dragButton) {
        m_dragButton = Qt::NoButton;
        event->accept();
        return;
    }
    QRhiWidget::mouseReleaseEvent(event);
}

void Viewport::wheelEvent(QWheelEvent *event)
{
    m_camera.zoom(event->angleDelta().y() / 120.0f);
    redraw();
    event->accept();
}

void Viewport::leaveEvent(QEvent *event)
{
    if (m_gizmo.hovered() != Gizmo::None) {
        m_gizmo.setHovered(Gizmo::None);
        redraw();
    }
    QRhiWidget::leaveEvent(event);
}
