#include "displaymenu.h"

#include <QApplication>
#include <QBoxLayout>
#include <QChildEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QToolButton>

#include "frontend/components/panel/panel.h"
#include "frontend/components/widgets/draglabel.h"
#include "frontend/core/displaysettings.h"

// Note: inside this class (a QFrame) the bare name "Panel" means the QFrame::Panel frame
// style, so our Panel class is written ::Panel.

namespace
{
constexpr int kMargin = 12;           // from the viewport edges and the right-side panels
constexpr double kExposureDrag = 0.02; // EV per pixel when dragging the label
} // namespace

DisplayMenu::DisplayMenu(DisplaySettings *settings, QWidget *viewport)
    : QFrame(viewport)
    , m_settings(settings)
{
    setObjectName("DisplayMenu");
    setProperty("glass", true); // the viewport blurs the scene behind it
    setProperty("glassRadius", 10);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 4, 6, 6);
    root->setSpacing(4);

    m_header = new QToolButton(this);
    m_header->setObjectName("DisplayMenuHeader");
    m_header->setText("Display");
    m_header->setCheckable(true);
    m_header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_header->setArrowType(Qt::DownArrow);
    m_header->setCursor(Qt::PointingHandCursor);
    m_header->setFocusPolicy(Qt::NoFocus);
    root->addWidget(m_header, 0, Qt::AlignRight);

    m_body = new QWidget(this);
    auto *grid = new QGridLayout(m_body);
    grid->setContentsMargins(6, 2, 6, 4);
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(6);

    // Exposure: drag the label to scrub (Shift = fine, Ctrl = coarse), or type a value.
    auto *exposureLabel = new DragLabel("exposure", m_body);
    m_exposure = new QDoubleSpinBox(m_body);
    m_exposure->setRange(DisplaySettings::MinExposure, DisplaySettings::MaxExposure);
    m_exposure->setDecimals(2);
    m_exposure->setSingleStep(0.1);
    m_exposure->setSuffix(" EV");
    m_exposure->setKeyboardTracking(false);
    m_exposure->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_exposure->setMinimumWidth(90);
    auto *reset = new QToolButton(m_body);
    reset->setObjectName("DisplayMenuReset");
    reset->setText("0");
    reset->setToolTip("Reset exposure to 0 EV");
    reset->setCursor(Qt::PointingHandCursor);

    auto *tonemapLabel = new QLabel("tonemap", m_body);
    m_tonemapper = new QComboBox(m_body);
    m_tonemapper->addItems(DisplaySettings::tonemapperNames());

    grid->addWidget(exposureLabel, 0, 0);
    grid->addWidget(m_exposure, 0, 1);
    grid->addWidget(reset, 0, 2);
    grid->addWidget(tonemapLabel, 1, 0);
    grid->addWidget(m_tonemapper, 1, 1, 1, 2);
    root->addWidget(m_body);
    m_body->hide();

    // UI -> settings
    connect(m_header, &QToolButton::toggled, this, &DisplayMenu::setOpen);
    connect(exposureLabel, &DragLabel::dragged, this, [this](int dx, Qt::KeyboardModifiers mods) {
        m_settings->setExposure(m_settings->exposure() + dx * kExposureDrag * dragFactor(mods));
    });
    connect(m_exposure, &QDoubleSpinBox::valueChanged, this, [this](double ev) {
        if (!m_syncing)
            m_settings->setExposure(ev);
    });
    connect(reset, &QToolButton::clicked, this, [this] { m_settings->setExposure(0.0); });
    connect(m_tonemapper, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (!m_syncing)
            m_settings->setTonemapper(index);
    });

    // Settings -> UI (also changes made elsewhere: View menu, loading a scene)
    connect(m_settings, &DisplaySettings::changed, this, &DisplayMenu::syncFromSettings);
    syncFromSettings();

    // Stay in the corner when the viewport resizes or the right-side panels move.
    viewport->installEventFilter(this);
    for (QObject *child : viewport->children())
        watch(child);
    reposition();
    raise();
}

void DisplayMenu::watch(QObject *sibling)
{
    if (sibling != this && qobject_cast<::Panel *>(sibling))
        sibling->installEventFilter(this);
}

void DisplayMenu::syncFromSettings()
{
    m_syncing = true;
    m_exposure->setValue(m_settings->exposure());
    m_tonemapper->setCurrentIndex(m_settings->tonemapper());
    m_syncing = false;
    // The header shows the current choice, so it's readable even when closed.
    const QString ev = QString::number(m_settings->exposure(), 'f', 1);
    m_header->setText(QStringLiteral("Display  %1 EV  %2")
                          .arg(m_settings->exposure() > 0 ? "+" + ev : ev,
                               DisplaySettings::tonemapperNames().value(m_settings->tonemapper())));
    reposition();
}

bool DisplayMenu::isOpen() const
{
    return m_body->isVisible();
}

void DisplayMenu::setOpen(bool open)
{
    if (open == isOpen())
        return;
    m_body->setVisible(open);
    m_header->setChecked(open);
    m_header->setArrowType(open ? Qt::UpArrow : Qt::DownArrow);
    // While open, watch clicks anywhere in the app to close on a click outside.
    if (open) {
        qApp->installEventFilter(this);
        raise();
    } else {
        qApp->removeEventFilter(this);
    }
    reposition();
}

void DisplayMenu::reposition()
{
    QWidget *p = parentWidget();
    if (!p)
        return;
    // Right edge of the free area: left of the panels docked on the right.
    int right = p->width() - kMargin;
    for (QObject *child : p->children()) {
        auto *panel = qobject_cast<::Panel *>(child);
        if (panel && panel->isVisible() && !panel->isNested() && panel->side() == ::Panel::Side::Right)
            right = qMin(right, panel->geometry().left() - kMargin);
    }
    adjustSize();
    move(right - width(), kMargin);
}

void DisplayMenu::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && isOpen()) {
        setOpen(false);
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

bool DisplayMenu::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::ChildAdded:
        if (watched == parentWidget())
            watch(static_cast<QChildEvent *>(event)->child());
        break;
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::Show:
    case QEvent::Hide:
        if (watched == parentWidget() || qobject_cast<::Panel *>(watched))
            reposition();
        break;
    case QEvent::MouseButtonPress:
        // Click outside while open: close. Clicks in a popup (the combo box list) don't count.
        if (isOpen() && !QApplication::activePopupWidget()) {
            auto *w = qobject_cast<QWidget *>(watched);
            if (w && w != this && !isAncestorOf(w))
                setOpen(false);
        }
        break;
    default:
        break;
    }
    return QFrame::eventFilter(watched, event);
}
