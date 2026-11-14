// Radiance HDR files: round trip, run-length encoded scanlines, bad files.
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include "engine/io/hdrimage.h"

namespace
{
void writeFile(const QString &path, const QByteArray &content)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}

QByteArray header(int width, int height)
{
    return "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\nEXPOSURE=1.0\n\n-Y " + QByteArray::number(height) + " +X "
        + QByteArray::number(width) + "\n";
}
} // namespace

class TestHdrImage : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip()
    {
        HdrImage image;
        image.width = 3;
        image.height = 2;
        image.pixels = { 1.0f, 0.5f, 0.25f, 0, 0, 0, 100.0f, 2.0f, 0.001f, //
                         0.75f, 0.75f, 0.75f, 4.0f, 0, 0, 0, 0, 1e-6f };
        QTemporaryDir dir;
        const QString path = dir.filePath("image.hdr");
        QString error;
        QVERIFY2(HdrIO::save(image, path, &error), qPrintable(error));

        const HdrImage loaded = HdrIO::load(path, &error);
        QVERIFY2(!loaded.isNull(), qPrintable(error));
        QCOMPARE(loaded.width, 3);
        QCOMPARE(loaded.height, 2);
        for (size_t i = 0; i < image.pixels.size(); ++i) {
            // RGBE keeps 8 bits of mantissa relative to the brightest channel of the pixel.
            const size_t first = i - i % 3;
            const float brightest = std::max({ image.pixels[first], image.pixels[first + 1], image.pixels[first + 2] });
            QVERIFY2(std::abs(loaded.pixels[i] - image.pixels[i]) <= brightest / 128.0f,
                     qPrintable(QStringLiteral("value %1: %2 != %3").arg(i).arg(loaded.pixels[i]).arg(image.pixels[i])));
        }
        QCOMPARE(loaded.pixel(0, 0)[0], 1.0f); // exact for powers of two
        QCOMPARE(loaded.pixel(1, 0)[0], 0.0f); // row 0 is the top row
        QCOMPARE(loaded.pixel(1, 1)[0], 4.0f);
        QCOMPARE(loaded.pixel(0, 1)[0], 0.75f);
    }

    void runLengthEncoded()
    {
        // One 8-pixel scanline: red is a run of 8 x 128, green 8 literals, blue a run,
        // exponent a run of 129 (scale 1/128): pixels (1, i/128, 0.5).
        QByteArray data = header(8, 1);
        data += QByteArray("\x02\x02\x00\x08", 4);
        data += QByteArray("\x88\x80", 2);                                // R: run of 8
        data += QByteArray("\x08\x00\x01\x02\x03\x04\x05\x06\x07", 9);   // G: 8 literals
        data += QByteArray("\x88\x40", 2);                                // B: run of 8
        data += QByteArray("\x88\x81", 2);                                // E: run of 8
        QTemporaryDir dir;
        const QString path = dir.filePath("rle.hdr");
        writeFile(path, data);

        QString error;
        const HdrImage image = HdrIO::load(path, &error);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QCOMPARE(image.width, 8);
        for (int x = 0; x < 8; ++x) {
            QCOMPARE(image.pixel(x, 0)[0], 1.0f);
            QCOMPARE(image.pixel(x, 0)[1], x / 128.0f);
            QCOMPARE(image.pixel(x, 0)[2], 0.5f);
        }
    }

    void badFiles_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("not hdr") << QByteArray("P6\n1 1\n255\nabc");
        QTest::newRow("no header end") << QByteArray("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n");
        QTest::newRow("xyz format") << QByteArray("#?RADIANCE\nFORMAT=32-bit_rle_xyze\n\n-Y 1 +X 1\nabcd");
        QTest::newRow("flipped orientation") << QByteArray("#?RADIANCE\n\n+Y 1 +X 1\nabcd");
        QTest::newRow("truncated") << header(4, 4) + QByteArray(10, '\x01');
        QTest::newRow("run too long") << header(8, 1) + QByteArray("\x02\x02\x00\x08\x90\x01", 6);
    }

    void badFiles()
    {
        QFETCH(QByteArray, content);
        QTemporaryDir dir;
        const QString path = dir.filePath("bad.hdr");
        writeFile(path, content);
        QString error;
        QVERIFY(HdrIO::load(path, &error).isNull());
        QVERIFY(!error.isEmpty());
    }

    void downscale()
    {
        HdrImage image;
        image.width = 5; // odd: the last column is dropped
        image.height = 2;
        for (int i = 0; i < 10; ++i)
            image.pixels.insert(image.pixels.end(), { float(i), 1.0f, 0.0f });
        const HdrImage half = downscaled(image, 4);
        QCOMPARE(half.width, 2);
        QCOMPARE(half.height, 1);
        QCOMPARE(half.pixel(0, 0)[0], (0 + 1 + 5 + 6) / 4.0f);
        QCOMPARE(half.pixel(1, 0)[0], (2 + 3 + 7 + 8) / 4.0f);
        QCOMPARE(half.pixel(1, 0)[1], 1.0f);
        QCOMPARE(downscaled(image, 8).width, 5); // already small enough
    }

    void missingFile()
    {
        QString error;
        QVERIFY(HdrIO::load(QStringLiteral("does/not/exist.hdr"), &error).isNull());
        QVERIFY(error.contains(QStringLiteral("exist.hdr")));
    }
};

QTEST_GUILESS_MAIN(TestHdrImage)
#include "tst_hdrimage.moc"
