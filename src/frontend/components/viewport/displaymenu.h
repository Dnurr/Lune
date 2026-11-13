#pragma once

#include <QFrame>

class DisplaySettings;
class QComboBox;
class QDoubleSpinBox;
class QToolButton;

// Small glass dropdown in the top-right corner of the free viewport area (left of any
// panel docked on the right): a "Display" header that opens exposure and tonemapping
// controls. Closes on Esc or a click elsewhere. Edits a DisplaySettings, and follows it
// when something else changes it (menu actions, loading a scene).
class DisplayMenu : public QFrame
{
    Q_OBJECT

public:
    DisplayMenu(DisplaySettings *settings, QWidget *viewport);

    void setOpen(bool open);
    bool isOpen() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    void reposition();
    void syncFromSettings();
    void watch(QObject *sibling);

    DisplaySettings *m_settings;
    QToolButton *m_header = nullptr;
    QWidget *m_body = nullptr;
    QDoubleSpinBox *m_exposure = nullptr;
    QComboBox *m_tonemapper = nullptr;
    bool m_syncing = false;
};
