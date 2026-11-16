#include <QJsonArray>
#include <QTest>

#include "engine/render/imagerender.h"
#include "frontend/core/rendersettings.h"

class TestRenderSettings : public QObject
{
    Q_OBJECT

private slots:
    void passes()
    {
        // The final image comes first (Render Image); ids are unique and known.
        QCOMPARE(imagePasses().front().id, QStringLiteral("rendered"));
        QCOMPARE(imagePasses().front().view.shading, Shading::Rendered);
        QStringList ids;
        for (const ImagePass &p : imagePasses()) {
            QVERIFY(!ids.contains(p.id));
            ids.append(p.id);
            QCOMPARE(imagePass(p.id), &p);
        }
        QVERIFY(!imagePass(QStringLiteral("nope")));
        for (const QString &id : RenderSettings::defaultPasses())
            QVERIFY(imagePass(id));
    }

    void jsonRoundTrip()
    {
        RenderSettings s;
        s.viewportSize = false;
        s.width = 2560;
        s.height = 1440;
        s.folder = QStringLiteral("out/frames");
        s.skybox = false;
        s.passes = { QStringLiteral("clay"), QStringLiteral("normals") };
        const RenderSettings r = RenderSettings::fromJson(s.toJson());
        QCOMPARE(r.viewportSize, false);
        QCOMPARE(r.width, 2560);
        QCOMPARE(r.height, 1440);
        QCOMPARE(r.folder, s.folder);
        QCOMPARE(r.skybox, false);
        QCOMPARE(r.passes, s.passes);
    }

    void invalidValues()
    {
        // Missing: defaults.
        const RenderSettings empty = RenderSettings::fromJson({});
        QCOMPARE(empty.viewportSize, true);
        QCOMPARE(empty.passes, RenderSettings::defaultPasses());
        // Sizes clamped, unknown and repeated passes dropped (an empty list stays empty).
        QJsonObject json;
        json["width"] = 1;
        json["height"] = 100000;
        json["passes"] = QJsonArray { "clay", "removed_pass", "clay", "depth" };
        const RenderSettings r = RenderSettings::fromJson(json);
        QCOMPARE(r.width, RenderSettings::MinSize);
        QCOMPARE(r.height, RenderSettings::MaxSize);
        QCOMPARE(r.passes, QStringList({ "clay", "depth" }));
        json["passes"] = QJsonArray {};
        QVERIFY(RenderSettings::fromJson(json).passes.isEmpty());
    }

    void files()
    {
        QCOMPARE(RenderSettings::fileName("shot", 1), QStringLiteral("shot_frame0001.png"));
        QCOMPARE(RenderSettings::fileName("shot", 12, "clay"), QStringLiteral("shot_frame0012_clay.png"));

        RenderSettings s;
        QCOMPARE(s.outputFolder("C:/scenes/base.lune"), QStringLiteral("C:/scenes/renders"));
        s.folder = QStringLiteral("frames");
        QCOMPARE(s.outputFolder("C:/scenes/base.lune"), QStringLiteral("C:/scenes/frames"));
        s.folder = QStringLiteral("D:/out");
        QCOMPARE(s.outputFolder("C:/scenes/base.lune"), QStringLiteral("D:/out"));
        s.folder.clear();
        QVERIFY(s.outputFolder(QString()).endsWith(QStringLiteral("Lune/renders"))); // unsaved: Pictures/Lune
    }

    void imageSize()
    {
        RenderSettings s;
        QCOMPARE(s.imageSize(QSize(1200, 800)), QSize(1200, 800));
        s.viewportSize = false;
        s.width = 640;
        s.height = 360;
        QCOMPARE(s.imageSize(QSize(1200, 800)), QSize(640, 360));
    }
};

QTEST_GUILESS_MAIN(TestRenderSettings)
#include "tst_rendersettings.moc"
