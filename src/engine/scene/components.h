#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <functional>
#include <vector>

#include "entity.h"

// Reflection of the entity's data: each component lists its properties with a type and
// how to read/write them. The editor builds the Inspector from this (one section per
// component, one row per property) and a generic undo command edits any property,
// so a new component needs a descriptor here and nothing in the UI.
// The same descriptors drive scene saving and loading (engine/io/sceneio.cpp).

enum class PropertyType
{
    Float,   // double
    Vector3, // QVector3D
    Color,   // QColor
    Bool,    // bool
    Text,    // QString
    Enum,    // int: index into PropertyDesc::options
};

struct PropertyDesc
{
    QString name;
    PropertyType type = PropertyType::Float;
    double dragStep = 0.01; // value change per pixel when dragging the label (numbers)
    double minimum = -1e6;
    double maximum = 1e6;
    bool readOnly = false;
    QStringList options; // Enum: one id per value, used in files and shown in the Inspector
};

struct ComponentDesc
{
    QString name;
    std::vector<PropertyDesc> properties;
    std::function<bool(const Entity &)> present;                // does the entity have it?
    std::function<QVariant(const Entity &, int)> get;           // property index -> value
    std::function<void(Entity &, int, const QVariant &)> set;   // never called for read-only ones
    // Optional components only: gives the entity the component with default values
    // (used when loading a file that has it).
    std::function<void(Entity &)> add;
};

// All components, in Inspector order.
const std::vector<ComponentDesc> &components();
