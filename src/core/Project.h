#pragma once

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

#include <array>

namespace lumi {

enum class ContentType { TestPattern, Image };

struct Surface {
    QUuid id = QUuid::createUuid();
    QString name;
    // Corners in normalized output space (0..1, y down), order: TL, TR, BR, BL.
    std::array<QPointF, 4> corners{QPointF(0.25, 0.25), QPointF(0.75, 0.25),
                                   QPointF(0.75, 0.75), QPointF(0.25, 0.75)};
    ContentType content = ContentType::TestPattern;
    QString imagePath;
    QImage image; // RGBA8888, loaded from imagePath; not serialized
    float opacity = 1.0f;
    bool visible = true;
};

// Stable per-index colour shared by the editor and the output test pattern.
QColor surfaceColor(int index);

class Project : public QObject {
    Q_OBJECT

public:
    explicit Project(QObject* parent = nullptr);

    const QVector<Surface>& surfaces() const { return m_surfaces; }
    int count() const { return int(m_surfaces.size()); }
    bool isValidIndex(int index) const { return index >= 0 && index < count(); }
    const Surface& surface(int index) const { return m_surfaces.at(index); }

    int addSurface();
    void removeSurface(int index);
    void setCorner(int index, int corner, QPointF pos);
    void translate(int index, QPointF delta);
    void setName(int index, const QString& name);
    void setOpacity(int index, float opacity);
    void setVisible(int index, bool visible);
    // Switching to Image only succeeds when an image path is already set.
    bool setContent(int index, ContentType content);
    bool setImage(int index, const QString& path);

    void clear();
    bool save(const QString& path, QString* error) const;
    bool load(const QString& path, QString* error, QStringList* missingImages);

signals:
    void structureChanged();      // surfaces added, removed or replaced
    void surfaceChanged(int index); // geometry or properties of one surface

private:
    QVector<Surface> m_surfaces;
    int m_nextNumber = 1;
};

} // namespace lumi
