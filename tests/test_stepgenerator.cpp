#include "core/ImageLoader.h"
#include "core/StepGenerator.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageWriter>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

using lumi::GeneratedStep;
using lumi::StepOptions;

namespace {

QList<GeneratedStep> run(const QImage& source, const StepOptions& options)
{
    QList<GeneratedStep> steps;
    lumi::generateSteps(source, options, [&](const GeneratedStep& step, int, int) {
        steps.append(step);
        return true;
    });
    return steps;
}

// Black, grey and white vertical stripes: three obvious regions.
QImage stripes()
{
    QImage image(300, 120, QImage::Format_RGB32);
    QPainter p(&image);
    p.fillRect(0, 0, 100, 120, QColor(0, 0, 0));
    p.fillRect(100, 0, 100, 120, QColor(128, 128, 128));
    p.fillRect(200, 0, 100, 120, QColor(255, 255, 255));
    return image;
}

StepOptions regionsOnly(int levels)
{
    StepOptions options;
    options.levels = levels;
    options.outline = false;
    options.finalOriginal = false;
    return options;
}

// A cross drawn on a fully transparent background.
QImage lineArt(const QColor& color)
{
    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.fillRect(46, 0, 8, 100, color);
    p.fillRect(0, 46, 100, 8, color);
    return image;
}

int lit(const QImage& image, int x, int y)
{
    return qGray(image.pixel(x, y));
}

} // namespace

class TestStepGenerator : public QObject {
    Q_OBJECT

private slots:
    void darkFirstFillsFromDarkest()
    {
        const QList<GeneratedStep> steps = run(stripes(), regionsOnly(3));
        QCOMPARE(steps.size(), 3);
        QVERIFY(lit(steps[0].image, 50, 60) > 0);
        QCOMPARE(lit(steps[0].image, 150, 60), 0);
        QCOMPARE(lit(steps[0].image, 250, 60), 0);
        QVERIFY(lit(steps[1].image, 150, 60) > 0);
        QCOMPARE(lit(steps[1].image, 250, 60), 0);
        QVERIFY(lit(steps[2].image, 250, 60) > 0);
    }

    void lightFirstFillsFromLightest()
    {
        StepOptions options = regionsOnly(3);
        options.darkFirst = false;
        const QList<GeneratedStep> steps = run(stripes(), options);
        QCOMPARE(steps.size(), 3);
        QVERIFY(lit(steps[0].image, 250, 60) > 0);
        QCOMPARE(lit(steps[0].image, 50, 60), 0);
    }

    // A black region must still light up, or its step would look empty on the canvas.
    void darkRegionsStayVisible()
    {
        const QList<GeneratedStep> steps = run(stripes(), regionsOnly(3));
        QVERIFY(lit(steps[0].image, 50, 60) >= 50);
    }

    void stepsAreCumulative()
    {
        const QList<GeneratedStep> steps = run(stripes(), regionsOnly(3));
        for (int s = 1; s < steps.size(); ++s) {
            for (int x = 0; x < 300; x += 7) {
                if (lit(steps[s - 1].image, x, 60) > 0)
                    QVERIFY(lit(steps[s].image, x, 60) > 0);
            }
        }
    }

    void outlineAndOriginalWrapTheRegions()
    {
        StepOptions options;
        options.levels = 3;
        const QList<GeneratedStep> steps = run(stripes(), options);
        QCOMPARE(steps.size(), 5);
        QCOMPARE(steps.first().name, QStringLiteral("outline"));
        QCOMPARE(steps.last().name, QStringLiteral("original"));
        QCOMPARE(steps.last().image.pixelColor(50, 60), QColor(0, 0, 0));
        QCOMPARE(steps.last().image.pixelColor(250, 60), QColor(255, 255, 255));
    }

    void outlineFindsEdgesOnly()
    {
        StepOptions options = regionsOnly(3);
        options.outline = true;
        const QImage outline = run(stripes(), options).first().image;
        int nearEdge = 0;
        for (int x = 90; x < 210; ++x)
            nearEdge = std::max(nearEdge, lit(outline, x, 60));
        QVERIFY(nearEdge > 200);
        QCOMPARE(lit(outline, 50, 60), 0); // middle of a flat stripe
    }

    // Line sizes grow with the image; a clear edge must stay unbroken at full size too.
    void outlineStaysUnbrokenOnLargeImages()
    {
        QImage image(4000, 1000, QImage::Format_RGB32);
        QPainter p(&image);
        p.fillRect(image.rect(), QColor(207, 195, 180));
        p.fillRect(0, 500, 4000, 500, QColor(70, 140, 70));
        p.end();
        StepOptions options = regionsOnly(2);
        options.outline = true;
        const QImage outline = run(image, options).first().image;
        for (int x = 100; x < 3900; x += 100) {
            int strongest = 0;
            for (int y = 470; y < 530; ++y)
                strongest = std::max(strongest, lit(outline, x, y));
            QVERIFY2(strongest > 200, qPrintable(QStringLiteral("gap at x=%1").arg(x)));
        }
    }

    void outlineIgnoresSmoothGradients()
    {
        QImage image(1000, 1000, QImage::Format_RGB32);
        QPainter p(&image);
        QLinearGradient sky(0, 0, 0, 1000);
        sky.setColorAt(0, QColor(70, 130, 210));
        sky.setColorAt(1, QColor(250, 215, 170));
        p.fillRect(image.rect(), sky);
        p.end();
        StepOptions options = regionsOnly(2);
        options.outline = true;
        const QImage outline = run(image, options).first().image;
        for (int y = 50; y < 950; y += 50)
            QCOMPARE(lit(outline, 500, y), 0);
    }

    void outlineLeavesImageBordersClean_data()
    {
        QTest::addColumn<int>("size");
        QTest::addColumn<bool>("gradient");
        QTest::newRow("flat small") << 400 << false;
        QTest::newRow("gradient small") << 400 << true;
        QTest::newRow("gradient large") << 4000 << true;
    }

    void outlineLeavesImageBordersClean()
    {
        QFETCH(int, size);
        QFETCH(bool, gradient);
        QImage image(size, size * 3 / 4, QImage::Format_RGB32);
        QPainter p(&image);
        QLinearGradient sky(0, 0, 0, image.height());
        sky.setColorAt(0, QColor(70, 130, 210));
        sky.setColorAt(1, gradient ? QColor(250, 215, 170) : QColor(70, 130, 210));
        p.fillRect(image.rect(), sky);
        p.end();
        StepOptions options = regionsOnly(2);
        options.outline = true;
        const QImage outline = run(image, options).first().image;
        int strongest = 0;
        for (int d = 0; d < 40; ++d) {
            for (int t = 0; t < image.height(); t += 17) {
                strongest = std::max(strongest, lit(outline, d, t));
                strongest = std::max(strongest, lit(outline, image.width() - 1 - d, t));
            }
            for (int t = 0; t < image.width(); t += 17) {
                strongest = std::max(strongest, lit(outline, t, d));
                strongest = std::max(strongest, lit(outline, t, image.height() - 1 - d));
            }
        }
        QCOMPARE(strongest, 0);
    }

    void outlineFindsEdgesBetweenEqualBrightnessColors()
    {
        QImage image(200, 100, QImage::Format_RGB32);
        QPainter p(&image);
        p.fillRect(0, 0, 100, 100, QColor(200, 90, 90));
        p.fillRect(100, 0, 100, 100, QColor(60, 130, 130));
        p.end();
        StepOptions options = regionsOnly(2);
        options.outline = true;
        const QImage outline = run(image, options).first().image;
        int strongest = 0;
        for (int x = 90; x < 110; ++x)
            strongest = std::max(strongest, lit(outline, x, 50));
        QVERIFY(strongest > 200);
    }

    void colorsOfEqualBrightnessStaySeparate()
    {
        QImage image(200, 100, QImage::Format_RGB32);
        QPainter p(&image);
        p.fillRect(0, 0, 100, 100, QColor(200, 60, 60));
        p.fillRect(100, 0, 100, 100, QColor(40, 130, 130));
        p.end();
        QCOMPARE(run(image, regionsOnly(2)).size(), 2);

        StepOptions tones = regionsOnly(2);
        tones.method = StepOptions::Method::Tones;
        const QImage first = run(image, tones).first().image;
        const QColor c = first.pixelColor(50, 50);
        QCOMPARE(c.red(), c.green()); // tones are shown in grey
    }

    void flatImageGivesOneStep()
    {
        QImage image(64, 64, QImage::Format_RGB32);
        image.fill(QColor(90, 120, 200));
        QCOMPARE(run(image, regionsOnly(6)).size(), 1);
    }

    void transparencyCountsAsPaper()
    {
        QImage image(64, 64, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        StepOptions options;
        options.outline = false;
        options.levels = 1;
        const QImage original = run(image, options).last().image;
        QCOMPARE(original.pixelColor(32, 32), QColor(255, 255, 255));
    }

    void isDeterministic()
    {
        QImage noise(120, 80, QImage::Format_RGB32);
        for (int y = 0; y < 80; ++y) {
            for (int x = 0; x < 120; ++x)
                noise.setPixel(x, y, qRgb(x * 2, y * 3, (x * y) % 255));
        }
        const QList<GeneratedStep> a = run(noise, StepOptions());
        const QList<GeneratedStep> b = run(noise, StepOptions());
        QCOMPARE(a.size(), b.size());
        for (int i = 0; i < a.size(); ++i)
            QVERIFY(a[i].image == b[i].image);
    }

    void largeImagesAreScaledToMaxSide()
    {
        QImage image(800, 400, QImage::Format_RGB32);
        image.fill(Qt::gray);
        StepOptions options = regionsOnly(2);
        options.maxSide = 200;
        QCOMPARE(run(image, options).first().image.size(), QSize(200, 100));
    }

    void sinkCanStopEarly()
    {
        int calls = 0;
        lumi::generateSteps(stripes(), StepOptions(), [&](const GeneratedStep&, int, int) {
            ++calls;
            return false;
        });
        QCOMPARE(calls, 1);
    }

    // Black lines on transparency must not disappear on the black output.
    void loaderPutsDarkArtOnPaper()
    {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("art.png"));
        QVERIFY(lineArt(QColor(Qt::black)).save(path));
        const QImage loaded = lumi::loadImage(path);
        QCOMPARE(loaded.pixelColor(10, 10), QColor(255, 255, 255));
        QCOMPARE(loaded.pixelColor(50, 50), QColor(0, 0, 0));
    }

    void loaderKeepsLightArtTransparent()
    {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("art.png"));
        QVERIFY(lineArt(QColor(Qt::white)).save(path));
        const QImage loaded = lumi::loadImage(path);
        QCOMPARE(loaded.pixelColor(10, 10).alpha(), 0);
        QCOMPARE(loaded.pixelColor(50, 50), QColor(255, 255, 255));
    }

    void loaderLeavesOpaqueImagesAlone()
    {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("dark.png"));
        QImage dark(100, 100, QImage::Format_ARGB32);
        dark.fill(QColor(10, 10, 10));
        QVERIFY(dark.save(path));
        QCOMPARE(lumi::loadImage(path).pixelColor(50, 50), QColor(10, 10, 10));
    }

    void thumbnailMatchesWhatTheOutputShows()
    {
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("art.png"));
        QVERIFY(lineArt(QColor(Qt::black)).save(path));
        QCOMPARE(lumi::loadThumbnail(path, 50).pixelColor(5, 5), QColor(255, 255, 255));
    }

    void loaderReadsEveryCommonFormat_data()
    {
        QTest::addColumn<QByteArray>("format");
        for (const char* format : {"png", "jpg", "bmp", "tiff", "webp", "gif"})
            QTest::newRow(format) << QByteArray(format);
    }

    void loaderReadsEveryCommonFormat()
    {
        QFETCH(QByteArray, format);
        if (!QImageWriter::supportedImageFormats().contains(format))
            QSKIP("Qt has no writer for this format here");
        QTemporaryDir temp;
        const QString path = temp.filePath(QStringLiteral("a.") + QString::fromLatin1(format));
        QImage image(64, 64, QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(path, format.constData()));
        const QImage loaded = lumi::loadImage(path);
        QVERIFY(!loaded.isNull());
        QCOMPARE(loaded.format(), QImage::Format_RGBA8888);
        QVERIFY(qGray(loaded.pixel(32, 32)) > 200);
    }

    void nullImageProducesNothing()
    {
        QVERIFY(run(QImage(), StepOptions()).isEmpty());
    }

    void savesNumberedFilesInOrder()
    {
        QTemporaryDir temp;
        const QString folder = temp.filePath(QStringLiteral("nested/steps"));
        StepOptions options;
        options.levels = 3;
        int lastDone = 0;
        int lastTotal = 0;
        const QString error = lumi::saveSteps(stripes(), options, folder, [&](int done, int total) {
            lastDone = done;
            lastTotal = total;
            return true;
        });
        QVERIFY(error.isEmpty());
        QCOMPARE(lastDone, 5);
        QCOMPARE(lastTotal, 5);
        const QStringList files = QDir(folder).entryList(QDir::Files, QDir::Name);
        QCOMPARE(files, QStringList({QStringLiteral("step01_outline.png"), QStringLiteral("step02_color1.png"),
                                     QStringLiteral("step03_color2.png"), QStringLiteral("step04_color3.png"),
                                     QStringLiteral("step05_original.png")}));
        QCOMPARE(QImage(QDir(folder).filePath(files.last())).size(), stripes().size());
    }

    void cancelRemovesWhatItCreated()
    {
        QTemporaryDir temp;
        const QString folder = temp.filePath(QStringLiteral("steps"));
        const QString error = lumi::saveSteps(stripes(), StepOptions(), folder,
                                              [](int done, int) { return done < 2; });
        QVERIFY(error.isEmpty());
        QVERIFY(!QFileInfo::exists(folder));
    }

    void cancelKeepsExistingFolderContents()
    {
        QTemporaryDir temp;
        QFile keep(temp.filePath(QStringLiteral("keep.txt")));
        QVERIFY(keep.open(QIODevice::WriteOnly));
        keep.close();
        lumi::saveSteps(stripes(), StepOptions(), temp.path(), [](int, int) { return false; });
        QCOMPARE(QDir(temp.path()).entryList(QDir::Files), QStringList(QStringLiteral("keep.txt")));
    }

    void reportsWriteFailure()
    {
        QTemporaryDir temp;
        QFile blocker(temp.filePath(QStringLiteral("file")));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();
        // A folder can't be created beneath a regular file.
        const QString error = lumi::saveSteps(stripes(), StepOptions(), blocker.fileName() + QStringLiteral("/steps"), {});
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestStepGenerator)
#include "test_stepgenerator.moc"
