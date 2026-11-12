#pragma once

#include <QFrame>
#include <QPointer>
#include <vector>

class QAction;
class QLineEdit;
class QListWidget;
class QMainWindow;

// Command palette (Ctrl+K): type a few letters, pick an action, Enter runs it.
// It lists every enabled action of the window's menus (as "Menu: Action") plus the
// window's own actions, so anything added to a menu shows up here automatically.
// An action with the dynamic property "paletteHidden" = true is left out.
//
// Floats at the top of its parent (the viewport) as a glass panel; Esc or clicking
// elsewhere closes it.
class CommandPalette : public QFrame
{
    Q_OBJECT

public:
    explicit CommandPalette(QMainWindow *window, QWidget *parent);

    void open();
    // restoreFocus: give the keyboard focus back to whoever had it before open().
    void close(bool restoreFocus = true);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Entry
    {
        QPointer<QAction> action;
        QString label;    // "Add: Cube"
        QString shortcut; // "Shift+A"
    };

    void collect();
    void filter(const QString &query);
    void runCurrent();
    void reposition();

    QMainWindow *m_window;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    std::vector<Entry> m_entries;
    QPointer<QWidget> m_previousFocus;
};
