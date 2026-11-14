#pragma once

#include <QPointF>
#include <QSizeF>
#include <vector>

#include "engine/render/camera.h"
#include "engine/render/renderer.h"
#include "engine/scene/transform.h"

// Transform gizmo: move / rotate / scale along the world axes (Global) or the entity's own
// axes (Local). Scale is always along the entity's axes: scale is applied before rotation,
// so it can't stretch along a world axis of a rotated entity.
// Everything happens in screen space: the handles are projected once per frame
// (layout), then the same 2D shapes are used to draw them and to hit-test the mouse,
// so what you see is exactly what you can grab. Constant size on screen.
//
// Typical use (see Viewport):
//   layout(camera, viewSize, transform)        every frame / mouse event
//   hitTest(mouse) -> handle                   hover + press
//   beginDrag / dragTo(mouse) -> Transform     while dragging
//   appendGeometry(vertices)                   overlay triangles for the renderer
class Gizmo
{
public:
    enum class Mode
    {
        Translate,
        Rotate,
        Scale,
    };

    enum class Space
    {
        Global, // world axes
        Local,  // the entity's axes
    };

    // Handles: one per axis, plus the center: move in the view plane (Translate),
    // uniform scale (Scale).
    static constexpr int None = -1;
    static constexpr int AxisX = 0;
    static constexpr int AxisY = 1;
    static constexpr int AxisZ = 2;
    static constexpr int Center = 3;
    static constexpr int Uniform = Center;

    void setMode(Mode mode) { m_mode = mode; }
    Mode mode() const { return m_mode; }
    void setSpace(Space space) { m_space = space; }
    Space space() const { return m_space; }

    void layout(const OrbitCamera &camera, const QSizeF &viewSize, const Transform &transform);
    bool isVisible() const { return m_visible; }

    int hitTest(const QPointF &point) const;
    void setHovered(int handle) { m_hovered = handle; }
    int hovered() const { return m_hovered; }

    void appendGeometry(std::vector<OverlayVertex> &out) const;

    void beginDrag(int handle, const QPointF &mouse, const Transform &start);
    Transform dragTo(const QPointF &mouse); // new transform for the current mouse position
    void endDrag();
    bool isDragging() const { return m_active != None; }
    int activeHandle() const { return m_active; }
    const Transform &dragStart() const { return m_start; }
    TransformField field() const;

private:
    struct Shape
    {
        int handle;
        std::vector<QPointF> points; // polyline in screen pixels
        bool closed = false;
    };

    QVector3D axisVector(int axis) const { return m_axes[axis]; }
    bool hasCenterHandle() const { return m_mode != Mode::Rotate; }
    bool viewPlanePoint(const QPointF &mouse, QVector3D *point) const;

    Mode m_mode = Mode::Translate;
    Space m_space = Space::Global;
    QVector3D m_axes[3];    // world direction of each handle's axis
    OrbitCamera m_camera;
    QSizeF m_viewSize;
    QVector3D m_center;     // world
    QPointF m_screenCenter;
    float m_length = 1.0f;  // world length of an axis handle
    bool m_visible = false;
    std::vector<Shape> m_shapes;
    std::vector<QPointF> m_axisEnds; // screen end point of each axis handle
    int m_hovered = None;

    // Drag state
    int m_active = None;
    Transform m_start;
    QPointF m_startMouse;
    float m_startParam = 0.0f;  // translate: position along the axis at the start
    QPointF m_startAxisScreen;  // scale: screen vector of the axis at the start
    float m_lastAngle = 0.0f;   // rotate: previous mouse angle around the center (degrees)
    float m_angle = 0.0f;       // rotate: accumulated angle
    QVector3D m_dragAxis;       // rotate: world axis, fixed for the whole drag
    QVector3D m_lastRotation;   // rotate: Euler angles of the previous step (continuity)
    QVector3D m_startPlanePoint; // translate, center: mouse on the view plane at the start
};
