#pragma once

#include <QPolygonF>
#include <QSize>
#include <QWidget>

namespace lumi {

class Project;

// 2D editor for the output canvas: shows every surface's quad and lets the
// user drag corners or whole surfaces. Arrow keys nudge by one output pixel.
class WarpEditor : public QWidget {
    Q_OBJECT

public:
    explicit WarpEditor(Project* project, QWidget* parent = nullptr);

    int selectedSurface() const { return m_selected; }
    // Does not emit selectionChanged.
    void setSelectedSurface(int index);
    // Output resolution in pixels; sets the canvas aspect and the nudge step.
    void setCanvasSize(QSize size);

signals:
    void selectionChanged(int index);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct Hit {
        int surface = -1;
        int corner = -1;
    };
    enum class Drag { None, Corner, Surface };

    QRectF canvasRect() const;
    QPointF toWidget(QPointF normalized) const;
    QPointF toNormalized(QPointF widget) const;
    QPolygonF widgetQuad(int index) const;
    Hit hitTest(QPointF pos) const;
    void select(int surface, int corner);
    void drawSurface(QPainter& p, int index, bool selected) const;
    void drawHandles(QPainter& p, int index) const;

    Project* m_project;
    QSize m_canvasSize{1920, 1080};
    int m_selected = -1;
    int m_selectedCorner = -1;
    Drag m_drag = Drag::None;
    QPointF m_grabOffset;     // corner drag: corner minus cursor, normalized
    QPointF m_lastNormalized; // surface drag: previous cursor position
};

} // namespace lumi
