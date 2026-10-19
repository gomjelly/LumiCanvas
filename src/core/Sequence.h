#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

namespace lumi {

// An ordered folder of step images (step1.png, step2.png, ... in natural order).
// Images around the current step are decoded in the background so stepping
// forward is instant; until a step finishes loading the previous one stays up.
class Sequence : public QObject {
    Q_OBJECT

public:
    explicit Sequence(QObject* parent = nullptr);

    // Returns false (and leaves the sequence empty) when the folder has no images.
    bool load(const QString& folder);
    void clear();

    QString folder() const { return m_folder; }
    int count() const { return int(m_files.size()); }
    bool isEmpty() const { return m_files.isEmpty(); }
    QString fileName(int index) const;
    QString filePath(int index) const;
    bool hasFailed(int index) const { return m_failed.contains(index); }

    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    void next() { setCurrentIndex(m_current + 1); }
    void previous() { setCurrentIndex(m_current - 1); }
    void first() { setCurrentIndex(0); }
    void last() { setCurrentIndex(count() - 1); }

    // What should be projected right now; null while nothing has loaded yet.
    QImage displayImage() const { return m_display; }
    // The step before the current one, or null while it is still loading or
    // when the current step is the first.
    QImage previousImage() const { return m_cache.value(m_current - 1); }

signals:
    void loaded();               // the file list changed
    void currentChanged(int index);
    void displayImageChanged(); // the current or previous step image changed
    void loadFailed(int index);

private:
    void updateCache();
    void startLoad(int index);
    void onImageLoaded(quint64 generation, int index, const QImage& image);

    QString m_folder;
    QStringList m_files;
    int m_current = -1;
    QHash<int, QImage> m_cache;
    QSet<int> m_pending;
    QSet<int> m_failed;
    QImage m_display;
    quint64 m_generation = 0; // bumped on reload so stale loads are ignored
};

} // namespace lumi
