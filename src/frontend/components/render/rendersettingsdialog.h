#pragma once

#include <QDialog>
#include <QHash>

#include "frontend/core/rendersettings.h"

class QCheckBox;
class QLineEdit;
class QSpinBox;

// Render > Render Settings: image size, output folder, and which passes
// Render Passes (F6) writes.
class RenderSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    // `viewportPixels`: shown next to "Viewport size". `scenePath`: for the default folder.
    RenderSettingsDialog(const RenderSettings &settings, const QSize &viewportPixels, const QString &scenePath,
                         QWidget *parent = nullptr);

    RenderSettings settings() const;

private:
    void updateEnabled();

    RenderSettings m_settings; // what isn't edited here (frame) is kept
    QCheckBox *m_viewportSize = nullptr;
    QSpinBox *m_width = nullptr;
    QSpinBox *m_height = nullptr;
    QLineEdit *m_folder = nullptr;
    QHash<QString, QCheckBox *> m_passes; // by pass id
};
