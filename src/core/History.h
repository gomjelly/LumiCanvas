#pragma once

#include "core/Project.h"

#include <QObject>
#include <QUndoStack>

#include <optional>

namespace lumi {

// Undo/redo for surface edits (alignment and properties). Callers wrap a change
// in begin()/end(): the surfaces are snapshotted before and compared after, so
// one drag or one property change becomes one undo step. Stepping through the
// sequence and guide display settings are not part of the history.
class History : public QObject {
    Q_OBJECT

public:
    // Edits with the same merge id and target collapse into one undo step
    // while they follow each other (typing a name, nudging one corner).
    enum MergeId {
        NoMerge = -1,
        MergeNudge = 1,
        MergeRename,
        MergeOpacity,
        MergeCanvasSize,
        MergeCornerInput,
    };

    explicit History(Project* project, QObject* parent = nullptr);

    QUndoStack* stack() { return &m_stack; }

    void begin(const QString& text, int mergeId = NoMerge, int mergeTarget = -1);
    void end();
    void clear() { m_stack.clear(); }

private:
    struct Pending {
        QVector<Surface> before;
        QString text;
        int mergeId;
        int mergeTarget;
    };

    Project* m_project;
    QUndoStack m_stack;
    std::optional<Pending> m_pending;
};

} // namespace lumi
