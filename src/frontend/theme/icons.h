#pragma once

#include <QIcon>
#include <QString>

// Interface icons: the SVG files in assets/icons (embedded at :/icons/<name>.svg), drawn
// white on transparent and tinted here with the theme's colors:
//   normal          onSurfaceVariant (like the text of the bars' buttons)
//   hovered         onSurface
//   checked (on)    onSecondaryContainer (like a checked button's text)
//   disabled        onSurfaceVariant, faded
// Names are file names without ".svg", e.g. Icons::get("shading").
namespace Icons
{
bool exists(const QString &name);
// The icon, or a null QIcon when there's no such file.
QIcon get(const QString &name);
// The first of `names` that exists (e.g. a specific icon, then a generic one).
QIcon first(std::initializer_list<const char *> names);
} // namespace Icons
