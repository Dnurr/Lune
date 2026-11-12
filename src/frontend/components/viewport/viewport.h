#pragma once

#include <QPoint>
#include <QRhiWidget>
#include <functional>

#include "engine/render/camera.h"
#include "engine/render/renderer.h"
#include "gizmo.h"

class QUndoStack;
class Scene;
class Selection;

// Editor viewport: a thin widget around the engine's Renderer.
// It owns the camera, the gizmo and the mouse input, and tells the renderer where the
// glass panels are; all GPU work lives in engine/render.
//
// Backend: choose it with setApi(QRhiWidget::Api::...) before the widget is shown;
// otherwise the platform default is used (Direct3D 11 on Windows).
//
// Glass rectangles: every direct child widget with the dynamic property "glass" = true
// (the floating Panels set it) gets the blurred scene behind it, with rounded corners
// ("glassRadius" property, default 12). Moving/resizing/showing/hiding them redraws.
//
// Mouse:
//   left click        select the entity under the cursor (empty space = deselect)
//   left drag gizmo   move / rotate / scale the selection (one undo step per drag;
//                     right click during the drag cancels it)
//   right drag        orbit,  middle drag = pan,  wheel = zoom
class Viewport : public QRhiWidget
{
public:
    explicit Viewport(QWidget *parent = nullptr);
    ~Viewport() override;

    void resize(int x, int y);
    void update(std::function<void()> f);

    // Editor state the viewport shows and edits. Must be set before the first frame.
    void setEditorContext(Scene *scene, Selection *selection, QUndoStack *undoStack);

    void setGizmoMode(Gizmo::Mode mode);
    Gizmo::Mode gizmoMode() const { return m_gizmo.mode(); }

    const OrbitCamera &camera() const { return m_camera; }
    void setCamera(const OrbitCamera &camera);

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
    void leaveEvent(QEvent *event) override;

private:
    void updateGlassRects();
    // Lays the gizmo out on the selected entity; false if nothing (or nothing visible) is selected.
    bool layoutGizmo();
    void redraw();

    Renderer m_renderer;
    OrbitCamera m_camera;
    Gizmo m_gizmo;

    Scene *m_scene = nullptr;
    Selection *m_selection = nullptr;
    QUndoStack *m_undo = nullptr;

    Qt::MouseButton m_dragButton = Qt::NoButton; // camera drag
    QPoint m_lastMouse;
};
