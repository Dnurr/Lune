#include "displaysettings.h"

#include <QtGlobal>

DisplaySettings::DisplaySettings(QObject *parent)
    : QObject(parent)
{
}

void DisplaySettings::setExposure(double ev)
{
    ev = qBound(MinExposure, ev, MaxExposure);
    if (qFuzzyCompare(ev + 100.0, m_exposure + 100.0)) // +100: fuzzy compare doesn't like 0
        return;
    m_exposure = ev;
    emit changed();
}

void DisplaySettings::setTonemapper(int tonemapper)
{
    if (tonemapper < 0 || tonemapper >= tonemapperNames().size() || tonemapper == m_tonemapper)
        return;
    m_tonemapper = tonemapper;
    emit changed();
}

QStringList DisplaySettings::tonemapperNames()
{
    return { QStringLiteral("None"), QStringLiteral("Reinhard"), QStringLiteral("ACES"), QStringLiteral("AgX") };
}

QStringList DisplaySettings::tonemapperIds()
{
    return { QStringLiteral("none"), QStringLiteral("reinhard"), QStringLiteral("aces"), QStringLiteral("agx") };
}

void DisplaySettings::reset()
{
    const bool wasDefault = m_tonemapper == None && m_exposure == 0.0;
    m_exposure = 0.0;
    m_tonemapper = None;
    if (!wasDefault)
        emit changed();
}
