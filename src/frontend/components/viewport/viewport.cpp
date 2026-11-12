#include "viewport.h"

#include <QChildEvent>
#include <QMouseEvent>
#include <QWheelEvent>

Viewport::Viewport(QWidget *parent)
    : QRhiWidget(parent)
{
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
    updateGlassRects();
    m_renderer.render(cb, renderTarget(), m_camera);
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

// ---------------------------------------------------------------------------
// Glass widgets
// ---------------------------------------------------------------------------

void Viewport::childEvent(QChildEvent *event)
{
    if (event->added() && event->child()->isWidgetType())
        event->child()->installEventFilter(this);
    else if (event->removed())
        QRhiWidget::update();
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
            QRhiWidget::update(); // glass rectangles changed
        break;
    default:
        break;
    }
    return QRhiWidget::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// Camera input
// ---------------------------------------------------------------------------

void Viewport::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        m_dragButton = event->button();
        m_lastMouse = event->position().toPoint();
        event->accept();
        return;
    }
    QRhiWidget::mousePressEvent(event);
}

void Viewport::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragButton == Qt::NoButton) {
        QRhiWidget::mouseMoveEvent(event);
        return;
    }
    const QPoint pos = event->position().toPoint();
    const QPoint d = pos - m_lastMouse;
    m_lastMouse = pos;

    if (m_dragButton == Qt::RightButton)
        m_camera.orbit(-d.x() * 0.3f, d.y() * 0.3f);
    else
        m_camera.pan(float(d.x()), float(d.y()));

    QRhiWidget::update();
    event->accept();
}

void Viewport::mouseReleaseEvent(QMouseEvent *event)
{
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
    QRhiWidget::update();
    event->accept();
}
