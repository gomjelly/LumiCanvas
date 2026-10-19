#include "ui/WarpEditor.h"

#include "core/Project.h"

#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QTransform>

namespace lumi {

namespace {

constexpr double kMargin = 24.0;
constexpr double kHintHeight = 22.0;
constexpr double kHandleRadius = 6.0;
constexpr double kHandleHitRadius = 10.0;
constexpr int kGridDivisions = 8;

} // namespace

WarpEditor::WarpEditor(Project* project, QWidget* parent)
    : QWidget(parent)
    , m_project(project)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(480, 320);

    connect(project, &Project::structureChanged, this, [this] {
        if (m_selected >= m_project->count())
            setSelectedSurface(m_project->count() - 1);
        update();
    });
    connect(project, &Project::surfaceChanged, this, [this] { update(); });
}

void WarpEditor::setSelectedSurface(int index)
{
    if (!m_project->isValidIndex(index))
        index = -1;
    if (index == m_selected)
        return;
    m_selected = index;
    m_selectedCorner = -1;
    update();
}

void WarpEditor::setCanvasSize(QSize size)
{
    if (size.isEmpty() || size == m_canvasSize)
        return;
    m_canvasSize = size;
    update();
}

QRectF WarpEditor::canvasRect() const
{
    const QRectF area = QRectF(rect()).adjusted(kMargin, kMargin, -kMargin, -kMargin - kHintHeight);
    const double aspect = double(m_canvasSize.width()) / m_canvasSize.height();
    double w = area.width();
    double h = w / aspect;
    if (h > area.height()) {
        h = area.height();
        w = h * aspect;
    }
    return QRectF(area.center().x() - w / 2, area.center().y() - h / 2, w, h);
}

QPointF WarpEditor::toWidget(QPointF n) const
{
    const QRectF r = canvasRect();
    return QPointF(r.left() + n.x() * r.width(), r.top() + n.y() * r.height());
}

QPointF WarpEditor::toNormalized(QPointF w) const
{
    const QRectF r = canvasRect();
    return QPointF((w.x() - r.left()) / r.width(), (w.y() - r.top()) / r.height());
}

QPolygonF WarpEditor::widgetQuad(int index) const
{
    QPolygonF quad;
    for (const QPointF& c : m_project->surface(index).corners)
        quad << toWidget(c);
    return quad;
}

WarpEditor::Hit WarpEditor::hitTest(QPointF pos) const
{
    auto cornerAt = [&](int index) {
        const QPolygonF quad = widgetQuad(index);
        for (int k = 0; k < 4; ++k) {
            if (QLineF(quad[k], pos).length() <= kHandleHitRadius)
                return k;
        }
        return -1;
    };

    // The selected surface wins, then the top-most (last drawn) surface.
    if (m_project->isValidIndex(m_selected)) {
        if (const int k = cornerAt(m_selected); k >= 0)
            return {m_selected, k};
    }
    for (int i = m_project->count() - 1; i >= 0; --i) {
        if (const int k = cornerAt(i); k >= 0)
            return {i, k};
    }
    if (m_project->isValidIndex(m_selected)
        && widgetQuad(m_selected).containsPoint(pos, Qt::OddEvenFill))
        return {m_selected, -1};
    for (int i = m_project->count() - 1; i >= 0; --i) {
        if (widgetQuad(i).containsPoint(pos, Qt::OddEvenFill))
            return {i, -1};
    }
    return {};
}

void WarpEditor::select(int surface, int corner)
{
    const bool surfaceChanged = surface != m_selected;
    m_selected = surface;
    m_selectedCorner = corner;
    update();
    if (surfaceChanged)
        emit selectionChanged(surface);
}

void WarpEditor::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(30, 30, 33));

    const QRectF canvas = canvasRect();
    p.fillRect(canvas, Qt::black);

    // Canvas reference grid at 10% steps.
    p.setPen(QPen(QColor(255, 255, 255, 18), 1));
    for (int i = 1; i < 10; ++i) {
        const double x = canvas.left() + canvas.width() * i / 10.0;
        const double y = canvas.top() + canvas.height() * i / 10.0;
        p.drawLine(QPointF(x, canvas.top()), QPointF(x, canvas.bottom()));
        p.drawLine(QPointF(canvas.left(), y), QPointF(canvas.right(), y));
    }
    p.setPen(QPen(QColor(90, 90, 96), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(canvas);

    p.setPen(QColor(140, 140, 146));
    p.drawText(QPointF(canvas.left(), canvas.top() - 6),
               QStringLiteral("%1 × %2").arg(m_canvasSize.width()).arg(m_canvasSize.height()));

    for (int i = 0; i < m_project->count(); ++i)
        drawSurface(p, i, i == m_selected);
    if (m_project->isValidIndex(m_selected))
        drawHandles(p, m_selected);

    p.setPen(QColor(130, 130, 136));
    p.drawText(QRectF(0, height() - kHintHeight - 6, width(), kHintHeight), Qt::AlignCenter,
               tr("Drag corners or surfaces  ·  Arrows nudge 1 px (Shift ×10)  ·  Tab cycles corners  ·  Esc deselects corner"));
}

void WarpEditor::drawSurface(QPainter& p, int index, bool selected) const
{
    const Surface& s = m_project->surface(index);
    const QPolygonF quad = widgetQuad(index);
    const QColor color = surfaceColor(index);

    QColor fill = color;
    fill.setAlpha(!s.visible ? 12 : selected ? 70 : 40);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawPolygon(quad);

    // Content grid, mapped through the same homography the output uses.
    QTransform t;
    const bool mapped = QTransform::squareToQuad(quad, t);
    if (mapped) {
        QColor gridColor = color;
        gridColor.setAlpha(selected ? 140 : 70);
        p.setPen(QPen(gridColor, 1));
        for (int k = 1; k < kGridDivisions; ++k) {
            const double u = double(k) / kGridDivisions;
            p.drawLine(t.map(QPointF(u, 0)), t.map(QPointF(u, 1)));
            p.drawLine(t.map(QPointF(0, u)), t.map(QPointF(1, u)));
        }
    }

    QPen outline(selected ? QColor(Qt::white) : color, selected ? 2.0 : 1.5);
    if (!s.visible)
        outline.setStyle(Qt::DashLine);
    p.setPen(outline);
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(quad);

    const QPointF center = mapped ? t.map(QPointF(0.5, 0.5)) : quad.boundingRect().center();
    p.setPen(Qt::white);
    p.drawText(QRectF(center - QPointF(120, 10), QSizeF(240, 20)), Qt::AlignCenter, s.name);
}

void WarpEditor::drawHandles(QPainter& p, int index) const
{
    const QPolygonF quad = widgetQuad(index);
    const QColor color = surfaceColor(index);
    for (int k = 0; k < 4; ++k) {
        const bool active = k == m_selectedCorner;
        p.setPen(QPen(active ? QColor(Qt::white) : color, 2));
        p.setBrush(active ? color : QColor(0, 0, 0, 200));
        p.drawEllipse(quad[k], kHandleRadius, kHandleRadius);
    }
}

bool WarpEditor::event(QEvent* event)
{
    // Tab normally moves focus; here it cycles the selected corner.
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab)
            && m_project->isValidIndex(m_selected)) {
            const int step = key->key() == Qt::Key_Tab ? 1 : 3;
            m_selectedCorner = m_selectedCorner < 0 ? 0 : (m_selectedCorner + step) % 4;
            update();
            return true;
        }
    }
    return QWidget::event(event);
}

void WarpEditor::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPointF pos = event->position();
    const Hit hit = hitTest(pos);
    select(hit.surface, hit.corner);
    if (hit.surface < 0) {
        m_drag = Drag::None;
        return;
    }
    const QPointF cursor = toNormalized(pos);
    if (hit.corner >= 0) {
        m_drag = Drag::Corner;
        m_grabOffset = m_project->surface(hit.surface).corners[hit.corner] - cursor;
    } else {
        m_drag = Drag::Surface;
        m_lastNormalized = cursor;
    }
}

void WarpEditor::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF pos = event->position();
    const QPointF cursor = toNormalized(pos);

    switch (m_drag) {
    case Drag::Corner:
        m_project->setCorner(m_selected, m_selectedCorner, cursor + m_grabOffset);
        return;
    case Drag::Surface:
        m_project->translate(m_selected, cursor - m_lastNormalized);
        m_lastNormalized = cursor;
        return;
    case Drag::None:
        break;
    }

    const Hit hit = hitTest(pos);
    setCursor(hit.corner >= 0    ? Qt::CrossCursor
              : hit.surface >= 0 ? Qt::SizeAllCursor
                                 : Qt::ArrowCursor);
}

void WarpEditor::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_drag = Drag::None;
    QWidget::mouseReleaseEvent(event);
}

void WarpEditor::keyPressEvent(QKeyEvent* event)
{
    if (!m_project->isValidIndex(m_selected)) {
        QWidget::keyPressEvent(event);
        return;
    }

    QPointF direction;
    switch (event->key()) {
    case Qt::Key_Left: direction = QPointF(-1, 0); break;
    case Qt::Key_Right: direction = QPointF(1, 0); break;
    case Qt::Key_Up: direction = QPointF(0, -1); break;
    case Qt::Key_Down: direction = QPointF(0, 1); break;
    case Qt::Key_Escape:
        m_selectedCorner = -1;
        update();
        return;
    default:
        QWidget::keyPressEvent(event);
        return;
    }

    const double pixels = (event->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0;
    const QPointF delta(direction.x() * pixels / m_canvasSize.width(),
                        direction.y() * pixels / m_canvasSize.height());
    if (m_selectedCorner >= 0) {
        const QPointF corner = m_project->surface(m_selected).corners[m_selectedCorner];
        m_project->setCorner(m_selected, m_selectedCorner, corner + delta);
    } else {
        m_project->translate(m_selected, delta);
    }
}

} // namespace lumi
