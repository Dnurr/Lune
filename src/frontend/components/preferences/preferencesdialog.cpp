#include "preferencesdialog.h"

#include <QBoxLayout>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>

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

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QRhi::Implementation PreferencesDialog::backend() const
{
    return QRhi::Implementation(m_backend->currentData().toInt());
}
