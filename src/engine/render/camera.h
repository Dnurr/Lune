#pragma once

#include <QMatrix4x4>
#include <QVector3D>

// Camera orbiting around a target point (editor-style).
// Angles in degrees; projection in the OpenGL clip-space convention
// (the renderer corrects it per backend where needed).
class OrbitCamera
{
public:
    QVector3D target { 0.0f, 0.0f, 0.0f };
    float yaw = 45.0f;    // around the world Y axis
    float pitch = 30.0f;  // above the ground plane, clamped to (-89, 89)
    float distance = 15.0f;

    float fieldOfView = 50.0f; // vertical, degrees
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    QVector3D eye() const;
    QMatrix4x4 view() const;
    QMatrix4x4 projection(float aspect) const;

    void orbit(float deltaYaw, float deltaPitch);
    // Move the target on the camera plane; dx/dy in pixels (speed scales with distance).
    void pan(float dx, float dy);
    // Positive steps move closer (one mouse-wheel notch = 1 step).
    void zoom(float steps);
};
