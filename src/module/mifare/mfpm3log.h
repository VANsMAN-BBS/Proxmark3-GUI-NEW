#ifndef MFPM3LOG_H
#define MFPM3LOG_H

#include <QSet>
#include <QString>
#include <QStringList>

namespace mfgen3
{

/**
 * Task-scoped compactor for the Proxmark3 output shown in the MIFARE result
 * panel.
 *
 * The terminal keeps the complete, unfiltered stream.  This class only builds
 * the short "[PM3]" detail log: it splits output into lines, drops separator
 * and percentage-only noise, removes repeated status lines and caps the number
 * of lines one task may add.  Commands, card identity, backdoor probes,
 * nonces, key/block results, failures and completion lines are kept.
 *
 * A filter instance belongs to one task at a time.  Starting a new task
 * clears all state, so output from a previous (possibly cancelled) task can
 * never leak into the next one.
 */
class Pm3LogFilter
{
public:
    /** Begin a task; any previous task state is discarded. */
    void beginTask(quint64 serial);
    /** Stop accepting output (task finished). */
    void endTask();
    /** Stop accepting output immediately (cancel requested). */
    void suspend();
    bool active() const { return m_active; }
    quint64 serial() const { return m_serial; }

    /**
     * Consume one raw output chunk.  Chunks are allowed to split a line.
     * Returns the compacted, non-empty lines that should be appended.
     * A stale serial, an inactive filter or a cancelled task yields nothing.
     */
    QStringList feed(quint64 serial, const QString &chunk, bool cancelled = false);

    /** Emit a trailing partial line (end of task); stale/cancelled is ignored. */
    QStringList flush(quint64 serial);

    int emittedLines() const { return m_emitted; }

    /** Keep the scroll position when the user scrolled away from the bottom. */
    static bool shouldAutoScroll(int value, int maximum);

private:
    bool accepts(quint64 serial, bool cancelled) const;
    bool keepLine(const QString &line) const;
    QStringList takeLine(const QString &line);

    quint64 m_serial = 0;
    bool m_active = false;
    QString m_pending;
    QSet<QString> m_seen;
    int m_emitted = 0;
    bool m_capNoted = false;
};

} // namespace mfgen3

#endif // MFPM3LOG_H
