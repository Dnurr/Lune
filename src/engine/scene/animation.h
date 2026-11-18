#pragma once

#include <optional>

#include "components.h"
#include "keyframes.h"

class Scene;

// Evaluating and editing keyframe animation (the data is in keyframes.h, on each Entity).
//
// An animated property's value on the entity is always its value at the scene's frame:
// Animation::apply() writes it whenever the frame or the keys change. Editing an animated
// property (Inspector, gizmo) therefore sets a key at the current frame, otherwise the next
// apply() would put the old value back; the undo commands do that (frontend/core/commands.h).
namespace Animation
{
// Numbers, vectors and colors interpolate; booleans and enums hold (constant) between keys.
// Text, files and read-only properties can't be animated.
bool animatable(const PropertyDesc &property);
bool interpolates(PropertyType type);

// Component and property indices (components()) of a track, or false when it names
// nothing known.
bool resolve(const AnimationTrack &track, int *component, int *property);

const AnimationTrack *track(const Entity &entity, int component, int property);
// Index of the key on `frame` (within a hundredth of a frame), or -1.
int keyAt(const AnimationTrack &track, double frame);

// The value of `track` at `frame` (`type`: the property's), which may lie between keys,
// before the first one (its value) or after the last one (its value). Invalid without keys.
QVariant evaluate(const AnimationTrack &track, PropertyType type, double frame);

// `animation` with the property's key on `frame` set to `value` (added, or replacing the
// value of the key already there and keeping its interpolation); the track is created if
// needed. New keys take the interpolation of the key before them (Bezier for the first).
std::vector<AnimationTrack> withKey(std::vector<AnimationTrack> animation, int component, int property, double frame,
                                    const QVariant &value);
// `animation` without the property's key on `frame`; the track goes when it's left empty.
std::vector<AnimationTrack> withoutKey(std::vector<AnimationTrack> animation, int component, int property,
                                       double frame);
// `animation` without the property's track.
std::vector<AnimationTrack> withoutTrack(std::vector<AnimationTrack> animation, int component, int property);
// `animation` with the interpolation of the key on `frame` (or, between keys, of the key
// before it) set to `interpolation`.
std::vector<AnimationTrack> withInterpolation(std::vector<AnimationTrack> animation, int component, int property,
                                              double frame, Interpolation interpolation);

// Writes every animated property of `entity` at `frame`. Returns true if a value changed.
bool applyTo(Entity &entity, double frame);
// The same for every entity of the scene, at its frame (entityChanged only where needed).
void apply(Scene &scene);
// Every frame that has a key of `entity` (any property), sorted, without repeats.
std::vector<double> keyFrames(const Entity &entity);

QString interpolationName(Interpolation interpolation); // "constant", "linear", "bezier"
std::optional<Interpolation> interpolationFromName(const QString &name);
} // namespace Animation
