#ifndef MFCARDINFOSERVICE_H
#define MFCARDINFOSERVICE_H

#include "mfgen3types.h"

#include <QString>
#include <QStringList>
#include <functional>

namespace mfgen3
{

/**
 * Read-only card-information workflow used by the single MIFARE "Card Info"
 * entry.
 *
 * The workflow is deliberately serial and bounded:
 *
 *   1. `hf 14a info` (never `-nsv`)
 *   2. only when the first command produced a MIFARE Classic layout:
 *      `hf mf info`
 *
 * Both commands go through the supplied executor, which in the application is
 * Util::execCMDWithOutputInterruptible() on top of the one ClientSession /
 * CommandQueue.  The class is widget free so the routing rules can be tested
 * with a scripted fake.
 */
struct CardInfoHooks
{
    std::function<bool()> isConnected;
    std::function<QString(const QString &command, int timeoutMs)> exec;
    /** Whether the most recent exec() actually completed successfully. */
    std::function<bool()> lastExecSucceeded;
    std::function<bool()> canceled;
    std::function<void(const QString &text)> log;
    bool magicDetectionAvailable = false;
};

class CardInfoService
{
public:
    explicit CardInfoService(const CardInfoHooks &hooks);

    /** Runs the serial workflow and returns the merged read-only identity. */
    CardIdentity read();

    /** Commands actually sent, in order (empty when a condition stopped it). */
    QStringList executedCommands() const { return m_commands; }

    /** Outputs of the two stages, kept for diagnostics and tests. */
    QString infoOutput() const { return m_infoOutput; }
    QString mfInfoOutput() const { return m_mfInfoOutput; }

    /** Command constants, exposed so tests can assert the exact strings. */
    static QString cardInfoCommand() { return QStringLiteral("hf 14a info"); }
    static QString mifareInfoCommand() { return QStringLiteral("hf mf info"); }

    /** Parse the extra evidence printed by `hf mf info`. */
    static void refineFromMfInfo(CardIdentity *identity, const QString &output);

private:
    void log(const QString &text);

    CardInfoHooks m_hooks;
    QStringList m_commands;
    QString m_infoOutput;
    QString m_mfInfoOutput;
};

} // namespace mfgen3

#endif // MFCARDINFOSERVICE_H
