#include "core/History.h"

namespace lumi {

namespace {

class SurfacesCommand : public QUndoCommand {
public:
    SurfacesCommand(Project* project, QVector<Surface> before, QVector<Surface> after,
                    const QString& text, int mergeId, int mergeTarget)
        : QUndoCommand(text)
        , m_project(project)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_mergeId(mergeId)
        , m_mergeTarget(mergeTarget)
    {
    }

    void undo() override { m_project->restoreSurfaces(m_before); }

    void redo() override
    {
        // The change was already applied when the command was recorded.
        if (m_firstRedo) {
            m_firstRedo = false;
            return;
        }
        m_project->restoreSurfaces(m_after);
    }

    int id() const override { return m_mergeId; }

    bool mergeWith(const QUndoCommand* other) override
    {
        const auto* next = static_cast<const SurfacesCommand*>(other);
        if (next->m_mergeTarget != m_mergeTarget)
            return false;
        m_after = next->m_after;
        setObsolete(sameSurfaces(m_before, m_after)); // e.g. nudged back and forth
        return true;
    }

private:
    Project* m_project;
    QVector<Surface> m_before;
    QVector<Surface> m_after;
    int m_mergeId;
    int m_mergeTarget;
    bool m_firstRedo = true;
};

} // namespace

History::History(Project* project, QObject* parent)
    : QObject(parent)
    , m_project(project)
{
    m_stack.setUndoLimit(200);
}

void History::begin(const QString& text, int mergeId, int mergeTarget)
{
    if (m_pending)
        end(); // never lose an edit if a caller forgot to close the previous one
    m_pending = Pending{m_project->surfaces(), text, mergeId, mergeTarget};
}

void History::end()
{
    if (!m_pending)
        return;
    Pending pending = std::move(*m_pending);
    m_pending.reset();

    QVector<Surface> after = m_project->surfaces();
    if (sameSurfaces(pending.before, after))
        return; // a click without a drag, or a value set to what it already was
    m_stack.push(new SurfacesCommand(m_project, std::move(pending.before), std::move(after),
                                     pending.text, pending.mergeId, pending.mergeTarget));
}

} // namespace lumi
