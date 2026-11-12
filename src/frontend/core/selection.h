#pragma once

#include <QObject>

#include "engine/scene/entity.h"

// What the user has selected in the editor (editor state, not part of the scene).
// Single selection for now; InvalidEntity = nothing selected.
class Selection : public QObject
{
    Q_OBJECT

public:
    explicit Selection(QObject *parent = nullptr);

    EntityId current() const { return m_current; }
    bool isEmpty() const { return m_current == InvalidEntity; }

    void select(EntityId id);
    void clear() { select(InvalidEntity); }

signals:
    void changed(EntityId current);

private:
    EntityId m_current = InvalidEntity;
};
