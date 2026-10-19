#include "core/Project.h"

#include "core/ImageLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace lumi {

namespace {

// 1: surfaces only. 2: canvas size, fit mode, sequence content and progress,
// optional guide display settings.
constexpr int kFormatVersion = 2;

QString fitModeName(FitMode fit)
{
    switch (fit) {
    case FitMode::Fill: return QStringLiteral("fill");
    case FitMode::Stretch: return QStringLiteral("stretch");
    case FitMode::Fit: break;
    }
    return QStringLiteral("fit");
}

FitMode fitModeFromName(const QString& name)
{
    if (name == QLatin1String("fill"))
        return FitMode::Fill;
    if (name == QLatin1String("stretch"))
        return FitMode::Stretch;
    return FitMode::Fit;
}

QString contentName(ContentType content)
{
    switch (content) {
    case ContentType::Image: return QStringLiteral("image");
    case ContentType::Sequence: return QStringLiteral("sequence");
    case ContentType::TestPattern: break;
    }
    return QStringLiteral("testPattern");
}

QString guideModeName(GuideMode mode)
{
    switch (mode) {
    case GuideMode::Highlight: return QStringLiteral("highlight");
    case GuideMode::Onion: return QStringLiteral("onion");
    case GuideMode::Normal: break;
    }
    return QStringLiteral("normal");
}

GuideMode guideModeFromName(const QString& name)
{
    if (name == QLatin1String("normal"))
        return GuideMode::Normal;
    if (name == QLatin1String("onion"))
        return GuideMode::Onion;
    return GuideMode::Highlight;
}

// Keeps slider percentages readable in the file (0.2, not 0.200000003).
double percent(float value)
{
    return qRound(value * 100.0) / 100.0;
}

QJsonObject guideToJson(const GuideSettings& g)
{
    return QJsonObject{
        {"mode", guideModeName(g.mode)},
        {"dimUnchanged", percent(g.dimUnchanged)},
        {"tintChanges", g.tintChanges},
        {"highlightColor", g.highlightColor.name()},
        {"previousOpacity", percent(g.previousOpacity)},
    };
}

GuideSettings guideFromJson(const QJsonObject& o)
{
    GuideSettings g;
    if (o.isEmpty())
        return g;
    g.mode = guideModeFromName(o.value("mode").toString());
    g.dimUnchanged = std::clamp(float(o.value("dimUnchanged").toDouble(g.dimUnchanged)), 0.0f, 1.0f);
    g.tintChanges = o.value("tintChanges").toBool(g.tintChanges);
    const QColor color = QColor::fromString(o.value("highlightColor").toString());
    if (color.isValid())
        g.highlightColor = color;
    g.previousOpacity = std::clamp(float(o.value("previousOpacity").toDouble(g.previousOpacity)), 0.0f, 1.0f);
    return g;
}

} // namespace

bool sameSurface(const Surface& a, const Surface& b)
{
    return a.id == b.id && a.name == b.name && a.corners == b.corners
           && a.canvasSizeMm == b.canvasSizeMm && a.fit == b.fit && a.content == b.content
           && a.imagePath == b.imagePath && a.image.cacheKey() == b.image.cacheKey()
           && a.opacity == b.opacity && a.visible == b.visible;
}

bool sameSurfaces(const QVector<Surface>& a, const QVector<Surface>& b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), sameSurface);
}

QColor surfaceColor(int index)
{
    const double hue = std::fmod(0.08 + index * 0.618033988749895, 1.0);
    return QColor::fromHsvF(float(hue), 0.65f, 0.95f);
}

std::array<float, 4> contentRect(FitMode fit, QSizeF canvas, QSize content)
{
    if (fit == FitMode::Stretch || canvas.isEmpty() || content.isEmpty())
        return {0.0f, 0.0f, 1.0f, 1.0f};
    const double canvasAspect = canvas.width() / canvas.height();
    const double contentAspect = double(content.width()) / content.height();
    const bool wider = contentAspect > canvasAspect;
    double w = 1.0;
    double h = 1.0;
    if (fit == FitMode::Fit) {
        if (wider)
            h = canvasAspect / contentAspect; // full width, bars top and bottom
        else
            w = contentAspect / canvasAspect; // full height, bars left and right
    } else {
        if (wider)
            w = contentAspect / canvasAspect; // full height, sides cropped
        else
            h = canvasAspect / contentAspect; // full width, top and bottom cropped
    }
    return {float((1.0 - w) / 2), float((1.0 - h) / 2), float(w), float(h)};
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

void Project::setCanvasSize(int index, QSizeF sizeMm)
{
    if (!isValidIndex(index) || sizeMm.isEmpty() || m_surfaces[index].canvasSizeMm == sizeMm)
        return;
    m_surfaces[index].canvasSizeMm = sizeMm;
    emit surfaceChanged(index);
}

void Project::setFitMode(int index, FitMode fit)
{
    if (!isValidIndex(index) || m_surfaces[index].fit == fit)
        return;
    m_surfaces[index].fit = fit;
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

void Project::restoreSurfaces(const QVector<Surface>& surfaces)
{
    const bool sameLayout = std::equal(m_surfaces.begin(), m_surfaces.end(), surfaces.begin(),
                                       surfaces.end(),
                                       [](const Surface& a, const Surface& b) { return a.id == b.id; });
    if (!sameLayout) {
        m_surfaces = surfaces;
        emit structureChanged();
        return;
    }
    for (int i = 0; i < count(); ++i) {
        if (!sameSurface(m_surfaces[i], surfaces[i])) {
            m_surfaces[i] = surfaces[i];
            emit surfaceChanged(i);
        }
    }
}

void Project::setGuide(const GuideSettings& guide)
{
    if (m_guide == guide)
        return;
    m_guide = guide;
    emit guideChanged();
}

void Project::clear()
{
    m_surfaces.clear();
    m_nextNumber = 1;
    m_sequence.clear();
    setGuide(GuideSettings());
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
            {"canvasSizeMm", QJsonArray{s.canvasSizeMm.width(), s.canvasSizeMm.height()}},
            {"fit", fitModeName(s.fit)},
            {"content", contentName(s.content)},
            {"opacity", s.opacity},
            {"visible", s.visible},
        };
        if (!s.imagePath.isEmpty())
            o["image"] = dir.relativeFilePath(s.imagePath);
        surfaces.append(o);
    }

    QJsonObject root{
        {"format", "LumiCanvas"},
        {"version", kFormatVersion},
        {"surfaces", surfaces},
        {"guide", guideToJson(m_guide)},
    };
    if (!m_sequence.isEmpty()) {
        root["sequence"] = QJsonObject{
            {"folder", dir.relativeFilePath(m_sequence.folder())},
            {"current", m_sequence.currentIndex()},
        };
    }

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

bool Project::load(const QString& path, QString* error, QStringList* missingFiles)
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

        const QJsonArray canvasSize = o.value("canvasSizeMm").toArray();
        const QSizeF sizeMm(canvasSize.at(0).toDouble(), canvasSize.at(1).toDouble());
        if (!sizeMm.isEmpty())
            s.canvasSizeMm = sizeMm;
        s.fit = fitModeFromName(o.value("fit").toString());
        s.opacity = std::clamp(float(o.value("opacity").toDouble(1.0)), 0.0f, 1.0f);
        s.visible = o.value("visible").toBool(true);

        const QString image = o.value("image").toString();
        if (!image.isEmpty()) {
            s.imagePath = QDir::cleanPath(dir.absoluteFilePath(image));
            s.image = loadImage(s.imagePath);
            if (s.image.isNull() && missingFiles)
                missingFiles->append(QDir::toNativeSeparators(s.imagePath));
        }
        const QString content = o.value("content").toString();
        if (content == QLatin1String("sequence"))
            s.content = ContentType::Sequence;
        else if (content == QLatin1String("image") && !s.imagePath.isEmpty())
            s.content = ContentType::Image;
        else
            s.content = ContentType::TestPattern;
        loaded.push_back(std::move(s));
    }

    m_surfaces = std::move(loaded);
    m_nextNumber = count() + 1;
    setGuide(guideFromJson(root.value("guide").toObject()));

    const QJsonObject sequence = root.value("sequence").toObject();
    const QString folder = sequence.value("folder").toString();
    if (folder.isEmpty()) {
        m_sequence.clear();
    } else {
        const QString absolute = QDir::cleanPath(dir.absoluteFilePath(folder));
        if (m_sequence.load(absolute))
            m_sequence.setCurrentIndex(sequence.value("current").toInt());
        else if (missingFiles)
            missingFiles->append(QDir::toNativeSeparators(absolute));
    }

    emit structureChanged();
    return true;
}

} // namespace lumi
