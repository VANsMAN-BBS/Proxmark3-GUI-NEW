#ifndef MFCANCELGATE_H
#define MFCANCELGATE_H

#include <QAtomicInt>

namespace mfgen3
{

/**
 * One-shot cancellation flag shared by a Gen3 task, its worker and the GUI.
 *
 * `request()` is sticky for the lifetime of the task (the worker polls it).
 * Once the task has returned, `reset()` must be called so later read-only
 * queries (Refresh / Card Info) are no longer aborted immediately.
 */
class CancelGate
{
public:
    void request() { m_flag.storeRelaxed(1); }
    void reset() { m_flag.storeRelaxed(0); }
    bool requested() const { return m_flag.loadRelaxed() != 0; }
    /** Read-only view for Util::execCMDWithOutputInterruptible. */
    const QAtomicInt *atomic() const { return &m_flag; }

private:
    QAtomicInt m_flag;
};

} // namespace mfgen3

#endif // MFCANCELGATE_H
