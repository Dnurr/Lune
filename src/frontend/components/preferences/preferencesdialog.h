#pragma once

#include <QDialog>
#include <rhi/qrhi.h>

class QComboBox;
class QSpinBox;

// Edit > Preferences: settings of the application, not of the scene (AppSettings): the graphics
// backend (from the next start) and the fluid cache's memory (at once).
class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    // `running`: the backend the editor uses now (shown next to the choice).
    explicit PreferencesDialog(QRhi::Implementation running, QWidget *parent = nullptr);

    QRhi::Implementation backend() const;
    int fluidCacheMegabytes() const;

private:
    QComboBox *m_backend = nullptr;
    QSpinBox *m_fluidCache = nullptr;
};
