#pragma once

#include <QElapsedTimer>
#include <QPoint>
#include <QRhiWidget>
#include <QSet>
#include <QTimer>
#include <functional>

#include "engine/render/camera.h"
#include "engine/render/renderer.h"
#include "gizmo.h"

class DisplaySettings;
class QUndoStack;
class StatsOverlay;
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
//                     right click during the drag cancels it). The center handle moves
//                     in the view plane (move) or scales uniformly (scale)
//   right drag        orbit,  middle drag = pan,  wheel = zoom
//
// Free camera (setCameraMode(CameraMode::Free)):
//   hold right button look around (cursor hidden), and meanwhile
//                     W/S forward/back, A/D left/right, E/Q up/down, Shift = faster,
//                     wheel = flying speed
//   wheel             move forward / back,  middle drag = pan
class Viewport : public QRhiWidget
{
    Q_OBJECT

public:
    explicit Viewport(QWidget *parent = nullptr);
    ~Viewport() override;

    void resize(int x, int y);
    void update(std::function<void()> f);

    // Editor state the viewport shows and edits. Must be set before the first frame.
    void setEditorContext(Scene *scene, Selection *selection, QUndoStack *undoStack);

    // Exposure and tonemapping used to show the HDR scene. Redraws when they change.
    void setDisplaySettings(DisplaySettings *settings);

    void setGizmoMode(Gizmo::Mode mode);
    Gizmo::Mode gizmoMode() const { return m_gizmo.mode(); }
    // World or entity axes for move and rotate (scale always uses the entity's axes).
    void setGizmoSpace(Gizmo::Space space);
    Gizmo::Space gizmoSpace() const { return m_gizmo.space(); }

    // Frame statistics panel; while visible the viewport renders continuously.
    void setStatsVisible(bool visible);
    bool isStatsVisible() const;

    const OrbitCamera &camera() const { return m_camera; }
    void setCamera(const OrbitCamera &camera);

    void setCameraMode(CameraMode mode);
    CameraMode cameraMode() const { return m_cameraMode; }
    // Free camera speed, world units per second.
    void setFlySpeed(float speed);
    float flySpeed() const { return m_flySpeed; }
    static constexpr float MinFlySpeed = 0.1f;
    static constexpr float MaxFlySpeed = 200.0f;

signals:
    void cameraModeChanged(CameraMode mode);
    void flySpeedChanged(float speed);

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
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
    void stopFlying();
    void flyStep(); // timer: moves the free camera by the held keys

    Renderer m_renderer;
    OrbitCamera m_camera;
    Gizmo m_gizmo;
    StatsOverlay *m_stats = nullptr;
    DisplaySettings *m_display = nullptr;

    Scene *m_scene = nullptr;
    Selection *m_selection = nullptr;
    QUndoStack *m_undo = nullptr;

    Qt::MouseButton m_dragButton = Qt::NoButton; // camera drag
    QPoint m_lastMouse;

    CameraMode m_cameraMode = CameraMode::Orbit;
    float m_flySpeed = 4.0f;
    bool m_flying = false;   // free camera, right button held
    QPoint m_lookAnchor;     // global cursor position, kept there while looking
    QSet<int> m_flyKeys;     // movement keys held
    QTimer m_flyTimer;
    QElapsedTimer m_flyClock;
};
