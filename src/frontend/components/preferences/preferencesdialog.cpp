#include "preferencesdialog.h"

#include <QBoxLayout>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>

#include "frontend/core/appsettings.h"

PreferencesDialog::PreferencesDialog(QRhi::Implementation running, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Preferences");
    auto *layout = new QVBoxLayout(this);

    // Graphics
    auto *graphics = new QGroupBox("Graphics", this);
    auto *form = new QFormLayout(graphics);
    m_backend = new QComboBox(graphics);
    const QRhi::Implementation saved = AppSettings::backend();
    for (QRhi::Implementation b : AppSettings::availableBackends()) {
        m_backend->addItem(AppSettings::backendLabel(b), int(b));
        if (b == saved)
            m_backend->setCurrentIndex(m_backend->count() - 1);
    }
    m_backend->setToolTip("The graphics API the viewport and the renders use");
    form->addRow("Backend", m_backend);
    auto *note = new QLabel(QStringLiteral("In use: %1. A change applies when Lune starts again.").arg(AppSettings::backendLabel(running)),
                            graphics);
    note->setEnabled(false);
    note->setWordWrap(true);
    form->addRow(note);
    layout->addWidget(graphics);

    // Simulation
    auto *simulation = new QGroupBox("Simulation", this);
    auto *simForm = new QFormLayout(simulation);
    m_fluidCache = new QSpinBox(simulation);
    m_fluidCache->setRange(AppSettings::MinFluidCacheMegabytes, AppSettings::MaxFluidCacheMegabytes);
    m_fluidCache->setSingleStep(256);
    m_fluidCache->setSuffix(" MB");
    m_fluidCache->setValue(AppSettings::fluidCacheMegabytes());
    m_fluidCache->setToolTip("Video memory each fluid volume may use to keep simulated frames, for scrubbing without simulating again");
    simForm->addRow("Fluid cache", m_fluidCache);
    auto *cacheNote = new QLabel("Per fluid volume, in video memory. Frames are kept sparse (only the parts with smoke), so "
                                 "how many fit depends on the simulation.",
                                 simulation);
    cacheNote->setEnabled(false);
    cacheNote->setWordWrap(true);
    simForm->addRow(cacheNote);
    layout->addWidget(simulation);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

int PreferencesDialog::fluidCacheMegabytes() const
{
    return m_fluidCache->value();
}

QRhi::Implementation PreferencesDialog::backend() const
{
    return QRhi::Implementation(m_backend->currentData().toInt());
}
