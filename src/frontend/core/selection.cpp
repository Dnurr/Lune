#include "selection.h"

Selection::Selection(QObject *parent)
    : QObject(parent)
{
}

void Selection::select(EntityId id)
{
    if (m_current == id)
        return;
    m_current = id;
    emit changed(m_current);
}
