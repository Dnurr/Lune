#pragma once

#include <QAbstractButton>

// The keyframe diamond at the end of an animatable Inspector row:
//   hollow and faint   not animated (click: insert a key at the current frame)
//   hollow, green      animated, no key on this frame (click: insert one)
//   filled, amber      a key on this frame (click: delete it)
// Right click asks for the row's animation menu (contextMenuRequested).
class KeyButton : public QAbstractButton
{
    Q_OBJECT

public:
    enum class State
    {
        None,
        Animated,
        Key,
    };

    explicit KeyButton(QWidget *parent = nullptr);

    void setState(State state);
    State state() const { return m_state; }

    QSize sizeHint() const override { return { 18, 20 }; }

    // The colors of the two animated states, shared with the Inspector's fields.
    static QColor keyColor();
    static QColor animatedColor();

signals:
    void contextMenuRequested(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    State m_state = State::None;
    bool m_hover = false;
};
