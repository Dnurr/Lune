#pragma once

#include <QElapsedTimer>
#include <QPoint>
#include <QRhiWidget>
#include <QSet>
#include <functional>

#include "engine/render/camera.h"
#include "engine/render/renderer.h"
#include "gizmo.h"

class DisplaySettings;
class QMimeData;
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
//
// Drag and drop: local files (from the Assets panel or the system's file manager) whose
// suffix is in setDropSuffixes() can be dropped; filesDropped() tells where.
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

    // Where the camera is going: input moves this one, and the camera drawn on screen
    // follows it smoothly (see advanceCamera()). Jumps there in setCamera().
    const OrbitCamera &camera() const { return m_goal; }
    void setCamera(const OrbitCamera &camera);

    void setCameraMode(CameraMode mode);
    CameraMode cameraMode() const { return m_cameraMode; }
    // Free camera speed, world units per second.
    void setFlySpeed(float speed);
    float flySpeed() const { return m_flySpeed; }
    static constexpr float MinFlySpeed = 0.1f;
    static constexpr float MaxFlySpeed = 200.0f;

    // File suffixes (lower case, no dot) accepted by drag and drop.
    void setDropSuffixes(const QStringList &suffixes) { m_dropSuffixes = suffixes; }
    // Where something dropped at `pos` (widget pixels) goes in the world: on the ground
    // plane (y = 0) under the cursor, or in front of the camera when the ground isn't
    // under it. `onGround` tells which.
    QVector3D dropPoint(const QPointF &pos, bool *onGround = nullptr) const;

    // Node materials: why one can't be drawn (empty = fine). materialErrorsChanged() after a
    // frame that changed any.
    QString materialError(MaterialId id) const { return m_renderer.materialError(id); }

    // Centers the scene in the part of the viewport not covered by the panels on the left
    // and right (a lens shift, smoothed like the camera). Off: centered in the whole widget.
    void setCenterInFreeArea(bool on);

signals:
    void materialErrorsChanged();
    void filesDropped(const QStringList &paths, const QPointF &pos);
    void cameraModeChanged(CameraMode mode);
    void flySpeedChanged(float speed);

protected:
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
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
    void updateLensShift();
    QStringList droppableFiles(const QMimeData *mime) const;
    // Once per frame, before drawing: flies the goal by the held keys, then moves the
    // drawn camera towards the goal. Returns true while it's still moving.
    bool advanceCamera();

    Renderer m_renderer;
    OrbitCamera m_camera; // drawn (also used by picking and the gizmo: what you see)
    OrbitCamera m_goal;   // where the input wants it
    Gizmo m_gizmo;
    StatsOverlay *m_stats = nullptr;
    DisplaySettings *m_display = nullptr;

    Scene *m_scene = nullptr;
    Selection *m_selection = nullptr;
    QUndoStack *m_undo = nullptr;

    Qt::MouseButton m_dragButton = Qt::NoButton; // camera drag
    QPoint m_lastMouse;

    QStringList m_dropSuffixes;
    bool m_centerInFreeArea = false;
    CameraMode m_cameraMode = CameraMode::Orbit;
    float m_flySpeed = 4.0f;
    bool m_flying = false;   // free camera, right button held
    QPoint m_lookAnchor;     // global cursor position, kept there while looking
    QSet<int> m_flyKeys;     // movement keys held
    QElapsedTimer m_frameClock; // time between animated frames
    bool m_animating = false;   // the previous frame asked for another one
};
