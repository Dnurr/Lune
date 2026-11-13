// Display settings: exposure/tonemapper values passed to the display pass.
#include <QSignalSpy>
#include <QTest>

#include "frontend/core/displaysettings.h"

class TestDisplaySettings : public QObject
{
    Q_OBJECT

private slots:
    void defaults()
    {
        DisplaySettings s;
        QCOMPARE(s.exposure(), 0.0);
        QCOMPARE(s.tonemapper(), int(DisplaySettings::None));
    }

    void exposureIsClampedAndNotifiesOnlyOnChange()
    {
        DisplaySettings s;
        QSignalSpy changed(&s, &DisplaySettings::changed);
        s.setExposure(1.5);
        QCOMPARE(s.exposure(), 1.5);
        s.setExposure(1.5);
        QCOMPARE(changed.count(), 1);
        s.setExposure(100.0);
        QCOMPARE(s.exposure(), DisplaySettings::MaxExposure);
        s.setExposure(-100.0);
        QCOMPARE(s.exposure(), DisplaySettings::MinExposure);
    }

    void tonemapperRejectsUnknownIndices()
    {
        DisplaySettings s;
        QSignalSpy changed(&s, &DisplaySettings::changed);
        s.setTonemapper(DisplaySettings::AgX);
        QCOMPARE(s.tonemapper(), int(DisplaySettings::AgX));
        s.setTonemapper(99);
        s.setTonemapper(-1);
        QCOMPARE(s.tonemapper(), int(DisplaySettings::AgX));
        QCOMPARE(changed.count(), 1);
    }

    void namesAndIdsMatchTheEnum()
    {
        const QStringList names = DisplaySettings::tonemapperNames();
        const QStringList ids = DisplaySettings::tonemapperIds();
        QCOMPARE(names.size(), ids.size());
        QCOMPARE(ids.at(DisplaySettings::None), QStringLiteral("none"));
        QCOMPARE(ids.at(DisplaySettings::Reinhard), QStringLiteral("reinhard"));
        QCOMPARE(ids.at(DisplaySettings::Aces), QStringLiteral("aces"));
        QCOMPARE(ids.at(DisplaySettings::AgX), QStringLiteral("agx"));
    }

    void reset()
    {
        DisplaySettings s;
        s.setExposure(2.0);
        s.setTonemapper(DisplaySettings::Aces);
        QSignalSpy changed(&s, &DisplaySettings::changed);
        s.reset();
        QCOMPARE(s.exposure(), 0.0);
        QCOMPARE(s.tonemapper(), int(DisplaySettings::None));
        QCOMPARE(changed.count(), 1);
        s.reset(); // already default: no signal
        QCOMPARE(changed.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TestDisplaySettings)
#include "tst_displaysettings.moc"
