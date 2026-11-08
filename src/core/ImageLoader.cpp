#include "core/ImageLoader.h"

#include <QImageReader>

#include <algorithm>

namespace lumi {

namespace {

constexpr int kMaxTextureSize = 8192;

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
    return image.convertToFormat(QImage::Format_RGBA8888);
}

QImage loadThumbnail(const QString& path, int maxSide)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (size.isValid())
        reader.setScaledSize(size.scaled(maxSide, maxSide, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull() || std::max(image.width(), image.height()) <= maxSide)
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
