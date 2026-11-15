#include "theme.h"

#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFont>
#include <QMenu>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTemporaryDir>

#include <algorithm>
#include <utility>
#include <vector>

namespace Theme
{
namespace
{

Mode g_mode = Mode::System;
bool g_initialized = false;

QColor withAlpha(const QColor &c, qreal alpha)
{
    QColor r(c);
    r.setAlphaF(alpha);
    return r;
}

// Opaque mix of `overlay` on top of `base` (Material "state layer").
QColor blend(const QColor &base, const QColor &overlay, qreal amount)
{
    return QColor::fromRgbF(base.redF() + (overlay.redF() - base.redF()) * amount,
                            base.greenF() + (overlay.greenF() - base.greenF()) * amount,
                            base.blueF() + (overlay.blueF() - base.blueF()) * amount);
}

QString css(const QColor &c)
{
    if (c.alpha() == 255)
        return c.name(QColor::HexRgb);
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

// Stylesheets can only load images from files/resources, so small SVG icons are
// generated with the current colors. The color is part of the file name so that
// switching theme never hits a stale cached pixmap.
QString writeIcon(const QString &name, const QColor &color, const QString &body)
{
    static QTemporaryDir dir;
    const QString hex = color.name(QColor::HexRgb).mid(1);
    const QString path = dir.filePath(QStringLiteral("%1-%2.svg").arg(name, hex));
    if (!QFile::exists(path))
    {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
        {
            QString svg = QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\">%1</svg>").arg(body);
            svg.replace(QStringLiteral("COLOR"), color.name(QColor::HexRgb));
            file.write(svg.toUtf8());
        }
    }
    return path;
}

// Menus are top-level windows: make them translucent so the rounded corners
// from the stylesheet are actually visible instead of square window corners.
class PopupFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        if (event->type() == QEvent::Polish)
        {
            if (auto *menu = qobject_cast<QMenu *>(obj); menu && !menu->testAttribute(Qt::WA_TranslucentBackground))
            {
                menu->setAttribute(Qt::WA_TranslucentBackground);
                menu->setWindowFlag(Qt::FramelessWindowHint);
                menu->setWindowFlag(Qt::NoDropShadowWindowHint);
            }
        }
        return QObject::eventFilter(obj, event);
    }
};

const char *kStyleSheet = R"QSS(
* { outline: 0; }

QToolTip {
    background-color: @inverseSurface; color: @inverseOnSurface;
    border: none; border-radius: 4px; padding: 6px 8px;
}

/* ---------- Menus ---------- */
QMenuBar {
    background-color: @surfaceContainer; color: @onSurface;
    border-bottom: 1px solid @outlineVariant; padding: 2px 4px;
}
QMenuBar::item { background: transparent; padding: 6px 12px; border-radius: 8px; }
QMenuBar::item:selected { background-color: @hover; }
QMenuBar::item:pressed { background-color: @pressed; }

QMenu {
    background-color: @surfaceContainer; color: @onSurface;
    border: 1px solid @outlineVariant; border-radius: 12px; padding: 6px;
}
QMenu::item { background: transparent; padding: 8px 32px 8px 12px; border-radius: 8px; }
QMenu::item:selected { background-color: @hover; }
QMenu::item:disabled { color: @disabled; }
QMenu::icon { padding-left: 10px; }
QMenu::separator { height: 1px; background-color: @outlineVariant; margin: 6px 8px; }
QMenu::right-arrow { image: url("@iconRight"); width: 16px; height: 16px; margin-right: 6px; }

/* ---------- Tool bars / status bar / docks ---------- */
QToolBar {
    background-color: @surfaceContainer; border: none;
    border-bottom: 1px solid @outlineVariant; padding: 4px; spacing: 4px;
}
QToolBar::separator { background-color: @outlineVariant; width: 1px; height: 1px; margin: 6px 4px; }
QToolButton {
    background: transparent; color: @onSurface; border: none;
    border-radius: 8px; padding: 6px;
}
QToolButton:hover { background-color: @hover; }
QToolButton:pressed { background-color: @pressed; }
QToolButton:checked { background-color: @secondaryContainer; color: @onSecondaryContainer; }
QToolButton:disabled { color: @disabled; }
QToolButton::menu-indicator { image: none; }

QStatusBar {
    background-color: @surfaceContainer; color: @onSurfaceVariant;
    border-top: 1px solid @outlineVariant;
}
QStatusBar::item { border: none; }

QMainWindow::separator { background-color: @outlineVariant; width: 1px; height: 1px; }
QMainWindow::separator:hover { background-color: @primary; }

QDockWidget {
    color: @onSurface;
    titlebar-close-icon: url("@iconClose"); titlebar-normal-icon: url("@iconFloat");
}
QDockWidget::title {
    background-color: @surfaceContainer; color: @onSurfaceVariant;
    padding: 8px 12px; text-align: left;
}
QDockWidget::close-button, QDockWidget::float-button {
    border: none; background: transparent; border-radius: 6px; padding: 2px;
}
QDockWidget::close-button:hover, QDockWidget::float-button:hover { background-color: @hover; }

/* Floating panels over the viewport (components/panel): "glass" look.
   Translucent gray; the viewport draws a blurred copy of the scene behind them. */
Panel {
    background-color: @glass; color: @onSurface;
    border: 1px solid @glassBorder; border-radius: 12px;
}
QLabel#PanelTitle { color: @onSurfaceVariant; font-weight: 600; background: transparent; }
/* Nested panels: a light tint on top of the host's glass */
Panel Panel { background-color: @glassNested; border-radius: 10px; }
/* Read-only values in the Inspector */
QLabel#PropertyValue { color: @onSurfaceVariant; }
/* File properties in the Inspector: a field-like button with the file name, and a clear button */
QPushButton#FileButton, QPushButton#FileClearButton {
    background-color: @surfaceContainerLow; color: @onSurface; border: 1px solid @outline;
    border-radius: 8px; padding: 4px 8px; min-height: 18px; font-weight: normal; text-align: left;
}
QPushButton#FileClearButton { padding: 4px 0; text-align: center; color: @onSurfaceVariant; }
QPushButton#FileButton:hover, QPushButton#FileClearButton:hover { border-color: @primary; background-color: @surfaceContainerLow; }

/* Command palette (components/palette): glass, a bit more opaque than panels */
QFrame#CommandPalette {
    background-color: @glassStrong; border: 1px solid @glassBorder; border-radius: 14px;
}
QFrame#CommandPalette QLineEdit, QFrame#CommandPalette QLineEdit:focus {
    background: transparent; border: none; border-bottom: 1px solid @glassBorder;
    border-radius: 0; padding: 10px 12px; font-size: 11pt;
}
QFrame#CommandPalette QAbstractItemView { background: transparent; border: none; padding: 2px; }
QFrame#CommandPalette QAbstractItemView::item { padding: 7px 10px; border-radius: 8px; }
QFrame#CommandPalette QAbstractItemView::item:selected {
    background-color: @primaryContainer; color: @onPrimaryContainer;
}

/* Frame statistics over the viewport (components/viewport/statsoverlay) */
QLabel#StatsOverlay {
    background-color: @glassStrong; color: @onSurfaceVariant;
    border: 1px solid @glassBorder; border-radius: 10px; padding: 8px 12px;
    font-family: "Cascadia Mono", "Consolas", monospace; font-size: 9pt;
}

/* Display dropdown in the viewport corner (components/viewport/displaymenu) */
QFrame#DisplayMenu {
    background-color: @glassStrong; color: @onSurface;
    border: 1px solid @glassBorder; border-radius: 10px;
}
QToolButton#DisplayMenuHeader { color: @onSurfaceVariant; padding: 4px 6px; font-weight: 600; }
QToolButton#DisplayMenuHeader:checked { background: transparent; color: @onSurface; }
QToolButton#DisplayMenuReset { padding: 2px 8px; border: 1px solid @glassBorder; }
QFrame#DisplayMenu QAbstractSpinBox, QFrame#DisplayMenu QComboBox { padding: 4px 8px; min-height: 18px; }
QFrame#DisplayMenu QAbstractSpinBox:focus, QFrame#DisplayMenu QComboBox:focus,
QFrame#DisplayMenu QComboBox:on { padding: 3px 7px; }
QFrame#DisplayMenu QComboBox { padding-right: 30px; }
/* MSVC limits one string literal to ~16 KB: the style sheet is split in pieces. */
)QSS" R"QSS(
/* Camera mode bar, top center of the viewport */
QFrame#CameraMenu {
    background-color: @glassStrong; color: @onSurface;
    border: 1px solid @glassBorder; border-radius: 10px;
}
QToolButton#CameraMenuButton { color: @onSurfaceVariant; padding: 3px 10px; border-radius: 7px; font-weight: 600; }
QToolButton#CameraMenuButton:checked { background-color: @secondaryContainer; color: @onSecondaryContainer; }
QLabel#CameraMenuSpeed { color: @onSurfaceVariant; padding: 0 8px 0 6px; background: transparent; }

/* Lists inside a panel sit directly on the glass */
Panel QAbstractItemView { background: transparent; border: none; padding: 0; }

QSplitter::handle { background-color: @outlineVariant; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QSplitter::handle:hover { background-color: @primary; }

/* ---------- Buttons ---------- */
QPushButton {
    background-color: @primary; color: @onPrimary;
    border: 1px solid @primary; border-radius: 20px;
    padding: 9px 24px; min-height: 20px; font-weight: 600;
}
QPushButton:hover { background-color: @primaryHover; border-color: @primaryHover; }
QPushButton:pressed { background-color: @primaryPressed; border-color: @primaryPressed; }
QPushButton:disabled { background-color: @disabledContainer; border-color: transparent; color: @disabled; }

QPushButton[variant="tonal"] {
    background-color: @secondaryContainer; color: @onSecondaryContainer; border-color: @secondaryContainer;
}
QPushButton[variant="tonal"]:hover { background-color: @tonalHover; border-color: @tonalHover; }
QPushButton[variant="tonal"]:pressed { background-color: @tonalPressed; border-color: @tonalPressed; }

QPushButton[variant="outlined"] { background: transparent; color: @primary; border-color: @outline; }
QPushButton[variant="outlined"]:hover { background-color: @primaryHoverLayer; }
QPushButton[variant="outlined"]:pressed { background-color: @primaryPressedLayer; }

QPushButton[variant="text"], QPushButton:flat {
    background: transparent; color: @primary; border-color: transparent; padding: 9px 12px;
}
QPushButton[variant="text"]:hover, QPushButton:flat:hover { background-color: @primaryHoverLayer; }
QPushButton[variant="text"]:pressed, QPushButton:flat:pressed { background-color: @primaryPressedLayer; }

QPushButton[variant="outlined"]:disabled, QPushButton[variant="text"]:disabled, QPushButton:flat:disabled {
    background: transparent; color: @disabled;
}
QPushButton[variant="outlined"]:disabled { border-color: @disabledContainer; }
QPushButton::menu-indicator { image: url("@iconDownOnPrimary"); subcontrol-position: right center; right: 10px; width: 16px; }

/* ---------- Text inputs, combo & spin boxes ---------- */
QLineEdit, QPlainTextEdit, QTextEdit, QAbstractSpinBox, QComboBox {
    background-color: @surfaceContainerLow; color: @onSurface;
    border: 1px solid @outline; border-radius: 8px; padding: 8px 12px;
    selection-background-color: @primary; selection-color: @onPrimary;
}
QPlainTextEdit, QTextEdit { padding: 4px; }
QLineEdit:hover, QPlainTextEdit:hover, QTextEdit:hover, QAbstractSpinBox:hover, QComboBox:hover {
    border-color: @onSurface;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QAbstractSpinBox:focus, QComboBox:focus,
QComboBox:on {
    border: 2px solid @primary; padding: 7px 11px;
}
QPlainTextEdit:focus, QTextEdit:focus { padding: 3px; }
QLineEdit:disabled, QPlainTextEdit:disabled, QTextEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled {
    color: @disabled; border-color: @disabledContainer; background-color: transparent;
}

QComboBox { combobox-popup: 0; padding-right: 36px; }
QComboBox:focus, QComboBox:on { padding-right: 35px; }
QComboBox::drop-down {
    subcontrol-origin: padding; subcontrol-position: center right;
    width: 32px; border: none; background: transparent;
}
QComboBox::down-arrow { image: url("@iconDown"); width: 20px; height: 20px; }
QComboBox::down-arrow:on { image: url("@iconUp"); }
QComboBox QAbstractItemView {
    background-color: @surfaceContainer; color: @onSurface;
    border: 1px solid @outlineVariant; border-radius: 8px; padding: 4px;
    selection-background-color: @secondaryContainer; selection-color: @onSecondaryContainer;
}
QComboBox QAbstractItemView::item { padding: 6px 10px; border-radius: 6px; min-height: 22px; }
QComboBox QAbstractItemView::item:hover { background-color: @hover; }
QComboBox QAbstractItemView::item:selected { background-color: @secondaryContainer; color: @onSecondaryContainer; }

QAbstractSpinBox { padding-right: 30px; min-height: 22px; }
QAbstractSpinBox:focus { padding-right: 29px; }
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {
    subcontrol-origin: border; width: 26px; border: none; background: transparent;
}
QAbstractSpinBox::up-button { subcontrol-position: top right; margin: 4px 4px 0 0; }
QAbstractSpinBox::down-button { subcontrol-position: bottom right; margin: 0 4px 4px 0; }
QAbstractSpinBox::up-button:hover, QAbstractSpinBox::down-button:hover { background-color: @hover; border-radius: 4px; }
QAbstractSpinBox::up-arrow { image: url("@iconUp"); width: 16px; height: 16px; }
QAbstractSpinBox::down-arrow { image: url("@iconDown"); width: 16px; height: 16px; }

/* ---------- Check boxes & radio buttons ---------- */
QCheckBox, QRadioButton { spacing: 10px; color: @onSurface; background: transparent; }
QCheckBox:disabled, QRadioButton:disabled { color: @disabled; }
QCheckBox::indicator {
    width: 14px; height: 14px; border: 2px solid @onSurfaceVariant; border-radius: 3px;
    background: transparent;
}
QCheckBox::indicator:hover { border-color: @onSurface; }
QCheckBox::indicator:checked { background-color: @primary; border-color: @primary; image: url("@iconCheck"); }
QCheckBox::indicator:indeterminate { background-color: @primary; border-color: @primary; image: url("@iconDash"); }
QCheckBox::indicator:disabled { border-color: @disabled; }
QCheckBox::indicator:checked:disabled, QCheckBox::indicator:indeterminate:disabled {
    background-color: @disabled; border-color: transparent;
}

QRadioButton::indicator {
    width: 16px; height: 16px; border: 2px solid @onSurfaceVariant; border-radius: 10px;
    background: transparent;
}
QRadioButton::indicator:hover { border-color: @onSurface; }
QRadioButton::indicator:checked { border-color: @primary; image: url("@iconRadio"); }
QRadioButton::indicator:disabled { border-color: @disabled; }

/* ---------- Sliders, progress bars, scroll bars ---------- */
QSlider { background: transparent; }
QSlider::groove:horizontal { height: 4px; background-color: @secondaryContainer; border-radius: 2px; }
QSlider::sub-page:horizontal { background-color: @primary; border-radius: 2px; }
QSlider::groove:vertical { width: 4px; background-color: @secondaryContainer; border-radius: 2px; }
QSlider::add-page:vertical { background-color: @primary; border-radius: 2px; }
QSlider::handle { background-color: @primary; border: 2px solid @primary; border-radius: 10px; width: 16px; height: 16px; }
QSlider::handle:horizontal { margin: -8px 0; }
QSlider::handle:vertical { margin: 0 -8px; }
QSlider::handle:hover { border-color: @primaryHoverRing; }
QSlider::handle:disabled { background-color: @disabled; border-color: transparent; }
QSlider::sub-page:horizontal:disabled, QSlider::add-page:vertical:disabled { background-color: @disabledContainer; }

QProgressBar {
    background-color: @secondaryContainer; border: none; border-radius: 3px;
    max-height: 6px; min-height: 6px; color: transparent; text-align: center;
}
QProgressBar::chunk { background-color: @primary; border-radius: 3px; }

QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }
QScrollBar::handle { background-color: @outlineVariant; border-radius: 4px; margin: 2px; }
QScrollBar::handle:vertical { min-height: 32px; }
QScrollBar::handle:horizontal { min-width: 32px; }
QScrollBar::handle:hover { background-color: @outline; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; border: none; background: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QAbstractScrollArea::corner { background: transparent; }

/* ---------- Tabs ---------- */
QTabWidget::pane { border: none; border-top: 1px solid @outlineVariant; top: -1px; }
QTabBar { background: transparent; }
QTabBar::tab {
    background: transparent; color: @onSurfaceVariant;
    padding: 10px 20px; border: none; border-bottom: 3px solid transparent;
    border-top-left-radius: 8px; border-top-right-radius: 8px;
}
QTabBar::tab:hover { background-color: @hover; color: @onSurface; }
QTabBar::tab:selected { color: @primary; border-bottom: 3px solid @primary; }
QTabBar::tab:disabled { color: @disabled; }
QTabBar::close-button { image: url("@iconClose"); subcontrol-position: right; border-radius: 4px; }
QTabBar::close-button:hover { background-color: @hover; }

/* ---------- Group boxes ---------- */
QGroupBox {
    background-color: @surfaceContainerLow; color: @onSurface;
    border: 1px solid @outlineVariant; border-radius: 12px;
    margin-top: 24px; padding: 12px; padding-top: 16px;
}
QGroupBox::title {
    subcontrol-origin: margin; subcontrol-position: top left;
    left: 8px; padding: 0 4px; color: @onSurfaceVariant; font-weight: 600;
}

/* ---------- Item views ---------- */
QAbstractItemView {
    background-color: @surfaceContainerLow; alternate-background-color: @surfaceContainer;
    color: @onSurface; border: 1px solid @outlineVariant; border-radius: 12px;
    selection-background-color: @secondaryContainer; selection-color: @onSecondaryContainer;
}
QListView, QTreeView { padding: 4px; }
QListView::item, QTreeView::item { padding: 6px 8px; border-radius: 6px; }
QListView::item:hover, QTreeView::item:hover, QTableView::item:hover { background-color: @hover; }
QListView::item:selected, QTreeView::item:selected, QTableView::item:selected {
    background-color: @secondaryContainer; color: @onSecondaryContainer;
}
QTreeView::branch { background: transparent; }
QTreeView::branch:has-children:closed { image: url("@iconRight"); }
QTreeView::branch:has-children:open { image: url("@iconDown"); }
QTableView { gridline-color: transparent; }
QTableView::item { padding: 0 10px; border: none; border-bottom: 1px solid @outlineVariant; }
QTableView QTableCornerButton::section { background-color: @surfaceContainer; border: none; }
QHeaderView { background: transparent; border: none; }
QHeaderView::section {
    background-color: @surfaceContainer; color: @onSurfaceVariant;
    border: none; border-bottom: 1px solid @outlineVariant; padding: 8px; font-weight: 600;
}
QHeaderView::section:first { border-top-left-radius: 11px; }
QHeaderView::section:last { border-top-right-radius: 11px; }

QLabel:disabled { color: @disabled; }
)QSS";

QString buildStyleSheet(const Palette &p)
{
    const QString iconChevronDown = QStringLiteral("<path d=\"M7.4 8.6 12 13.2l4.6-4.6L18 10l-6 6-6-6z\" fill=\"COLOR\"/>");
    const QString iconChevronUp = QStringLiteral("<path d=\"M7.4 15.4 12 10.8l4.6 4.6L18 14l-6-6-6 6z\" fill=\"COLOR\"/>");
    const QString iconChevronRight = QStringLiteral("<path d=\"M8.6 16.6 13.2 12 8.6 7.4 10 6l6 6-6 6z\" fill=\"COLOR\"/>");
    const QString iconCheck = QStringLiteral("<path d=\"M9 16.2 4.8 12l-1.4 1.4L9 19 21 7l-1.4-1.4z\" fill=\"COLOR\"/>");
    const QString iconDash = QStringLiteral("<rect x=\"5\" y=\"11\" width=\"14\" height=\"2.4\" rx=\"1\" fill=\"COLOR\"/>");
    const QString iconRadio = QStringLiteral("<circle cx=\"12\" cy=\"12\" r=\"6\" fill=\"COLOR\"/>");
    const QString iconClose = QStringLiteral(
        "<path d=\"M19 6.4 17.6 5 12 10.6 6.4 5 5 6.4 10.6 12 5 17.6 6.4 19 12 13.4 17.6 19 19 17.6 13.4 12z\" fill=\"COLOR\"/>");
    const QString iconFloat = QStringLiteral(
        "<rect x=\"8\" y=\"4\" width=\"12\" height=\"12\" rx=\"2\" fill=\"none\" stroke=\"COLOR\" stroke-width=\"2\"/>"
        "<path d=\"M4 8v10a2 2 0 0 0 2 2h10\" fill=\"none\" stroke=\"COLOR\" stroke-width=\"2\"/>");

    std::vector<std::pair<QString, QString>> tokens = {
        { "primary", css(p.primary) },
        { "onPrimary", css(p.onPrimary) },
        { "primaryContainer", css(p.primaryContainer) },
        { "onPrimaryContainer", css(p.onPrimaryContainer) },
        { "secondaryContainer", css(p.secondaryContainer) },
        { "onSecondaryContainer", css(p.onSecondaryContainer) },
        { "surface", css(p.surface) },
        { "surfaceContainerLowest", css(p.surfaceContainerLowest) },
        { "surfaceContainerLow", css(p.surfaceContainerLow) },
        { "surfaceContainer", css(p.surfaceContainer) },
        { "surfaceContainerHigh", css(p.surfaceContainerHigh) },
        { "surfaceContainerHighest", css(p.surfaceContainerHighest) },
        { "onSurface", css(p.onSurface) },
        { "onSurfaceVariant", css(p.onSurfaceVariant) },
        { "outline", css(p.outline) },
        { "outlineVariant", css(p.outlineVariant) },
        { "inverseSurface", css(p.inverseSurface) },
        { "inverseOnSurface", css(p.inverseOnSurface) },
        { "error", css(p.error) },
        { "onError", css(p.onError) },

        // Material state layers: 8% hover, 12% pressed, 38%/12% disabled
        { "hover", css(withAlpha(p.onSurface, 0.08)) },
        { "pressed", css(withAlpha(p.onSurface, 0.12)) },
        { "disabled", css(withAlpha(p.onSurface, 0.38)) },
        { "disabledContainer", css(withAlpha(p.onSurface, 0.12)) },
        { "primaryHover", css(blend(p.primary, p.onPrimary, 0.08)) },
        { "primaryPressed", css(blend(p.primary, p.onPrimary, 0.12)) },
        { "primaryHoverLayer", css(withAlpha(p.primary, 0.08)) },
        { "primaryPressedLayer", css(withAlpha(p.primary, 0.12)) },
        { "primaryHoverRing", css(withAlpha(p.primary, 0.35)) },
        { "tonalHover", css(blend(p.secondaryContainer, p.onSecondaryContainer, 0.08)) },
        { "tonalPressed", css(blend(p.secondaryContainer, p.onSecondaryContainer, 0.12)) },

        // Glass panels: translucent surface, a lighter tint for nested panels, a faint edge
        { "glassNested", css(withAlpha(p.onSurface, 0.05)) },
        { "glassBorder", css(withAlpha(p.onSurface, 0.10)) },
        { "glass", css(withAlpha(p.surfaceContainer, 0.45)) },
        { "glassStrong", css(withAlpha(p.surfaceContainer, 0.78)) },

        { "iconDown", writeIcon("chevron-down", p.onSurfaceVariant, iconChevronDown) },
        { "iconDownOnPrimary", writeIcon("chevron-down", p.onPrimary, iconChevronDown) },
        { "iconUp", writeIcon("chevron-up", p.onSurfaceVariant, iconChevronUp) },
        { "iconRight", writeIcon("chevron-right", p.onSurfaceVariant, iconChevronRight) },
        { "iconCheck", writeIcon("check", p.onPrimary, iconCheck) },
        { "iconDash", writeIcon("dash", p.onPrimary, iconDash) },
        { "iconRadio", writeIcon("radio", p.primary, iconRadio) },
        { "iconClose", writeIcon("close", p.onSurfaceVariant, iconClose) },
        { "iconFloat", writeIcon("float", p.onSurfaceVariant, iconFloat) },
    };

    // Longest names first so "@surfaceContainerHigh" never eats "@surfaceContainerHighest".
    std::sort(tokens.begin(), tokens.end(),
              [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });

    QString qss = QString::fromUtf8(kStyleSheet);
    for (const auto &[name, value] : tokens)
        qss.replace(QLatin1Char('@') + name, value);
    return qss;
}

QPalette buildPalette(const Palette &p)
{
    QPalette pal;
    pal.setColor(QPalette::Window, p.surface);
    pal.setColor(QPalette::WindowText, p.onSurface);
    pal.setColor(QPalette::Base, p.surfaceContainerLow);
    pal.setColor(QPalette::AlternateBase, p.surfaceContainer);
    pal.setColor(QPalette::Text, p.onSurface);
    pal.setColor(QPalette::PlaceholderText, p.onSurfaceVariant);
    pal.setColor(QPalette::Button, p.surfaceContainerHigh);
    pal.setColor(QPalette::ButtonText, p.onSurface);
    pal.setColor(QPalette::BrightText, p.error);
    pal.setColor(QPalette::ToolTipBase, p.inverseSurface);
    pal.setColor(QPalette::ToolTipText, p.inverseOnSurface);
    pal.setColor(QPalette::Highlight, p.primary);
    pal.setColor(QPalette::HighlightedText, p.onPrimary);
    pal.setColor(QPalette::Link, p.primary);
    pal.setColor(QPalette::LinkVisited, p.onPrimaryContainer);
    pal.setColor(QPalette::Accent, p.primary);
    pal.setColor(QPalette::Light, p.surfaceContainerHighest);
    pal.setColor(QPalette::Midlight, p.surfaceContainerHigh);
    pal.setColor(QPalette::Mid, p.outlineVariant);
    pal.setColor(QPalette::Dark, p.outline);
    pal.setColor(QPalette::Shadow, Qt::black);

    const QColor disabled = blend(p.surface, p.onSurface, 0.38);
    for (auto role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::PlaceholderText })
        pal.setColor(QPalette::Disabled, role, disabled);
    pal.setColor(QPalette::Disabled, QPalette::Highlight, blend(p.surface, p.onSurface, 0.12));
    pal.setColor(QPalette::Disabled, QPalette::HighlightedText, disabled);
    return pal;
}

void applyPalette(QApplication &app, const Palette &p)
{
    if (app.style()->name().compare(QLatin1String("fusion"), Qt::CaseInsensitive) != 0)
        app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QFont font = app.font();
    font.setFamilies({ QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI"),
                       QStringLiteral("Roboto"), QStringLiteral("Inter") });
    font.setPointSizeF(10.0);
    app.setFont(font);

    app.setPalette(buildPalette(p));
    app.setStyleSheet(buildStyleSheet(p));
}

void initOnce(QApplication &app)
{
    if (g_initialized)
        return;
    g_initialized = true;

    app.installEventFilter(new PopupFilter(&app));

    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [&app](Qt::ColorScheme) {
                         if (g_mode == Mode::System)
                             apply(app, Mode::System);
                     });
}

} // namespace

Palette darkPalette()
{
    Palette p;
    p.primary = QColor("#D0BCFF");
    p.onPrimary = QColor("#381E72");
    p.primaryContainer = QColor("#4F378B");
    p.onPrimaryContainer = QColor("#EADDFF");
    p.secondaryContainer = QColor("#4A4458");
    p.onSecondaryContainer = QColor("#E8DEF8");
    p.surface = QColor("#141218");
    p.surfaceContainerLowest = QColor("#0F0D13");
    p.surfaceContainerLow = QColor("#1D1B20");
    p.surfaceContainer = QColor("#211F26");
    p.surfaceContainerHigh = QColor("#2B2930");
    p.surfaceContainerHighest = QColor("#36343B");
    p.onSurface = QColor("#E6E0E9");
    p.onSurfaceVariant = QColor("#CAC4D0");
    p.outline = QColor("#938F99");
    p.outlineVariant = QColor("#49454F");
    p.inverseSurface = QColor("#E6E0E9");
    p.inverseOnSurface = QColor("#322F35");
    p.error = QColor("#F2B8B5");
    p.onError = QColor("#601410");
    return p;
}

Palette lightPalette()
{
    Palette p;
    p.primary = QColor("#6750A4");
    p.onPrimary = QColor("#FFFFFF");
    p.primaryContainer = QColor("#EADDFF");
    p.onPrimaryContainer = QColor("#21005D");
    p.secondaryContainer = QColor("#E8DEF8");
    p.onSecondaryContainer = QColor("#1D192B");
    p.surface = QColor("#FEF7FF");
    p.surfaceContainerLowest = QColor("#FFFFFF");
    p.surfaceContainerLow = QColor("#F7F2FA");
    p.surfaceContainer = QColor("#F3EDF7");
    p.surfaceContainerHigh = QColor("#ECE6F0");
    p.surfaceContainerHighest = QColor("#E6E0E9");
    p.onSurface = QColor("#1D1B20");
    p.onSurfaceVariant = QColor("#49454F");
    p.outline = QColor("#79747E");
    p.outlineVariant = QColor("#CAC4D0");
    p.inverseSurface = QColor("#322F35");
    p.inverseOnSurface = QColor("#F5EFF7");
    p.error = QColor("#B3261E");
    p.onError = QColor("#FFFFFF");
    return p;
}

void apply(QApplication &app, Mode mode)
{
    g_mode = mode;
    initOnce(app);

    // Also drives the native title bar color on Windows 11.
    auto *hints = QGuiApplication::styleHints();
    hints->setColorScheme(mode == Mode::Dark    ? Qt::ColorScheme::Dark
                          : mode == Mode::Light ? Qt::ColorScheme::Light
                                                : Qt::ColorScheme::Unknown);

    const bool dark = mode == Mode::Dark || (mode == Mode::System && hints->colorScheme() == Qt::ColorScheme::Dark);
    applyPalette(app, dark ? darkPalette() : lightPalette());
}

void apply(QApplication &app, const Palette &palette, bool dark)
{
    g_mode = dark ? Mode::Dark : Mode::Light;
    initOnce(app);
    QGuiApplication::styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    applyPalette(app, palette);
}

Mode currentMode()
{
    return g_mode;
}

} // namespace Theme
