#ifndef MFGEN3SERVICE_H
#define MFGEN3SERVICE_H

#include "mfgen3types.h"

#include <QString>
#include <QVector>

namespace mfgen3
{

/**
 * Orchestrates the two new MIFARE entry points on top of the existing PM3
 * session (no new transport, no new client):
 *
 *   - quickRead()      : identify the card, then read it with the cheapest
 *                        working path (known keys, or the Gen3 crack for a
 *                        Fudan Gen3 card).
 *   - crackGen3()      : probe backdoor key -> collect nonces -> local candidate
 *                        recovery -> on-card verification -> whole-card read.
 *   - verifyEvidence() : same verify + read pipeline for an already loaded
 *                        evidence file (the archived "candidate verification").
 *
 * The class is deliberately widget free: the page supplies `TaskHooks`, the
 * tests supply a scripted fake (see tests/mifaregen3).
 */
class Gen3Service
{
public:
    explicit Gen3Service(const TaskHooks &hooks);

    CardIdentity identifyCard();
    /**
     * Pure read-only decision tree.  Exposed for the identity matrix tests so
     * the same parsing code the page uses can be exercised without a PM3.
     */
    static CardIdentity classifyInfo(const QString &infoOutput, bool magicDetectionAvailable);
    CrackResult quickRead(FwFlavor flavor);
    CrackResult crackGen3(FwFlavor flavor);
    CrackResult verifyEvidence(const NonceSet &set, FwFlavor flavor);

    /** Last candidate list kept for one (sector, key) slot, for the export menu. */
    QVector<quint64> candidates(int sector, int keyIndex) const;

    /** keys/<UID>.bin cache location used by the service. */
    QString cardKeyFilePath(quint32 uid) const;
    QString keysDir() const;
    QString tmpDir() const;

    /** UID encoded in a client dump name (`hf-mf-<UID>-dump.bin`), or empty. */
    static QString dumpUidFromFileName(const QString &fileName);

private:
    // plumbing
    QString exec(const QString &cmd, int timeoutMs);
    void reportStep(int step, int total, const QString &text, int percent);
    void logLine(const QString &text);
    bool canceled() const { return m_hooks.canceled(); }
    QString newestNonceFile(qint64 sinceMs) const;

    // stages
    CrackResult crackGen3WithIdentity(FwFlavor flavor, const CardIdentity &identity);
    QString probeBackdoor();
    bool loadPm3Evidence(const QString &path, NonceSet *set, QByteArray *dump, bool *hasDump);
    bool readOnlyData(const QString &backdoorKey, CrackResult *result);
    void solveAll(const NonceSet &set, CrackResult *result);
    void verifyCandidates(const NonceSet &set, CrackResult *result, FwFlavor flavor);
    bool readWholeCard(CrackResult *result, FwFlavor flavor, qint64 sessionStart);
    bool readWithKnownKey(quint64 key, CrackResult *result, FwFlavor flavor);

    // helpers
    QString verifySector(const QVector<quint64> &ordered, int sector, int keyIndex, FwFlavor flavor);
    QString readBlock(int block, const QString &key, FwFlavor flavor);
    QVector<quint64> orderCandidates(const QVector<quint64> &candidates, quint32 uid) const;
    void rememberHit(quint64 key);
    static void backfillTrailerKeys(QByteArray *dump, const CrackResult &result);
    static void clearTrailerKeys(QByteArray *dump);
    QString cardKeysDir() const;

    TaskHooks m_hooks;
    QString m_probeFile;
    QVector<quint64> m_candidates[kSectors][2];
    QVector<quint64> m_hitKeys;
    QVector<quint64> m_cardKeys;      ///< keys/<UID>.bin cache
    QVector<quint64> m_libraryKeys;   ///< keys/known_keys.txt
};

} // namespace mfgen3

#endif // MFGEN3SERVICE_H
