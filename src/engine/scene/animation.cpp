#include "animation.h"

#include <QColor>
#include <QVector3D>
#include <algorithm>
#include <array>
#include <cmath>

#include "scene.h"

namespace
{
constexpr double kSameFrame = 0.01; // keys closer than this are on the same frame

using Numbers = std::array<double, 4>;

int numberCount(PropertyType type)
{
    switch (type) {
    case PropertyType::Float: return 1;
    case PropertyType::Vector3: return 3;
    case PropertyType::Color: return 4;
    default: return 0;
    }
}

Numbers toNumbers(PropertyType type, const QVariant &v)
{
    Numbers n {};
    switch (type) {
    case PropertyType::Float: n[0] = v.toDouble(); break;
    case PropertyType::Vector3: {
        const QVector3D p = v.value<QVector3D>();
        n = { p.x(), p.y(), p.z(), 0.0 };
        break;
    }
    case PropertyType::Color: {
        const QColor c = v.value<QColor>();
        n = { c.redF(), c.greenF(), c.blueF(), c.alphaF() };
        break;
    }
    default: break;
    }
    return n;
}

QVariant fromNumbers(PropertyType type, const Numbers &n)
{
    switch (type) {
    case PropertyType::Float: return n[0];
    case PropertyType::Vector3: return QVariant::fromValue(QVector3D(float(n[0]), float(n[1]), float(n[2])));
    case PropertyType::Color: {
        auto c01 = [](double x) { return float(qBound(0.0, x, 1.0)); };
        return QColor::fromRgbF(c01(n[0]), c01(n[1]), c01(n[2]), c01(n[3]));
    }
    default: return {};
    }
}

// Auto-clamped tangent (value change per frame) of one number at key i: the slope between
// its neighbors, flat at the ends and where the key is a peak or a valley, so the curve never
// overshoots a key.
double autoTangent(const std::vector<Keyframe> &keys, const std::vector<Numbers> &values, size_t i, int c)
{
    if (!keys[i].autoTangent)
        return keys[i].slope[size_t(c)];
    if (i == 0 || i + 1 >= keys.size())
        return 0.0;
    const double prev = values[i - 1][size_t(c)], cur = values[i][size_t(c)], next = values[i + 1][size_t(c)];
    if ((cur - prev) * (next - cur) <= 0.0)
        return 0.0;
    double m = (next - prev) / (keys[i + 1].frame - keys[i - 1].frame);
    // Limit the slope so the segment on either side stays monotonic (Fritsch-Carlson style).
    const double left = (cur - prev) / (keys[i].frame - keys[i - 1].frame);
    const double right = (next - cur) / (keys[i + 1].frame - keys[i].frame);
    const double limit = 3.0 * std::min(std::abs(left), std::abs(right));
    if (std::abs(m) > limit)
        m = std::copysign(limit, m);
    return m;
}

AnimationTrack *findTrack(std::vector<AnimationTrack> &animation, int component, int property)
{
    const ComponentDesc &c = components()[size_t(component)];
    const QString key = c.properties[size_t(property)].key();
    for (AnimationTrack &t : animation)
        if (t.component == c.name && t.property == key)
            return &t;
    return nullptr;
}
} // namespace

namespace Animation
{
bool animatable(const PropertyDesc &property)
{
    if (property.readOnly)
        return false;
    switch (property.type) {
    case PropertyType::Float:
    case PropertyType::Vector3:
    case PropertyType::Color:
    case PropertyType::Bool:
    case PropertyType::Enum: return true;
    case PropertyType::Text:
    case PropertyType::File: return false;
    }
    return false;
}

bool interpolates(PropertyType type)
{
    return numberCount(type) > 0;
}

bool resolve(const AnimationTrack &track, int *component, int *property)
{
    const auto &comps = components();
    for (int ci = 0; ci < int(comps.size()); ++ci) {
        if (comps[size_t(ci)].name != track.component)
            continue;
        const auto &props = comps[size_t(ci)].properties;
        for (int pi = 0; pi < int(props.size()); ++pi) {
            if (props[size_t(pi)].key() == track.property && animatable(props[size_t(pi)])) {
                *component = ci;
                *property = pi;
                return true;
            }
        }
    }
    return false;
}

const AnimationTrack *track(const Entity &entity, int component, int property)
{
    const ComponentDesc &c = components()[size_t(component)];
    const QString key = c.properties[size_t(property)].key();
    for (const AnimationTrack &t : entity.animation)
        if (t.component == c.name && t.property == key)
            return &t;
    return nullptr;
}

int keyAt(const AnimationTrack &track, double frame)
{
    for (int i = 0; i < int(track.keys.size()); ++i)
        if (std::abs(track.keys[size_t(i)].frame - frame) < kSameFrame)
            return i;
    return -1;
}

QVariant evaluate(const AnimationTrack &track, PropertyType type, double frame)
{
    const auto &keys = track.keys;
    if (keys.empty())
        return {};
    if (frame <= keys.front().frame)
        return keys.front().value;
    if (frame >= keys.back().frame)
        return keys.back().value;
    // The segment [k, k + 1] that holds the frame.
    const auto next = std::upper_bound(keys.begin(), keys.end(), frame,
                                       [](double f, const Keyframe &k) { return f < k.frame; });
    const size_t k = size_t(next - keys.begin()) - 1;
    const Keyframe &a = keys[k];
    const Keyframe &b = keys[k + 1];
    if (!interpolates(type) || a.interpolation == Interpolation::Constant)
        return a.value;

    const double h = b.frame - a.frame;
    const double t = (frame - a.frame) / h;
    const int count = numberCount(type);
    const Numbers va = toNumbers(type, a.value), vb = toNumbers(type, b.value);
    Numbers out {};
    if (a.interpolation == Interpolation::Linear) {
        for (int c = 0; c < count; ++c)
            out[size_t(c)] = va[size_t(c)] + (vb[size_t(c)] - va[size_t(c)]) * t;
        return fromNumbers(type, out);
    }
    // Bezier: cubic Hermite with the keys' auto tangents.
    std::vector<Numbers> values;
    values.reserve(keys.size());
    for (const Keyframe &key : keys)
        values.push_back(toNumbers(type, key.value));
    const double t2 = t * t, t3 = t2 * t;
    const double h00 = 2 * t3 - 3 * t2 + 1, h10 = t3 - 2 * t2 + t, h01 = -2 * t3 + 3 * t2, h11 = t3 - t2;
    for (int c = 0; c < count; ++c) {
        const double m0 = autoTangent(keys, values, k, c), m1 = autoTangent(keys, values, k + 1, c);
        out[size_t(c)] = h00 * va[size_t(c)] + h10 * h * m0 + h01 * vb[size_t(c)] + h11 * h * m1;
    }
    return fromNumbers(type, out);
}

std::vector<AnimationTrack> withKey(std::vector<AnimationTrack> animation, int component, int property, double frame,
                                    const QVariant &value)
{
    AnimationTrack *t = findTrack(animation, component, property);
    if (!t) {
        const ComponentDesc &c = components()[size_t(component)];
        animation.push_back({ c.name, c.properties[size_t(property)].key(), {} });
        t = &animation.back();
    }
    const int existing = keyAt(*t, frame);
    if (existing >= 0) {
        t->keys[size_t(existing)].value = value;
        return animation;
    }
    Keyframe key;
    key.frame = frame;
    key.value = value;
    const auto at = std::upper_bound(t->keys.begin(), t->keys.end(), frame,
                                     [](double f, const Keyframe &k) { return f < k.frame; });
    if (at != t->keys.begin())
        key.interpolation = std::prev(at)->interpolation;
    t->keys.insert(at, key);
    return animation;
}

std::vector<AnimationTrack> withoutKey(std::vector<AnimationTrack> animation, int component, int property, double frame)
{
    AnimationTrack *t = findTrack(animation, component, property);
    if (!t)
        return animation;
    const int i = keyAt(*t, frame);
    if (i >= 0)
        t->keys.erase(t->keys.begin() + i);
    if (t->keys.empty())
        return withoutTrack(std::move(animation), component, property);
    return animation;
}

std::vector<AnimationTrack> withoutTrack(std::vector<AnimationTrack> animation, int component, int property)
{
    if (AnimationTrack *t = findTrack(animation, component, property))
        animation.erase(animation.begin() + (t - animation.data()));
    return animation;
}

std::vector<AnimationTrack> withInterpolation(std::vector<AnimationTrack> animation, int component, int property,
                                              double frame, Interpolation interpolation)
{
    AnimationTrack *t = findTrack(animation, component, property);
    if (!t || t->keys.empty())
        return animation;
    int i = keyAt(*t, frame);
    if (i < 0) { // between keys: the one before (or the first)
        i = 0;
        for (int k = 0; k < int(t->keys.size()); ++k)
            if (t->keys[size_t(k)].frame < frame)
                i = k;
    }
    t->keys[size_t(i)].interpolation = interpolation;
    return animation;
}

int channelCount(PropertyType type)
{
    const int n = numberCount(type);
    return n > 0 ? n : (type == PropertyType::Bool || type == PropertyType::Enum ? 1 : 0);
}

double channel(PropertyType type, const QVariant &value, int index)
{
    if (type == PropertyType::Bool)
        return value.toBool() ? 1.0 : 0.0;
    if (type == PropertyType::Enum)
        return value.toInt();
    return toNumbers(type, value)[size_t(qBound(0, index, 3))];
}

QVariant withChannel(PropertyType type, const QVariant &value, int index, double number)
{
    if (type == PropertyType::Bool)
        return number >= 0.5;
    if (type == PropertyType::Enum)
        return int(std::lround(number));
    Numbers n = toNumbers(type, value);
    n[size_t(qBound(0, index, 3))] = number;
    return fromNumbers(type, n);
}

double tangent(const AnimationTrack &track, PropertyType type, int key, int index)
{
    if (key < 0 || key >= int(track.keys.size()) || !interpolates(type))
        return 0.0;
    std::vector<Numbers> values;
    values.reserve(track.keys.size());
    for (const Keyframe &k : track.keys)
        values.push_back(toNumbers(type, k.value));
    return autoTangent(track.keys, values, size_t(key), index);
}

bool applyTo(Entity &entity, double frame)
{
    bool changed = false;
    const auto &comps = components();
    for (const AnimationTrack &t : entity.animation) {
        int ci = 0, pi = 0;
        if (t.keys.empty() || !resolve(t, &ci, &pi))
            continue;
        const ComponentDesc &c = comps[size_t(ci)];
        if (!c.present(entity))
            continue;
        const QVariant value = evaluate(t, c.properties[size_t(pi)].type, frame);
        if (!value.isValid())
            continue;
        // Compared after setting: the entity may store less precision (floats) than the value.
        const QVariant before = c.get(entity, pi);
        c.set(entity, pi, value);
        changed = changed || c.get(entity, pi) != before;
    }
    return changed;
}

void apply(Scene &scene)
{
    const double frame = scene.frame();
    std::vector<EntityId> ids;
    for (const Entity &e : scene.entities()) {
        if (e.animation.empty())
            continue;
        Entity copy = e;
        if (applyTo(copy, frame))
            ids.push_back(e.id);
    }
    for (EntityId id : ids)
        scene.modifyEntity(id, [frame](Entity &e) { applyTo(e, frame); });
}

std::vector<double> keyFrames(const Entity &entity)
{
    std::vector<double> frames;
    for (const AnimationTrack &t : entity.animation)
        for (const Keyframe &k : t.keys)
            frames.push_back(k.frame);
    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end(), [](double a, double b) { return std::abs(a - b) < kSameFrame; }),
                 frames.end());
    return frames;
}

QString interpolationName(Interpolation interpolation)
{
    switch (interpolation) {
    case Interpolation::Constant: return QStringLiteral("constant");
    case Interpolation::Linear: return QStringLiteral("linear");
    case Interpolation::Bezier: return QStringLiteral("bezier");
    }
    return QStringLiteral("bezier");
}

std::optional<Interpolation> interpolationFromName(const QString &name)
{
    if (name == QLatin1String("constant"))
        return Interpolation::Constant;
    if (name == QLatin1String("linear"))
        return Interpolation::Linear;
    if (name == QLatin1String("bezier"))
        return Interpolation::Bezier;
    return std::nullopt;
}
} // namespace Animation
