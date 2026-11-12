#pragma once

#include <QColor>

class QApplication;

// Material 3 inspired theme: Fusion style + QPalette + generated stylesheet.
//
// Usage (before creating any window):
//     Theme::apply(app, Theme::Mode::Dark);
//
// Button variants via dynamic property:
//     button->setProperty("variant", "tonal");    // or "outlined", "text" (default: filled)
namespace Theme
{
enum class Mode
{
    System, // follows the OS light/dark setting and updates live
    Light,
    Dark,
};

// Material 3 color roles. Edit these (or build your own) to change the accent.
struct Palette
{
    QColor primary, onPrimary, primaryContainer, onPrimaryContainer;
    QColor secondaryContainer, onSecondaryContainer;
    QColor surface, surfaceContainerLowest, surfaceContainerLow, surfaceContainer,
        surfaceContainerHigh, surfaceContainerHighest;
    QColor onSurface, onSurfaceVariant;
    QColor outline, outlineVariant;
    QColor inverseSurface, inverseOnSurface;
    QColor error, onError;
};

Palette darkPalette();
Palette lightPalette();

void apply(QApplication &app, Mode mode = Mode::System);
void apply(QApplication &app, const Palette &palette, bool dark);

Mode currentMode();
} // namespace Theme
