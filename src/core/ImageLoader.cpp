#include "core/ImageLoader.h"

#include <QImageReader>

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

QStringList imageNameFilters()
{
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(format);
    return patterns;
}

} // namespace lumi
