#pragma once

#include <QDialog>
#include <rhi/qrhi.h>

class QComboBox;

// Edit > Preferences: settings of the application, not of the scene (AppSettings). For now the
// graphics backend, which applies on the next start.
class PreferencesDialog : public QDialog
{
    Q_OBJECT

public:
    // `running`: the backend the editor uses now (shown next to the choice).
    explicit PreferencesDialog(QRhi::Implementation running, QWidget *parent = nullptr);

    QRhi::Implementation backend() const;

private:
    QComboBox *m_backend = nullptr;
};
