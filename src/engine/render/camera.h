#pragma once

#include <QMatrix4x4>
#include <QPointF>
#include <QSizeF>
#include <QVector3D>

#include "engine/scene/entity.h"

// How the editor moves the camera. Both drive the same OrbitCamera, so switching keeps
// the view exactly where it is.
enum class CameraMode
{
    Orbit, // turn around the target (right drag), zoom towards it
    Free,  // turn around the eye and fly (right drag + WASD), like a game engine
};

// Camera orbiting around a target point (editor-style).
// The free camera uses the same state: it turns around the eye (look) and moves eye and
// target together (fly), so the target stays `distance` ahead of the eye.
// Angles in degrees; projection in the OpenGL clip-space convention
// (the renderer corrects it per backend where needed).
class OrbitCamera
{
public:
    QVector3D target { 0.0f, 0.0f, 0.0f };
    float yaw = 45.0f;    // around the world Y axis
    float pitch = 30.0f;  // above the ground plane, clamped to (-89, 89)
    float distance = 8.0f;

    float fieldOfView = 50.0f; // vertical, degrees
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    // Horizontal lens shift in clip space (2 = the whole width): the image slides sideways
    // without turning the camera, e.g. to center the scene in the part of the viewport
    // that panels don't cover. Not saved with the scene.
    float shiftX = 0.0f;
    // Turn around the view direction, degrees (positive: the image turns clockwise). Only
    // scene cameras have it (fromSceneCamera()); the editor's own camera keeps 0.
    float roll = 0.0f;

    // The view of a scene camera: its entity's transform (looking along local -Z, +Y up) and
    // lens. `distance`: how far ahead the orbit target goes. The pitch is limited to ±89°
    // like any OrbitCamera, so a camera looking straight up or down is off by up to 1°.
    static OrbitCamera fromSceneCamera(const Transform &transform, const Camera &camera, float distance = 5.0f);
    // The rotation (Euler degrees, as in Transform::rotation) that makes a scene camera look
    // like this view; among the equivalent sets, the one closest to `closeTo`.
    QVector3D sceneCameraRotation(const QVector3D &closeTo = {}) const;

    QVector3D eye() const;
    QVector3D forward() const; // unit, from the eye towards the target
    QVector3D right() const;   // unit, horizontal
    QVector3D up() const;      // unit, the image's up (turned by `roll`)
    QMatrix4x4 view() const;
    QMatrix4x4 projection(float aspect) const;

    // Screen helpers. `viewSize` and points are in widget pixels, origin top-left.
    // World point -> screen; `inFront` is false when the point is behind the camera.
    QPointF project(const QVector3D &world, const QSizeF &viewSize, bool *inFront = nullptr) const;
    // Ray through a screen point: origin on the near plane, direction (not normalized) towards far.
    void screenRay(const QPointF &point, const QSizeF &viewSize, QVector3D *origin, QVector3D *direction) const;
    // World units covered by one pixel at `distanceFromEye` (to keep things a constant size on screen).
    float worldPerPixel(float distanceFromEye, float viewHeight) const;

    void orbit(float deltaYaw, float deltaPitch);
    // Move the target on the camera plane; dx/dy in pixels (speed scales with distance).
    void pan(float dx, float dy);
    // Positive steps move closer (one mouse-wheel notch = 1 step).
    void zoom(float steps);

    // Free camera: turn around the eye (same angle convention as orbit()), and move eye
    // and target by `move` = (right, world up, forward) in world units.
    void look(float deltaYaw, float deltaPitch);
    void fly(const QVector3D &move);

    // Smoothing: moves this camera's target, angles and distance a fraction `amount`
    // (0..1) of the way to `goal`'s. Lens settings are copied.
    void moveTowards(const OrbitCamera &goal, float amount);
    // Same view, within a tiny tolerance (relative to the distance).
    bool isCloseTo(const OrbitCamera &other) const;
};
