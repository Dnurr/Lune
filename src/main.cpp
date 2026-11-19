#include <QApplication>

#include "frontend/core/batchrender.h"
#include "frontend/editor.h"
#include "frontend/frameanimationdriver.h"
#include "frontend/theme/theme.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // Where QSettings keeps the editor layout (on Windows: HKEY_CURRENT_USER\Software\Lune\Lune).
    QApplication::setOrganizationName(QStringLiteral("Lune"));
    QApplication::setApplicationName(QStringLiteral("Lune"));

    // "Lune.exe --render scene.lune [image.png]": the scene's image, without the editor.
    const QStringList args = QApplication::arguments();
    if (args.size() > 1 && args.at(1) == QLatin1String("--render"))
        return renderFromCommandLine(args.mid(2));

    // Dark gray palette with a navy accent.
    // Surfaces are neutral grays (no hue); only the accent roles are blue/navy.
    // Floating panels use these grays with transparency + a blurred viewport behind
    // (see Theme "glass" tokens and Viewport::setGlassRects).
    Theme::Palette p;

    // Accent (navy): light tone for text/indicators, deep navy for filled containers
    p.primary = QColor("#8FB0F2");
    p.onPrimary = QColor("#0D2452");
    p.primaryContainer = QColor("#1F3B73");
    p.onPrimaryContainer = QColor("#D9E3FF");

    // Secondary (muted navy): selected/checked states
    p.secondaryContainer = QColor("#24324F");
    p.onSecondaryContainer = QColor("#D5DEF5");

    // Surfaces, from darkest to lightest (backgrounds, menus, panels, raised elements)
    p.surfaceContainerLowest = QColor("#0E0E10");
    p.surface = QColor("#131315");
    p.surfaceContainerLow = QColor("#18181B");
    p.surfaceContainer = QColor("#1D1D21");
    p.surfaceContainerHigh = QColor("#26262B");
    p.surfaceContainerHighest = QColor("#303036");

    // Text on surfaces
    p.onSurface = QColor("#E4E4E8");
    p.onSurfaceVariant = QColor("#B4B4BC");

    // Borders and dividers
    p.outline = QColor("#85858F");
    p.outlineVariant = QColor("#36363D");

    // Tooltips
    p.inverseSurface = QColor("#E4E4E8");
    p.inverseOnSurface = QColor("#232327");

    // Errors
    p.error = QColor("#F2B8B5");
    p.onError = QColor("#601410");

    Theme::apply(app, p, true);

    Editor editor;
    editor.show();

    // UI animations advance with the window's frames, not on a timer of their own.
    FrameAnimationDriver animationDriver(editor.windowHandle());
    animationDriver.install();

    // "Lune.exe scene.lune" (or double-clicking an associated .lune file) opens it.
    if (args.size() > 1)
        editor.loadScene(args.at(1));

    return app.exec();
}
