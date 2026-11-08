#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

namespace lumi {

// Loads an image as RGBA8888, honouring EXIF orientation and clamping it to the
// largest texture size the renderer supports. Safe to call from any thread.
QImage loadImage(const QString& path);

// Small preview fitting a box of maxSide pixels, or null on failure. Any thread.
QImage loadThumbnail(const QString& path, int maxSide);

// Glob patterns ("*.png", ...) for every format Qt can read.
QStringList imageNameFilters();

} // namespace lumi
