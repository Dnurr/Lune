#pragma once

#include <QString>
#include <QVariant>
#include <array>
#include <vector>

// Keyframe animation of an entity's properties (the reflected ones: engine/scene/components.h).
// The data only; evaluating and editing it is in animation.h.

// How the value goes from a key to the next one.
enum class Interpolation
{
    Constant, // holds the key's value until the next key
    Linear,   // straight line
    Bezier,   // smooth: ease in and out, auto tangents that never overshoot the keys
};

struct Keyframe
{
    double frame = 0.0;
    QVariant value; // same type as the property's value (PropertyType)
    Interpolation interpolation = Interpolation::Bezier; // towards the next key
    // Bezier tangents through the key, the same on both sides (aligned handles): automatic
    // (smooth, never overshooting), or set by hand in the curve editor as the value change per
    // frame of each number (x, y, z of a vector; r, g, b, a of a color).
    bool autoTangent = true;
    std::array<double, 4> slope {};

    bool operator==(const Keyframe &) const = default;
};

// The keys of one property, sorted by frame, never two on the same frame.
struct AnimationTrack
{
    QString component; // ComponentDesc::name, e.g. "Transform"
    QString property;  // PropertyDesc::key(), e.g. "position" or "bloom.intensity"
    std::vector<Keyframe> keys;

    bool operator==(const AnimationTrack &) const = default;
};
