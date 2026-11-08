#pragma once

#include "core/Sequence.h"

#include <QColor>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

#include <array>

namespace lumi {

enum class ContentType { TestPattern, Image, Sequence };

// How content is placed on the physical canvas.
enum class FitMode {
    Fit,     // whole image visible, aspect kept, empty bars stay dark
    Fill,    // canvas fully covered, aspect kept, image cropped
    Stretch, // image distorted to the canvas
};

struct Surface {
    QUuid id = QUuid::createUuid();
    QString name;
    // Corners in normalized output space (0..1, y down), order: TL, TR, BR, BL.
    std::array<QPointF, 4> corners{QPointF(0.25, 0.25), QPointF(0.75, 0.25),
                                   QPointF(0.75, 0.75), QPointF(0.25, 0.75)};
    // Real-world size of what the corners are aligned to; only the aspect matters.
    QSizeF canvasSizeMm{600.0, 450.0};
    FitMode fit = FitMode::Fit;
    ContentType content = ContentType::TestPattern;
    QString imagePath;
    QImage image; // RGBA8888, loaded from imagePath; not serialized
    float opacity = 1.0f;
    bool visible = true;
};

// How sequence steps are presented to help the user see what to draw next.
enum class GuideMode {
    Normal,    // current step as-is
    Highlight, // parts that changed since the previous step at full strength, the rest dimmed
    Onion,     // current step over a faint copy of the previous one
};

struct GuideSettings {
    GuideMode mode = GuideMode::Highlight;
    float dimUnchanged = 0.25f;  // Highlight: brightness of parts already drawn
    bool tintChanges = false;    // Highlight: colour new parts with highlightColor
    QColor highlightColor{0, 255, 140};
    float previousOpacity = 0.35f; // Onion: strength of the previous step

    // Applied to every image shown as a guide, whatever the mode.
    float brightness = 1.0f;     // overall projector light on the canvas
    bool invert = false;         // dark lines on white become light lines on black
    bool recolor = false;        // draw the image in lineColor, keeping its brightness
    QColor lineColor{255, 200, 60};
    bool grid = false;           // classic drawing grid over the guide
    double gridCellMm = 50.0;    // measured on the real canvas
    float gridStrength = 0.4f;

    bool operator==(const GuideSettings&) const = default;
};

// Field-by-field comparison; images compare by identity, not pixels.
bool sameSurface(const Surface& a, const Surface& b);
bool sameSurfaces(const QVector<Surface>& a, const QVector<Surface>& b);

// Stable per-index colour shared by the editor and the output test pattern.
QColor surfaceColor(int index);

// Placement of content inside the canvas as {x, y, width, height} in canvas UV.
std::array<float, 4> contentRect(FitMode fit, QSizeF canvas, QSize content);

class Project : public QObject {
    Q_OBJECT

public:
    explicit Project(QObject* parent = nullptr);

    const QVector<Surface>& surfaces() const { return m_surfaces; }
    int count() const { return int(m_surfaces.size()); }
    bool isValidIndex(int index) const { return index >= 0 && index < count(); }
    const Surface& surface(int index) const { return m_surfaces.at(index); }

    Sequence& sequence() { return m_sequence; }
    const Sequence& sequence() const { return m_sequence; }

    const GuideSettings& guide() const { return m_guide; }
    void setGuide(const GuideSettings& guide);

    int addSurface();
    void removeSurface(int index);
    void setCorner(int index, int corner, QPointF pos);
    void translate(int index, QPointF delta);
    void setName(int index, const QString& name);
    void setCanvasSize(int index, QSizeF sizeMm);
    void setFitMode(int index, FitMode fit);
    void setOpacity(int index, float opacity);
    void setVisible(int index, bool visible);
    // Switching to Image only succeeds when an image path is already set.
    bool setContent(int index, ContentType content);
    bool setImage(int index, const QString& path);
    // Replaces all surfaces (undo/redo), emitting only the signals that apply.
    void restoreSurfaces(const QVector<Surface>& surfaces);

    void clear();
    bool save(const QString& path, QString* error) const;
    // missingFiles collects images or folders that could not be found.
    bool load(const QString& path, QString* error, QStringList* missingFiles);

signals:
    void structureChanged();        // surfaces added, removed or replaced
    void surfaceChanged(int index); // geometry or properties of one surface
    void guideChanged();

private:
    QVector<Surface> m_surfaces;
    Sequence m_sequence;
    GuideSettings m_guide;
    int m_nextNumber = 1;
};

} // namespace lumi
