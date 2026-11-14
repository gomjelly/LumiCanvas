#include "core/ImageLoader.h"

#include <QImageReader>

#include <algorithm>

namespace lumi {

namespace {

constexpr int kMaxTextureSize = 8192;
constexpr double kMinTransparentShare = 0.01; // less than this is just soft edges
constexpr double kDarkArtLuma = 0.35;

// The output background is black, so dark art on a transparent background would
// project as nothing at all. Such an image goes on white, as a viewer would show it.
// Light or colourful art stays transparent: the empty parts then cast no light.
QImage placeDarkArtOnPaper(const QImage& image)
{
    if (!image.hasAlphaChannel())
        return image;
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);

    qint64 transparent = 0;
    double weightedLuma = 0.0;
    double totalAlpha = 0.0;
    for (int y = 0; y < rgba.height(); ++y) {
        const uchar* p = rgba.constScanLine(y);
        for (int x = 0; x < rgba.width(); ++x, p += 4) {
            const double alpha = p[3] / 255.0;
            if (p[3] < 128)
                ++transparent;
            weightedLuma += alpha * (0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2]) / 255.0;
            totalAlpha += alpha;
        }
    }
    const double pixels = double(rgba.width()) * rgba.height();
    const bool hasBackgroundHole = transparent >= pixels * kMinTransparentShare;
    if (!hasBackgroundHole || (totalAlpha > 0.0 && weightedLuma / totalAlpha >= kDarkArtLuma))
        return image;

    QImage result = rgba;
    for (int y = 0; y < result.height(); ++y) {
        uchar* p = result.scanLine(y);
        for (int x = 0; x < result.width(); ++x, p += 4) {
            const int alpha = p[3];
            for (int c = 0; c < 3; ++c)
                p[c] = uchar((p[c] * alpha + 255 * (255 - alpha)) / 255);
            p[3] = 255;
        }
    }
    return result;
}

} // namespace

QImage loadImage(const QString& path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
        return {};
    if (image.width() > kMaxTextureSize || image.height() > kMaxTextureSize)
        image = image.scaled(kMaxTextureSize, kMaxTextureSize, Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    return placeDarkArtOnPaper(image).convertToFormat(QImage::Format_RGBA8888);
}

QImage loadThumbnail(const QString& path, int maxSide)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid())
        reader.setScaledSize(size.scaled(maxSide, maxSide, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull())
        return image;
    image = placeDarkArtOnPaper(image); // so the list shows what the output will show
    if (std::max(image.width(), image.height()) <= maxSide)
        return image;
    return image.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QStringList imageNameFilters()
{
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(format);
    return patterns;
}

} // namespace lumi
