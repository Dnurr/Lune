#pragma once

#include <QPoint>
#include <QRhiWidget>
#include <functional>

#include "engine/render/camera.h"
#include "engine/render/renderer.h"

// Editor viewport: a thin widget around the engine's Renderer.
// It owns the camera and the input, and tells the renderer where the glass panels are;
// all GPU work lives in engine/render.
//
// Backend: choose it with setApi(QRhiWidget::Api::...) before the widget is shown;
// otherwise the platform default is used (Direct3D 11 on Windows).
//
// Glass rectangles: every direct child widget with the dynamic property "glass" = true
// (the floating Panels set it) gets the blurred scene behind it, with rounded corners
// ("glassRadius" property, default 12). Moving/resizing/showing/hiding them redraws.
//
// Camera: right drag = orbit, middle drag = pan, wheel = zoom.
class Viewport : public QRhiWidget
{
public:
    explicit Viewport(QWidget *parent = nullptr);
    ~Viewport() override;

    void resize(int x, int y);
    void update(std::function<void()> f);

    OrbitCamera &camera() { return m_camera; }

protected:
    void initialize(QRhiCommandBuffer *cb) override;
    void render(QRhiCommandBuffer *cb) override;
    void releaseResources() override;

    void childEvent(QChildEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void updateGlassRects();

    Renderer m_renderer;
    OrbitCamera m_camera;

    Qt::MouseButton m_dragButton = Qt::NoButton;
    QPoint m_lastMouse;
};
