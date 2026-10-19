#include "core/Project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace lumi {

namespace {

constexpr int kMaxTextureSize = 8192;
constexpr int kFormatVersion = 1;

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

} // namespace

QColor surfaceColor(int index)
{
    const double hue = std::fmod(0.08 + index * 0.618033988749895, 1.0);
    return QColor::fromHsvF(float(hue), 0.65f, 0.95f);
}

Project::Project(QObject* parent)
    : QObject(parent)
{
}

int Project::addSurface()
{
    Surface s;
    s.name = tr("Surface %1").arg(m_nextNumber++);
    // Offset new surfaces slightly so they don't stack exactly on top of each other.
    const double offset = 0.03 * (m_surfaces.size() % 5);
    for (QPointF& c : s.corners)
        c += QPointF(offset, offset);
    m_surfaces.push_back(std::move(s));
    emit structureChanged();
    return count() - 1;
}

void Project::removeSurface(int index)
{
    if (!isValidIndex(index))
        return;
    m_surfaces.removeAt(index);
    emit structureChanged();
}

void Project::setCorner(int index, int corner, QPointF pos)
{
    if (!isValidIndex(index) || corner < 0 || corner > 3)
        return;
    m_surfaces[index].corners[corner] = pos;
    emit surfaceChanged(index);
}

void Project::translate(int index, QPointF delta)
{
    if (!isValidIndex(index))
        return;
    for (QPointF& c : m_surfaces[index].corners)
        c += delta;
    emit surfaceChanged(index);
}

void Project::setName(int index, const QString& name)
{
    if (!isValidIndex(index) || m_surfaces[index].name == name)
        return;
    m_surfaces[index].name = name;
    emit surfaceChanged(index);
}

void Project::setOpacity(int index, float opacity)
{
    if (!isValidIndex(index))
        return;
    m_surfaces[index].opacity = std::clamp(opacity, 0.0f, 1.0f);
    emit surfaceChanged(index);
}

void Project::setVisible(int index, bool visible)
{
    if (!isValidIndex(index) || m_surfaces[index].visible == visible)
        return;
    m_surfaces[index].visible = visible;
    emit surfaceChanged(index);
}

bool Project::setContent(int index, ContentType content)
{
    if (!isValidIndex(index))
        return false;
    Surface& s = m_surfaces[index];
    if (content == ContentType::Image && s.imagePath.isEmpty())
        return false;
    s.content = content;
    emit surfaceChanged(index);
    return true;
}

bool Project::setImage(int index, const QString& path)
{
    if (!isValidIndex(index))
        return false;
    QImage image = loadImage(path);
    if (image.isNull())
        return false;
    Surface& s = m_surfaces[index];
    s.imagePath = QFileInfo(path).absoluteFilePath();
    s.image = std::move(image);
    s.content = ContentType::Image;
    emit surfaceChanged(index);
    return true;
}

void Project::clear()
{
    m_surfaces.clear();
    m_nextNumber = 1;
    emit structureChanged();
}

bool Project::save(const QString& path, QString* error) const
{
    const QDir dir = QFileInfo(path).absoluteDir();

    QJsonArray surfaces;
    for (const Surface& s : m_surfaces) {
        QJsonArray corners;
        for (const QPointF& c : s.corners)
            corners.append(QJsonArray{c.x(), c.y()});

        QJsonObject o{
            {"id", s.id.toString(QUuid::WithoutBraces)},
            {"name", s.name},
            {"corners", corners},
            {"content", s.content == ContentType::Image ? "image" : "testPattern"},
            {"opacity", s.opacity},
            {"visible", s.visible},
        };
        if (!s.imagePath.isEmpty())
            o["image"] = dir.relativeFilePath(s.imagePath);
        surfaces.append(o);
    }

    const QJsonObject root{
        {"format", "LumiCanvas"},
        {"version", kFormatVersion},
        {"surfaces", surfaces},
    };

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

bool Project::load(const QString& path, QString* error, QStringList* missingImages)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = tr("Invalid project file: %1").arg(parseError.errorString());
        return false;
    }

    const QJsonObject root = doc.object();
    if (root.value("format").toString() != "LumiCanvas") {
        if (error)
            *error = tr("Not a LumiCanvas project file.");
        return false;
    }
    if (root.value("version").toInt() > kFormatVersion) {
        if (error)
            *error = tr("This project was saved by a newer version of LumiCanvas.");
        return false;
    }

    const QDir dir = QFileInfo(path).absoluteDir();
    QVector<Surface> loaded;
    for (const QJsonValue& value : root.value("surfaces").toArray()) {
        const QJsonObject o = value.toObject();
        Surface s;
        const QUuid id = QUuid::fromString(o.value("id").toString());
        if (!id.isNull())
            s.id = id;
        s.name = o.value("name").toString();

        const QJsonArray corners = o.value("corners").toArray();
        if (corners.size() == 4) {
            for (int i = 0; i < 4; ++i) {
                const QJsonArray p = corners.at(i).toArray();
                s.corners[i] = QPointF(p.at(0).toDouble(), p.at(1).toDouble());
            }
        }

        s.opacity = std::clamp(float(o.value("opacity").toDouble(1.0)), 0.0f, 1.0f);
        s.visible = o.value("visible").toBool(true);

        const QString image = o.value("image").toString();
        if (!image.isEmpty()) {
            s.imagePath = QDir::cleanPath(dir.absoluteFilePath(image));
            s.image = loadImage(s.imagePath);
            if (s.image.isNull() && missingImages)
                missingImages->append(QDir::toNativeSeparators(s.imagePath));
        }
        s.content = (o.value("content").toString() == "image" && !s.imagePath.isEmpty())
                        ? ContentType::Image
                        : ContentType::TestPattern;
        loaded.push_back(std::move(s));
    }

    m_surfaces = std::move(loaded);
    m_nextNumber = count() + 1;
    emit structureChanged();
    return true;
}

} // namespace lumi
