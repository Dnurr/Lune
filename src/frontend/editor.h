#pragma once

#include <QMainWindow>

class Viewport;

class Editor : public QMainWindow
{
    Q_OBJECT

public:
    explicit Editor(QWidget *parent = nullptr);
    ~Editor() override;

    Viewport *viewport() const { return m_viewport; }

private:
    Viewport *m_viewport = nullptr; // owned by Qt (central widget)
};
