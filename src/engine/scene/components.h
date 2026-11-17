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
    File,    // QString: absolute path (empty = none); relative to the scene in files
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
    QString fileFilter;  // File: filter of the open dialog, e.g. "HDR images (*.hdr)"
    // Rows with the same group are shown under a small heading in the Inspector, and saved
    // as "group.name" (names only need to be unique within their group).
    QString group;

    QString key() const { return group.isEmpty() ? name : group + QLatin1Char('.') + name; }
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
    // Optional components only: takes it away (Inspector: Remove Component).
    std::function<void(Entity &)> remove;
    // Optional components only: can the user add it to this entity (Inspector: Add Component)?
    // Unset: any entity that doesn't have it yet.
    std::function<bool(const Entity &)> canAdd;

    bool addable(const Entity &e) const { return add && !present(e) && (!canAdd || canAdd(e)); }
};

// All components, in Inspector order.
const std::vector<ComponentDesc> &components();
