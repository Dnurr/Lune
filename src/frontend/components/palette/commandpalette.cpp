#include "commandpalette.h"

#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QStyledItemDelegate>

#include <algorithm>
#include <functional>

namespace
{
constexpr int kShortcutRole = Qt::UserRole + 1;
constexpr int kEntryRole = Qt::UserRole + 2;
constexpr int kMaxVisibleRows = 8;
constexpr int kWidth = 560;
constexpr int kTopMargin = 56;

// Menu texts carry mnemonics ("&File") and ellipses ("Import OBJ...").
QString cleanText(QString s)
{
    s.remove(QLatin1Char('&'));
    if (s.endsWith(QLatin1String("...")))
        s.chop(3);
    return s.trimmed();
}

// Fuzzy match: higher is better, negative = no match.
// Substring beats subsequence; matches at the start of a word score more.
int score(const QString &text, const QString &query)
{
    if (query.isEmpty())
        return 0;
    const QString t = text.toLower();
    const QString q = query.toLower();

    const int at = int(t.indexOf(q));
    if (at >= 0) {
        const bool wordStart = at == 0 || !t[at - 1].isLetterOrNumber();
        return 1000 - at + (wordStart ? 500 : 0);
    }
    // Every query character in order, gaps allowed.
    int s = 0, ti = 0;
    for (const QChar c : q) {
        const int found = int(t.indexOf(c, ti));
        if (found < 0)
            return -1;
        s += (found == ti) ? 10 : 1; // consecutive characters count more
        if (found == 0 || !t[found - 1].isLetterOrNumber())
            s += 5;
        ti = found + 1;
    }
    return s;
}

// Draws the shortcut, right-aligned and muted, after the normal item.
class ShortcutDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        const QString shortcut = index.data(kShortcutRole).toString();
        if (shortcut.isEmpty())
            return;
        painter->save();
        painter->setPen(option.palette.color(QPalette::PlaceholderText));
        painter->drawText(option.rect.adjusted(0, 0, -12, 0), Qt::AlignRight | Qt::AlignVCenter, shortcut);
        painter->restore();
    }
};
} // namespace

CommandPalette::CommandPalette(QMainWindow *window, QWidget *parent)
    : QFrame(parent)
    , m_window(window)
{
    setObjectName("CommandPalette");
    setProperty("glass", true); // the viewport blurs the scene behind it
    setProperty("glassRadius", 14);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);

    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("Type a command...");
    m_search->installEventFilter(this);
    layout->addWidget(m_search);

    m_list = new QListWidget(this);
    m_list->setFocusPolicy(Qt::NoFocus); // typing always goes to the search field
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setItemDelegate(new ShortcutDelegate(m_list));
    m_list->setUniformItemSizes(true);
    layout->addWidget(m_list);

    connect(m_search, &QLineEdit::textChanged, this, &CommandPalette::filter);
    connect(m_list, &QListWidget::itemClicked, this, [this] { runCurrent(); });

    parent->installEventFilter(this); // follow resizes
    hide();
}

void CommandPalette::open()
{
    if (!isVisible())
        m_previousFocus = QApplication::focusWidget();
    collect();
    m_search->clear();
    filter(QString());
    reposition();
    show();
    raise();
    m_search->setFocus();
}

void CommandPalette::close(bool restoreFocus)
{
    if (!isVisible())
        return;
    // Hiding a focused widget moves the focus to the next one in the chain (e.g. an
    // Inspector field, which would then eat W/E/R and Ctrl+Z). Put it back instead.
    QWidget *back = m_previousFocus ? m_previousFocus.data() : parentWidget();
    hide();
    if (restoreFocus)
        back->setFocus();
}

void CommandPalette::collect()
{
    m_entries.clear();
    auto add = [this](QAction *a, const QString &prefix) {
        if (!a || a->isSeparator() || !a->isEnabled() || a->menu() || a->text().isEmpty()
            || a->property("paletteHidden").toBool())
            return;
        const QString label = prefix.isEmpty() ? cleanText(a->text()) : prefix + QStringLiteral(": ") + cleanText(a->text());
        // The same action can sit in several menus (e.g. Import OBJ): list it once.
        if (std::any_of(m_entries.begin(), m_entries.end(), [a](const Entry &e) { return e.action == a; }))
            return;
        m_entries.push_back({ a, label, a->shortcut().toString(QKeySequence::NativeText) });
    };

    std::function<void(QMenu *, const QString &)> walk = [&](QMenu *menu, const QString &prefix) {
        for (QAction *a : menu->actions()) {
            if (a->menu())
                walk(a->menu(), prefix + QStringLiteral(": ") + cleanText(a->text()));
            else
                add(a, prefix);
        }
    };
    for (QAction *top : m_window->menuBar()->actions()) {
        if (top->menu())
            walk(top->menu(), cleanText(top->text()));
    }
    for (QAction *a : m_window->actions())
        add(a, QString());
}

void CommandPalette::filter(const QString &query)
{
    struct Match
    {
        int index;
        int score;
    };
    std::vector<Match> matches;
    for (int i = 0; i < int(m_entries.size()); ++i) {
        if (!m_entries[size_t(i)].action)
            continue;
        const int s = score(m_entries[size_t(i)].label, query.trimmed());
        if (s >= 0)
            matches.push_back({ i, s });
    }
    std::stable_sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) { return a.score > b.score; });

    m_list->clear();
    for (const Match &m : matches) {
        const Entry &e = m_entries[size_t(m.index)];
        auto *item = new QListWidgetItem(e.label, m_list);
        item->setData(kShortcutRole, e.shortcut);
        item->setData(kEntryRole, m.index);
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);

    // Height follows the number of results (up to kMaxVisibleRows).
    const int rowHeight = m_list->count() > 0 ? m_list->sizeHintForRow(0) : 0;
    const int rows = qMin(m_list->count(), kMaxVisibleRows);
    m_list->setFixedHeight(rows * rowHeight + (rows > 0 ? 8 : 0));
    m_list->setVisible(rows > 0);
    reposition();
}

void CommandPalette::runCurrent()
{
    QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return;
    const QPointer<QAction> action = m_entries[size_t(item->data(kEntryRole).toInt())].action;
    close(); // first, so the action runs with the palette out of the way
    if (action)
        action->trigger();
}

void CommandPalette::reposition()
{
    QWidget *p = parentWidget();
    const int w = qMin(kWidth, p->width() - 48);
    adjustSize();
    setGeometry((p->width() - w) / 2, kTopMargin, w, sizeHint().height());
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize && isVisible())
        reposition();

    if (watched == m_search) {
        if (event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            const int row = m_list->currentRow();
            switch (key->key()) {
            case Qt::Key_Escape:
                close();
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                runCurrent();
                return true;
            case Qt::Key_Down:
                if (m_list->count() > 0)
                    m_list->setCurrentRow((row + 1) % m_list->count());
                return true;
            case Qt::Key_Up:
                if (m_list->count() > 0)
                    m_list->setCurrentRow((row - 1 + m_list->count()) % m_list->count());
                return true;
            default:
                break;
            }
        } else if (event->type() == QEvent::FocusOut) {
            // Clicked somewhere else: close (clicks on the list keep focus in the search field).
            QWidget *focus = QApplication::focusWidget();
            if (!focus || !isAncestorOf(focus))
                close(false); // the focus already went where the user clicked
        }
    }
    return QFrame::eventFilter(watched, event);
}
