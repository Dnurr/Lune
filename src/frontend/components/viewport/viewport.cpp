#include "viewport.h"

#include <QChildEvent>
#include <QCursor>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QtMath>
#include <cmath>
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
#include "overlaydraw.h"
#include "statsoverlay.h"

Viewport::Viewport(QWidget *parent)
    : QRhiWidget(parent)
{
    setMouseTracking(true); // gizmo hover highlight
    // Clicking the viewport takes the keyboard focus away from text fields
    // (so Ctrl+Z undoes the scene, and the command palette closes).
    setFocusPolicy(Qt::ClickFocus);
    setAcceptDrops(true); // files from the Assets panel or the file manager

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
    connect(m_scene, &Scene::materialAdded, this, &Viewport::redraw);
    connect(m_scene, &Scene::materialRemoved, this, &Viewport::redraw);
    connect(m_scene, &Scene::materialChanged, this, &Viewport::redraw); // graph edits, values
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
    leaveCameraView();
    const float shift = m_goal.shiftX; // the lens shift follows the panels, not the scene
    m_camera = camera;
    m_goal = camera;
    m_camera.shiftX = m_goal.shiftX = shift;
    redraw();
}

void Viewport::setCameraView(bool on)
{
    if (on && !(m_scene && m_scene->sceneCamera()))
        on = false;
    if (on == m_cameraView)
        return;
    if (!on) {
        leaveCameraView();
        return;
    }
    stopFlying();
    m_editorFieldOfView = m_goal.fieldOfView;
    m_cameraView = true; // advanceCamera() moves the goal to the scene camera every frame
    emit cameraViewChanged(true);
    redraw();
}

void Viewport::leaveCameraView()
{
    if (!m_cameraView)
        return;
    m_cameraView = false;
    // The view stays where it is, upright again, with the editor's lens.
    m_goal.roll = 0.0f;
    m_goal.fieldOfView = m_editorFieldOfView;
    updateLensShift();
    emit cameraViewChanged(false);
    redraw();
}

void Viewport::setRenderAspect(float aspect)
{
    aspect = qMax(0.0f, aspect);
    if (qAbs(aspect - m_renderAspect) < 1e-5f)
        return;
    m_renderAspect = aspect;
    redraw();
}

float Viewport::iconAspect() const
{
    return m_renderAspect > 0.0f ? m_renderAspect : float(width()) / float(qMax(1, height()));
}

QRectF Viewport::cameraFrame() const
{
    const QRectF all(QPointF(0, 0), QSizeF(size()));
    if (m_renderAspect <= 0.0f || height() <= 0)
        return all;
    const double widgetAspect = double(width()) / height();
    if (m_renderAspect >= widgetAspect) { // wider: bars above and below
        const double h = width() / m_renderAspect;
        return QRectF(0, (height() - h) * 0.5, width(), h);
    }
    const double w = height() * m_renderAspect; // narrower: bars on the sides
    return QRectF((width() - w) * 0.5, 0, w, height());
}

void Viewport::setCameraMode(CameraMode mode)
{
    if (mode == m_cameraMode)
        return;
    stopFlying();
    m_dragButton = Qt::NoButton;
    m_cameraMode = mode;
    emit cameraModeChanged(mode);
}

void Viewport::setFlySpeed(float speed)
{
    speed = qBound(MinFlySpeed, speed, MaxFlySpeed);
    if (qFuzzyCompare(speed, m_flySpeed))
        return;
    m_flySpeed = speed;
    emit flySpeedChanged(speed);
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

void Viewport::setViewSettings(const ViewSettings &settings)
{
    if (m_view == settings)
        return;
    m_view = settings;
    emit viewSettingsChanged();
    redraw();
}

QRhi::Implementation Viewport::rhiBackend() const
{
    // rhi() is null until the first frame: then the backend the widget will pick.
    if (QRhi *r = const_cast<Viewport *>(this)->rhi())
        return r->backend();
    switch (api()) {
    case Api::OpenGL: return QRhi::OpenGLES2;
    case Api::Metal: return QRhi::Metal;
    case Api::Vulkan: return QRhi::Vulkan;
    case Api::Direct3D11: return QRhi::D3D11;
    case Api::Direct3D12: return QRhi::D3D12;
    case Api::Null: return QRhi::Null;
    }
    return QRhi::Null;
}

void Viewport::setOverlaysVisible(bool visible)
{
    if (m_overlays == visible)
        return;
    if (!visible && m_gizmo.isDragging()) { // cancel the drag, like a right click
        if (m_scene && m_selection)
            m_scene->setTransform(m_selection->current(), m_gizmo.dragStart());
        m_gizmo.endDrag();
    }
    m_gizmo.setHovered(Gizmo::None);
    m_overlays = visible;
    m_renderer.setGridLinesVisible(visible);
    redraw();
}

void Viewport::setGizmoMode(Gizmo::Mode mode)
{
    if (m_gizmo.isDragging())
        return;
    m_gizmo.setMode(mode);
    m_gizmo.setHovered(Gizmo::None);
    redraw();
}

void Viewport::setGizmoSpace(Gizmo::Space space)
{
    if (m_gizmo.isDragging())
        return;
    m_gizmo.setSpace(space);
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
    const bool moving = advanceCamera();
    updateGlassRects();

    std::vector<OverlayVertex> overlay;
    const Entity *lookingThrough = m_cameraView && m_scene ? m_scene->sceneCamera() : nullptr;
    if (lookingThrough && m_overlays && m_renderAspect > 0.0f) {
        // Passepartout: what falls outside the rendered image is darkened, with a thin frame.
        const QRectF f = cameraFrame();
        const QColor shade(0, 0, 0, 140), line(255, 255, 255, 70);
        const double w = width(), h = height();
        if (f.top() > 0.5) {
            OverlayDraw::quad(overlay, { 0, 0 }, { w, 0 }, { w, f.top() }, { 0, f.top() }, shade);
            OverlayDraw::quad(overlay, { 0, f.bottom() }, { w, f.bottom() }, { w, h }, { 0, h }, shade);
        }
        if (f.left() > 0.5) {
            OverlayDraw::quad(overlay, { 0, 0 }, { f.left(), 0 }, { f.left(), h }, { 0, h }, shade);
            OverlayDraw::quad(overlay, { f.right(), 0 }, { w, 0 }, { w, h }, { f.right(), h }, shade);
        }
        OverlayDraw::segment(overlay, f.topLeft(), f.topRight(), 1.0f, line);
        OverlayDraw::segment(overlay, f.bottomLeft(), f.bottomRight(), 1.0f, line);
        OverlayDraw::segment(overlay, f.topLeft(), f.bottomLeft(), 1.0f, line);
        OverlayDraw::segment(overlay, f.topRight(), f.bottomRight(), 1.0f, line);
    }
    if (m_scene && m_overlays)
        LightIcons::append(*m_scene, m_camera, QSizeF(size()), m_selection ? m_selection->current() : InvalidEntity,
                           iconAspect(), lookingThrough ? lookingThrough->id : InvalidEntity, overlay);
    if (layoutGizmo()) // after the icons: drawn on top of them
        m_gizmo.appendGeometry(overlay);
    m_renderer.setOverlay(std::move(overlay), float(devicePixelRatio()));
    m_renderer.setViewSettings(m_view);
    m_renderer.setTime(m_seconds);
    if (m_display)
        m_renderer.setDisplayParams(float(m_display->exposure()), m_display->tonemapper());

    // Without overlays nothing is outlined.
    const EntityId selected = m_selection && m_overlays ? m_selection->current() : InvalidEntity;
    m_renderer.render(cb, renderTarget(), m_camera, m_scene, selected);
    if (m_renderer.takeMaterialErrorsChanged())
        emit materialErrorsChanged();
    if (const auto cache = m_renderer.fluidCachedFrames(); cache != m_fluidCache) {
        m_fluidCache = cache;
        emit fluidCacheChanged(m_fluidCache);
    }

    if (m_stats->isVisible()) {
        // CPU time to prepare and record this frame (the GPU work happens later).
        const double cpuMs = double(cpu.nsecsElapsed()) / 1e6;
        const int entities = m_scene ? int(m_scene->entities().size()) : 0;
        m_stats->frameRendered(cpuMs, m_renderer.lastFrameStats(), m_renderer.backendName(),
                               renderTarget()->pixelSize(), entities);
        QRhiWidget::update(); // keep rendering: FPS only means something with continuous frames
    }
    if (moving || m_renderer.needsAnotherFrame())
        QRhiWidget::update(); // next frame continues the camera's motion, or shows loaded textures
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
    if (!m_scene || !m_selection || !m_overlays)
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
        if (watched->parent() == this) {
            redraw(); // glass rectangles changed
            updateLensShift();
        }
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
        const Entity *lookingThrough = m_cameraView ? m_scene->sceneCamera() : nullptr;
        const EntityId light = m_overlays ? LightIcons::hitTest(*m_scene, m_camera, QSizeF(size()), pos, iconAspect(),
                                                                lookingThrough ? lookingThrough->id : InvalidEntity)
                                          : InvalidEntity;
        if (light) {
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

    if (event->button() == Qt::RightButton && m_cameraMode == CameraMode::Free) {
        // Look around: the cursor hides and stays put, only its motion counts.
        leaveCameraView();
        m_flying = true;
        m_lookAnchor = event->globalPosition().toPoint();
        m_flyKeys.clear();
        setFocus(Qt::MouseFocusReason); // the movement keys come here
        setCursor(Qt::BlankCursor);
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

    if (m_flying) {
        const QPoint d = event->globalPosition().toPoint() - m_lookAnchor;
        if (!d.isNull()) {
            m_goal.look(-d.x() * 0.2f, d.y() * 0.2f);
            QCursor::setPos(m_lookAnchor); // its own move event has no motion
            redraw();
        }
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
    if (d.isNull()) {
        event->accept();
        return;
    }
    leaveCameraView();
    if (m_dragButton == Qt::RightButton)
        m_goal.orbit(-d.x() * 0.3f, d.y() * 0.3f);
    else
        m_goal.pan(float(d.x()), float(d.y()));
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
    if (m_flying && event->button() == Qt::RightButton) {
        stopFlying();
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
    const float steps = event->angleDelta().y() / 120.0f;
    if (!m_flying)
        leaveCameraView();
    if (m_flying)
        setFlySpeed(m_flySpeed * qPow(1.2f, steps)); // while looking: flying speed
    else if (m_cameraMode == CameraMode::Free)
        m_goal.fly(QVector3D(0, 0, steps * qMax(0.25f, m_flySpeed * 0.25f))); // forward / back
    else
        m_goal.zoom(steps);
    redraw();
    event->accept();
}

void Viewport::setTime(float seconds)
{
    if (seconds == m_seconds)
        return;
    m_seconds = seconds;
    redraw();
}

void Viewport::setCenterInFreeArea(bool on)
{
    m_centerInFreeArea = on;
    updateLensShift();
}

void Viewport::updateLensShift()
{
    float shift = 0.0f;
    if (m_centerInFreeArea && width() > 0) {
        // Free area: between the panels docked on the left and on the right.
        int left = 0, right = width();
        for (QObject *o : children()) {
            auto *w = qobject_cast<QWidget *>(o);
            if (!w || !w->isVisible() || !w->property("glass").toBool() || w->property("dockSide").isNull())
                continue;
            const QString side = w->property("dockSide").toString();
            if (side == QLatin1String("left"))
                left = qMax(left, w->geometry().right() + 1);
            else if (side == QLatin1String("right"))
                right = qMin(right, w->geometry().left());
        }
        if (right > left)
            shift = float((left + right) * 0.5 - width() * 0.5) / float(width()) * 2.0f;
    }
    if (m_cameraView)
        return; // centered on the camera's frame (advanceCamera())
    if (qAbs(shift - m_goal.shiftX) > 1e-4f) {
        m_goal.shiftX = shift;
        redraw();
    }
}

// ---------------------------------------------------------------------------
// Drag and drop
// ---------------------------------------------------------------------------

QStringList Viewport::droppableFiles(const QMimeData *mime) const
{
    QStringList files;
    if (!mime || !mime->hasUrls())
        return files;
    for (const QUrl &url : mime->urls()) {
        const QString path = url.toLocalFile();
        if (!path.isEmpty() && m_dropSuffixes.contains(QFileInfo(path).suffix().toLower()))
            files.append(path);
    }
    return files;
}

void Viewport::dragEnterEvent(QDragEnterEvent *event)
{
    if (!droppableFiles(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();
}

void Viewport::dragMoveEvent(QDragMoveEvent *event)
{
    // Not over a panel: those aren't drop targets (yet).
    if (childAt(event->position().toPoint()) == nullptr && !droppableFiles(event->mimeData()).isEmpty())
        event->acceptProposedAction();
    else
        event->ignore();
}

void Viewport::dropEvent(QDropEvent *event)
{
    const QStringList files = droppableFiles(event->mimeData());
    if (files.isEmpty()) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    activateWindow(); // the drag may come from another application
    emit filesDropped(files, event->position());
}

QVector3D Viewport::dropPoint(const QPointF &pos, bool *onGround) const
{
    QVector3D origin, direction;
    m_camera.screenRay(pos, QSizeF(size()), &origin, &direction);
    const QVector3D d = direction.normalized();
    // Ground plane, if the ray reaches it in front of the camera and not too far away.
    if (d.y() < -1e-4f) {
        const float t = -origin.y() / d.y();
        if (t > 0.0f && t < 20.0f * qMax(1.0f, m_camera.distance)) {
            if (onGround)
                *onGround = true;
            return origin + d * t;
        }
    }
    if (onGround)
        *onGround = false;
    return origin + d * m_camera.distance; // as far as the orbit target
}

// ---------------------------------------------------------------------------
// Free camera
// ---------------------------------------------------------------------------

namespace
{
bool isFlyKey(int key)
{
    switch (key) {
    case Qt::Key_W:
    case Qt::Key_A:
    case Qt::Key_S:
    case Qt::Key_D:
    case Qt::Key_Q:
    case Qt::Key_E:
    case Qt::Key_Shift:
        return true;
    default:
        return false;
    }
}
} // namespace

bool Viewport::event(QEvent *event)
{
    // While flying, W / E / Q ... are movement keys, not the window's shortcuts (tools).
    if (event->type() == QEvent::ShortcutOverride && m_flying) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (isFlyKey(key->key()) && (key->modifiers() & ~Qt::ShiftModifier) == Qt::NoModifier) {
            event->accept();
            return true;
        }
    }
    return QRhiWidget::event(event);
}

void Viewport::keyPressEvent(QKeyEvent *event)
{
    if (m_flying && isFlyKey(event->key())) {
        if (!event->isAutoRepeat())
            m_flyKeys.insert(event->key());
        redraw(); // the frame loop moves the camera while the key is held
        event->accept();
        return;
    }
    QRhiWidget::keyPressEvent(event);
}

void Viewport::keyReleaseEvent(QKeyEvent *event)
{
    if (m_flying && isFlyKey(event->key())) {
        if (!event->isAutoRepeat())
            m_flyKeys.remove(event->key());
        event->accept();
        return;
    }
    QRhiWidget::keyReleaseEvent(event);
}

void Viewport::focusOutEvent(QFocusEvent *event)
{
    stopFlying(); // e.g. Alt+Tab while holding the button
    QRhiWidget::focusOutEvent(event);
}

void Viewport::stopFlying()
{
    if (!m_flying)
        return;
    m_flying = false;
    m_flyKeys.clear();
    unsetCursor();
}

namespace
{
// Time constant of the camera smoothing: the drawn camera covers ~63% of the remaining way
// to the goal in this time. Short enough to feel direct; long enough to even out mouse
// events that don't line up with the frames (e.g. a 125 Hz mouse on a 60 Hz screen gives
// 2 or 3 events per frame, so unsmoothed motion speeds up and slows down every frame).
constexpr float kSmoothingSeconds = 0.03f;
} // namespace

bool Viewport::advanceCamera()
{
    // First frame of a motion: the time since the last frame means nothing (idle), use
    // one nominal frame. Long stalls are capped so nothing jumps.
    const float elapsed = m_frameClock.isValid() ? float(m_frameClock.restart()) / 1000.0f : 0.0f;
    if (!m_frameClock.isValid())
        m_frameClock.start();
    if (m_cameraView) {
        const Entity *cam = m_scene ? m_scene->sceneCamera() : nullptr;
        if (!cam) {
            leaveCameraView(); // the camera was deleted or disabled
        } else {
            OrbitCamera goal = OrbitCamera::fromSceneCamera(cam->transform, *cam->camera, m_goal.distance);
            // The camera's vertical field of view covers the frame; the viewport shows more
            // above and below when the frame is shorter than the widget.
            const QRectF frame = cameraFrame();
            if (frame.height() > 0.0 && frame.height() < height() - 0.5) {
                const double t = std::tan(qDegreesToRadians(double(goal.fieldOfView)) * 0.5) * height() / frame.height();
                goal.fieldOfView = float(qRadiansToDegrees(2.0 * std::atan(t)));
            }
            goal.shiftX = 0.0f; // centered, like the render
            goal.yaw += 360.0f * std::round((m_camera.yaw - goal.yaw) / 360.0f); // the short way round
            m_goal = goal;
        }
    }
    const float dt = m_animating ? qBound(0.0f, elapsed, 0.1f) : 1.0f / 60.0f;

    QVector3D dir;
    if (m_flyKeys.contains(Qt::Key_W)) dir.setZ(dir.z() + 1);
    if (m_flyKeys.contains(Qt::Key_S)) dir.setZ(dir.z() - 1);
    if (m_flyKeys.contains(Qt::Key_D)) dir.setX(dir.x() + 1);
    if (m_flyKeys.contains(Qt::Key_A)) dir.setX(dir.x() - 1);
    if (m_flyKeys.contains(Qt::Key_E)) dir.setY(dir.y() + 1);
    if (m_flyKeys.contains(Qt::Key_Q)) dir.setY(dir.y() - 1);
    const bool flying = m_flying && !dir.isNull();
    if (flying) {
        const float boost = m_flyKeys.contains(Qt::Key_Shift) ? 3.0f : 1.0f;
        m_goal.fly(dir.normalized() * (m_flySpeed * boost * dt));
    }

    if (m_camera.isCloseTo(m_goal))
        m_camera = m_goal;
    else
        m_camera.moveTowards(m_goal, 1.0f - std::exp(-dt / kSmoothingSeconds));

    // Keep animating while catching up, or while a movement key is held.
    m_animating = flying || !m_camera.isCloseTo(m_goal);
    return m_animating;
}

void Viewport::leaveEvent(QEvent *event)
{
    if (m_gizmo.hovered() != Gizmo::None) {
        m_gizmo.setHovered(Gizmo::None);
        redraw();
    }
    QRhiWidget::leaveEvent(event);
}
