#ifndef MFGEN3CRYPTO_H
#define MFGEN3CRYPTO_H

#include <QtGlobal>
#include <QVector>
#include <functional>

/**
 * Minimal Crypto1 / crapto1 subset used by the FM11RF08S (Gen3) recovery flow.
 *
 * The GUI itself has no crypto1 implementation, and the archived specification
 * requires a single self-contained translation unit with no external
 * dependency, so the reference algorithms are ported here:
 *   - LFSR filter and parity helpers
 *   - crypto1 state init / word clocking / rollback / 48-bit key expansion
 *   - lfsr_recovery32 (state recovery from one 32-bit keystream word)
 *
 * Origin: crapto1 (copyright (C) 2008-2014 bla <blapost@gmail.com>,
 * GPL-2.0-or-later) which ships with the Proxmark3 client. Only the code path
 * needed by "one plain nonce + one encrypted nonce -> candidate keys" is kept.
 *
 * The recovery convention matches the archived Android implementation
 * (x1.h -> w2.b.a):
 *      x = nt ^ nt_enc                 (observed keystream word)
 *      y = nt ^ uid                    (input word fed during the window)
 *      states = lfsr_recovery32(x, y)
 *      key    = expand(rollback_word(state, y))
 */
namespace mfgen3
{

/** One Crypto1 LFSR state (two 24-bit halves). */
struct Crypto1State
{
    quint32 odd = 0;
    quint32 even = 0;
};

/** Non-linear filter of the LFSR half. */
quint32 crypto1Filter(quint32 x);
/** Even parity of a 32-bit word (1 when the number of set bits is even). */
quint8 evenParity32(quint32 x);

void crypto1Create(Crypto1State *state, quint64 key);
quint64 crypto1GetLfsr(const Crypto1State &state);
quint32 crypto1Word(Crypto1State *state, quint32 in, bool isEncrypted);
quint32 lfsrRollbackWord(Crypto1State *state, quint32 in, bool feedback);

/** True when the recovered key reproduces the observed parity bit. */
bool parityMatches(quint64 key, quint32 uid, quint32 nt, int parErr);

struct RecoveryLimits
{
    /** Hard cap per (sector, key) group, mirrors the reference client. */
    int maxCandidates = 262144;
    /** Cancel polling stride while walking recovered states. */
    int cancelCheckEvery = 1024;
};

/**
 * Candidate 48-bit keys for one (uid, nt, ntEnc) pair.
 *
 * parErr == -1 disables the parity filter (candidates grow, nothing is lost).
 * wasCanceled reports that cancel() interrupted the walk.
 */
QVector<quint64> solveNoncePair(quint32 uid, quint32 nt, quint32 ntEnc, int parErr,
                                const RecoveryLimits &limits,
                                const std::function<bool()> &cancel,
                                bool *wasCanceled = nullptr);

} // namespace mfgen3

#endif // MFGEN3CRYPTO_H
