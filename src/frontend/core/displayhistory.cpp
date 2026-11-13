#include "displayhistory.h"

#include <QElapsedTimer>
#include <QUndoCommand>
#include <QUndoStack>

#include "displaysettings.h"

namespace
{
// Same window as the Inspector property edits (commands.cpp).
constexpr qint64 kMergeWindowMs = 600;

// A display change that has already been applied when the command is pushed:
// the first redo does nothing, later redos and undos apply the stored states.
class SetDisplayCommand : public QUndoCommand
{
public:
    using State = DisplayHistory::State;

    SetDisplayCommand(DisplayHistory *history, const State &before, const State &after)
        : m_history(history)
        , m_before(before)
        , m_after(after)
    {
        m_lastEdit.start();
        updateText();
    }

    void redo() override
    {
        if (m_firstRedo) {
            m_firstRedo = false;
            return;
        }
        m_history->apply(m_after);
    }

    void undo() override { m_history->apply(m_before); }

    int id() const override { return 3; }

    // Only exposure changes merge: picking another tonemapper is always its own step.
    bool mergeWith(const QUndoCommand *other) override
    {
        const auto *o = static_cast<const SetDisplayCommand *>(other);
        if (!exposureOnly() || !o->exposureOnly() || m_lastEdit.elapsed() > kMergeWindowMs)
            return false;
        m_after = o->m_after;
        m_lastEdit.restart();
        setObsolete(m_after == m_before);
        return true;
    }

private:
    bool exposureOnly() const { return m_before.tonemapper == m_after.tonemapper; }

    void updateText()
    {
        if (!exposureOnly() && m_before.exposure != m_after.exposure)
            setText(QStringLiteral("Change Display Settings"));
        else if (!exposureOnly())
            setText(QStringLiteral("Change Tonemapper"));
        else
            setText(QStringLiteral("Change Exposure"));
    }

    DisplayHistory *m_history;
    State m_before, m_after;
    QElapsedTimer m_lastEdit;
    bool m_firstRedo = true;
};
} // namespace

DisplayHistory::DisplayHistory(DisplaySettings *settings, QUndoStack *undo, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_undo(undo)
    , m_last(current())
{
    connect(m_settings, &DisplaySettings::changed, this, &DisplayHistory::onChanged);
}

DisplayHistory::State DisplayHistory::current() const
{
    return { m_settings->exposure(), m_settings->tonemapper() };
}

void DisplayHistory::apply(const State &state)
{
    m_applying = true;
    m_settings->setExposure(state.exposure);
    m_settings->setTonemapper(state.tonemapper);
    m_applying = false;
    m_last = current();
}

void DisplayHistory::onChanged()
{
    const State now = current();
    if (m_applying || now == m_last)
        return;
    const State before = m_last;
    m_last = now; // before push: pushing calls redo(), which must see the new state as current
    m_undo->push(new SetDisplayCommand(this, before, now));
}
