// Display settings undo: changes recorded after the fact, undo/redo not recorded again,
// exposure scrubs merged, the document marked as modified.
#include <QTest>
#include <QUndoStack>

#include "frontend/core/displayhistory.h"
#include "frontend/core/displaysettings.h"

class TestDisplayHistory : public QObject
{
    Q_OBJECT

private slots:
    void changesAreUndoable()
    {
        DisplaySettings s;
        QUndoStack undo;
        DisplayHistory history(&s, &undo);

        s.setTonemapper(DisplaySettings::AgX);
        QCOMPARE(undo.count(), 1);
        QVERIFY(!undo.isClean());

        undo.undo();
        QCOMPARE(s.tonemapper(), int(DisplaySettings::None));
        QCOMPARE(undo.count(), 1); // undo itself isn't recorded as a new change
        QVERIFY(undo.isClean());

        undo.redo();
        QCOMPARE(s.tonemapper(), int(DisplaySettings::AgX));
        QCOMPARE(undo.count(), 1);
    }

    void exposureScrubMergesIntoOneStep()
    {
        DisplaySettings s;
        QUndoStack undo;
        DisplayHistory history(&s, &undo);

        for (int i = 1; i <= 10; ++i)
            s.setExposure(i * 0.1);
        QCOMPARE(undo.count(), 1);
        undo.undo();
        QCOMPARE(s.exposure(), 0.0);
        undo.redo();
        QCOMPARE(s.exposure(), 1.0);
    }

    void tonemapperChangesDontMerge()
    {
        DisplaySettings s;
        QUndoStack undo;
        DisplayHistory history(&s, &undo);

        s.setExposure(1.0);
        s.setTonemapper(DisplaySettings::Reinhard);
        s.setTonemapper(DisplaySettings::Aces);
        QCOMPARE(undo.count(), 3);
        undo.undo();
        QCOMPARE(s.tonemapper(), int(DisplaySettings::Reinhard));
        QCOMPARE(s.exposure(), 1.0);
    }

    void scrubBackToTheStartLeavesNoStep()
    {
        DisplaySettings s;
        QUndoStack undo;
        DisplayHistory history(&s, &undo);

        s.setExposure(0.5);
        s.setExposure(0.0);
        QCOMPARE(undo.count(), 0);
        QVERIFY(undo.isClean());
    }

    void separateScrubsAreSeparateSteps()
    {
        DisplaySettings s;
        QUndoStack undo;
        DisplayHistory history(&s, &undo);

        s.setExposure(1.0);
        QTest::qWait(700); // longer than the merge window
        s.setExposure(2.0);
        QCOMPARE(undo.count(), 2);
        undo.undo();
        QCOMPARE(s.exposure(), 1.0);
    }

    void changesAfterAClearedHistoryStartFromTheCurrentState()
    {
        DisplaySettings s;
        QUndoStack undo;
        DisplayHistory history(&s, &undo);

        s.setExposure(3.0); // e.g. loading a scene, then the editor clears the history
        undo.clear();
        s.setExposure(4.0);
        undo.undo();
        QCOMPARE(s.exposure(), 3.0);
    }
};

QTEST_GUILESS_MAIN(TestDisplayHistory)
#include "tst_displayhistory.moc"
