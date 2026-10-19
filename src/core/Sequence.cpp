#include "core/Sequence.h"

#include "core/ImageLoader.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace lumi {

namespace {

// Steps kept decoded around the current one.
constexpr int kCacheBehind = 1;
constexpr int kCacheAhead = 2;

} // namespace

Sequence::Sequence(QObject* parent)
    : QObject(parent)
{
}

bool Sequence::load(const QString& folder)
{
    QDir dir(folder);
    QStringList files = dir.entryList(imageNameFilters(), QDir::Files | QDir::Readable);
    if (files.isEmpty()) {
        clear();
        return false;
    }

    QCollator collator;
    collator.setNumericMode(true); // step2 before step10
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(), files.end(),
              [&](const QString& a, const QString& b) { return collator.compare(a, b) < 0; });

    ++m_generation;
    m_folder = QDir::cleanPath(dir.absolutePath());
    m_files = files;
    m_cache.clear();
    m_pending.clear();
    m_failed.clear();
    m_display = QImage();
    m_current = -1;
    emit loaded();
    emit displayImageChanged();
    setCurrentIndex(0);
    return true;
}

void Sequence::clear()
{
    ++m_generation;
    m_folder.clear();
    m_files.clear();
    m_cache.clear();
    m_pending.clear();
    m_failed.clear();
    m_display = QImage();
    m_current = -1;
    emit loaded();
    emit currentChanged(-1);
    emit displayImageChanged();
}

QString Sequence::fileName(int index) const
{
    return m_files.value(index);
}

QString Sequence::filePath(int index) const
{
    if (index < 0 || index >= count())
        return {};
    return m_folder + QLatin1Char('/') + m_files.at(index);
}

void Sequence::setCurrentIndex(int index)
{
    if (isEmpty())
        return;
    index = std::clamp(index, 0, count() - 1);
    if (index == m_current)
        return;
    m_current = index;
    emit currentChanged(index);

    if (const auto it = m_cache.constFind(index); it != m_cache.constEnd()) {
        m_display = it.value();
        emit displayImageChanged();
    } else if (m_failed.contains(index)) {
        m_display = QImage();
        emit displayImageChanged();
    }
    updateCache();
}

void Sequence::updateCache()
{
    const int lo = m_current - kCacheBehind;
    const int hi = m_current + kCacheAhead;
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (it.key() < lo || it.key() > hi)
            it = m_cache.erase(it);
        else
            ++it;
    }
    // Current step first, then the ones the user is most likely to go to next.
    for (const int index : {m_current, m_current + 1, m_current - 1, m_current + 2}) {
        if (index >= 0 && index < count() && !m_cache.contains(index)
            && !m_pending.contains(index) && !m_failed.contains(index))
            startLoad(index);
    }
}

void Sequence::startLoad(int index)
{
    m_pending.insert(index);
    const quint64 generation = m_generation;
    auto* watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, generation, index] {
        onImageLoaded(generation, index, watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&loadImage, filePath(index)));
}

void Sequence::onImageLoaded(quint64 generation, int index, const QImage& image)
{
    if (generation != m_generation)
        return; // the folder was reloaded meanwhile
    m_pending.remove(index);

    if (image.isNull()) {
        m_failed.insert(index);
        if (index == m_current) {
            m_display = QImage();
            emit displayImageChanged();
        }
        emit loadFailed(index);
        return;
    }
    if (index < m_current - kCacheBehind || index > m_current + kCacheAhead)
        return; // the user has moved on

    m_cache.insert(index, image);
    if (index == m_current) {
        m_display = image;
        emit displayImageChanged();
    }
}

} // namespace lumi
