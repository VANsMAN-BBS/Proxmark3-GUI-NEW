#include "mfgen3crypto.h"

#include <vector>
#include <cstring>

namespace mfgen3
{

namespace
{
constexpr quint32 LF_POLY_ODD = 0x29CE5C;
constexpr quint32 LF_POLY_EVEN = 0x870804;

inline quint32 bit(quint32 x, int n) { return (x >> n) & 1u; }
inline quint32 bit64(quint64 x, int n) { return quint32((x >> n) & 1u); }
inline quint32 bebit(quint32 x, int n) { return bit(x, n ^ 24); }

/**
 * Out-of-place bucket sort by the MSB (contribution bits) plus the
 * intersection filter of the reference implementation. The reference version
 * keeps per-bucket scratch buffers with a fixed capacity; here the buffers are
 * sized from the real counts, which preserves the produced order and removes
 * the theoretical overflow of the original fixed-size buckets.
 *
 * List 0 is the "even" table, list 1 the "odd" table (same order as the
 * reference call sites).
 */
struct BucketInfo
{
    quint32 *heads[2][0x100] = {};
    quint32 *tails[2][0x100] = {};
    quint32 count = 0;
};

bool bucketSortIntersect(quint32 *estart, quint32 *estop,
                         quint32 *ostart, quint32 *ostop,
                         BucketInfo *info)
{
    quint32 *start[2] = {estart, ostart};
    quint32 *stop[2] = {estop, ostop};

    quint32 counts[2][0x100];
    std::memset(counts, 0, sizeof(counts));
    for(int list = 0; list < 2; ++list)
    {
        for(quint32 *p = start[list]; p <= stop[list]; ++p)
            ++counts[list][(*p & 0xff000000u) >> 24];
    }

    std::vector<quint32> scratch[2];
    for(int list = 0; list < 2; ++list)
    {
        size_t n = 0;
        for(int j = 0; j < 0x100; ++j)
            if(counts[list][j] && counts[1 - list][j])
                n += counts[list][j];
        if(n == 0)
            return false;                 // no intersecting bucket at all
        scratch[list].resize(n);
    }

    // One pass per list, appending into the per-bucket slice of the scratch
    // buffer; this keeps the original relative order inside every bucket.
    quint32 cursor[2][0x100];
    for(int list = 0; list < 2; ++list)
    {
        quint32 offset = 0;
        for(int j = 0; j < 0x100; ++j)
        {
            cursor[list][j] = offset;
            if(counts[list][j] && counts[1 - list][j])
                offset += counts[list][j];
        }
    }
    for(int list = 0; list < 2; ++list)
    {
        for(quint32 *p = start[list]; p <= stop[list]; ++p)
        {
            const quint32 idx = (*p & 0xff000000u) >> 24;
            if(!counts[1 - list][idx])
                continue;                 // dropped: empty in the other list
            scratch[list][cursor[list][idx]++] = *p;
        }
    }

    quint32 nonEmpty = 0;
    for(int j = 0; j < 0x100; ++j)
    {
        if(counts[0][j] && counts[1][j])
            ++nonEmpty;
    }
    info->count = nonEmpty;

    // Write back contiguous per-bucket ranges (bucket order ascending).
    quint32 writeOffset[2] = {0, 0};
    quint32 bucketIndex = 0;
    for(int j = 0; j < 0x100; ++j)
    {
        if(!counts[0][j] || !counts[1][j])
            continue;
        for(int list = 0; list < 2; ++list)
        {
            const quint32 n = counts[list][j];
            quint32 *dst = start[list] + writeOffset[list];
            for(quint32 k = 0; k < n; ++k)
                dst[k] = scratch[list][writeOffset[list] + k];
            info->heads[list][bucketIndex] = dst;
            info->tails[list][bucketIndex] = dst + n - 1;
            writeOffset[list] += n;
        }
        ++bucketIndex;
    }
    return true;
}

inline void updateContribution(quint32 *item, quint32 mask1, quint32 mask2)
{
    quint32 p = *item >> 25;
    p = p << 1 | evenParity32(*item & mask1);
    p = p << 1 | evenParity32(*item & mask2);
    *item = p << 24 | (*item & 0xffffff);
}

inline void extendTable(quint32 *tbl, quint32 **end, int bitValue, quint32 m1, quint32 m2, quint32 in)
{
    in <<= 24;
    for(*tbl <<= 1; tbl <= *end; *++tbl <<= 1)
    {
        if((int)crypto1Filter(*tbl) ^ (int)crypto1Filter(*tbl | 1))
        {
            *tbl |= crypto1Filter(*tbl) ^ quint32(bitValue);
            updateContribution(tbl, m1, m2);
            *tbl ^= in;
        }
        else if((int)crypto1Filter(*tbl) == bitValue)
        {
            *++*end = tbl[1];
            tbl[1] = tbl[0] | 1;
            updateContribution(tbl, m1, m2);
            *tbl++ ^= in;
            updateContribution(tbl, m1, m2);
            *tbl ^= in;
        }
        else
            *tbl-- = *(*end)--;
    }
}

inline void extendTableSimple(quint32 *tbl, quint32 **end, int bitValue)
{
    for(*tbl <<= 1; tbl <= *end; *++tbl <<= 1)
    {
        if((int)crypto1Filter(*tbl) ^ (int)crypto1Filter(*tbl | 1))
            *tbl |= crypto1Filter(*tbl) ^ quint32(bitValue);
        else if((int)crypto1Filter(*tbl) == bitValue)
        {
            *++*end = *++tbl;
            *tbl = tbl[-1] | 1;
        }
        else
            *tbl-- = *(*end)--;
    }
}

// Recursive narrowing, four keystream bits per level (reference algorithm).
// Returns false only when the caller asked to stop.
bool recover(quint32 *ohead, quint32 *otail, quint32 oks,
             quint32 *ehead, quint32 *etail, quint32 eks, int rem,
             std::vector<Crypto1State> &out, quint32 in,
             const std::function<bool()> &cancel)
{
    if(rem == -1)
    {
        for(quint32 *e = ehead; e <= etail; ++e)
        {
            *e = *e << 1 ^ evenParity32(*e & LF_POLY_EVEN) ^ (in & 4 ? 1u : 0u);
            for(quint32 *o = ohead; o <= otail; ++o)
            {
                Crypto1State st;
                st.even = *o;
                st.odd = *e ^ evenParity32(*o & LF_POLY_ODD);
                out.push_back(st);
            }
        }
        return true;
    }

    for(int i = 0; i < 4 && rem--; i++)
    {
        oks >>= 1;
        eks >>= 1;
        in >>= 2;
        extendTable(ohead, &otail, int(oks & 1),
                    LF_POLY_EVEN << 1 | 1, LF_POLY_ODD << 1, 0);
        if(ohead > otail)
            return true;
        extendTable(ehead, &etail, int(eks & 1),
                    LF_POLY_ODD, LF_POLY_EVEN << 1 | 1, in & 3);
        if(ehead > etail)
            return true;
    }

    BucketInfo info;
    if(!bucketSortIntersect(ehead, etail, ohead, otail, &info))
        return true;

    for(int i = int(info.count) - 1; i >= 0; --i)
    {
        if(cancel && cancel())
            return false;
        // [1] is the odd table, [0] the even table (reference call order).
        if(!recover(info.heads[1][i], info.tails[1][i], oks,
                    info.heads[0][i], info.tails[0][i], eks,
                    rem, out, in, cancel))
            return false;
    }
    return true;
}

/** State recovery from 32 bits of keystream, `in` fed during the window. */
std::vector<Crypto1State> lfsrRecovery32(quint32 ks2, quint32 in,
                                         const std::function<bool()> &cancel,
                                         bool *stopped)
{
    std::vector<Crypto1State> out;
    out.reserve(8192);
    if(stopped)
        *stopped = false;

    quint32 oks = 0;
    quint32 eks = 0;
    for(int i = 31; i >= 0; i -= 2)
        oks = oks << 1 | bebit(ks2, i);
    for(int i = 30; i >= 0; i -= 2)
        eks = eks << 1 | bebit(ks2, i);

    std::vector<quint32> oddBuf(static_cast<size_t>(1) << 21);
    std::vector<quint32> evenBuf(static_cast<size_t>(1) << 21);
    quint32 *oddHead = oddBuf.data();
    quint32 *evenHead = evenBuf.data();
    quint32 *oddTail = oddHead - 1;
    quint32 *evenTail = evenHead - 1;

    for(int i = 1 << 20; i >= 0; --i)
    {
        if(int(crypto1Filter(quint32(i))) == int(oks & 1))
            *++oddTail = quint32(i);
        if(int(crypto1Filter(quint32(i))) == int(eks & 1))
            *++evenTail = quint32(i);
    }

    for(int i = 0; i < 4; i++)
    {
        extendTableSimple(oddHead, &oddTail, int((oks >>= 1) & 1));
        extendTableSimple(evenHead, &evenTail, int((eks >>= 1) & 1));
    }

    in = (in >> 16 & 0xff) | (in << 16) | (in & 0xff00);

    if(!recover(oddHead, oddTail, oks, evenHead, evenTail, eks, 11, out, in << 1, cancel))
    {
        if(stopped)
            *stopped = true;
        out.clear();
    }
    return out;
}
} // namespace

quint8 evenParity32(quint32 x)
{
    x ^= x >> 16;
    x ^= x >> 8;
    x ^= x >> 4;
    x &= 0xf;
    return quint8((0x6996u >> x) & 1u);
}

quint32 crypto1Filter(quint32 x)
{
    quint32 f;
    f  = 0xf22c0u >> (x       & 0xf) & 16u;
    f |= 0x6c9c0u >> (x >>  4 & 0xf) &  8u;
    f |= 0x3c8b0u >> (x >>  8 & 0xf) &  4u;
    f |= 0x1e458u >> (x >> 12 & 0xf) &  2u;
    f |= 0x0d938u >> (x >> 16 & 0xf) &  1u;
    return bit(0xEC57E80Au, int(f));
}

void crypto1Create(Crypto1State *state, quint64 key)
{
    state->odd = 0;
    state->even = 0;
    for(int i = 47; i > 0; i -= 2)
    {
        state->odd  = state->odd  << 1 | bit64(key, (i - 1) ^ 7);
        state->even = state->even << 1 | bit64(key, i ^ 7);
    }
}

quint64 crypto1GetLfsr(const Crypto1State &state)
{
    quint64 lfsr = 0;
    for(int i = 23; i >= 0; --i)
    {
        lfsr = lfsr << 1 | bit(state.odd, i ^ 3);
        lfsr = lfsr << 1 | bit(state.even, i ^ 3);
    }
    return lfsr;
}

quint32 crypto1Word(Crypto1State *s, quint32 in, bool isEncrypted)
{
    quint32 ret = 0;
    for(int i = 0; i < 32; ++i)
    {
        const quint32 feed = crypto1Filter(s->odd);
        quint32 feedin = feed & (isEncrypted ? 1u : 0u);
        feedin ^= bebit(in, i) ? 1u : 0u;
        feedin ^= LF_POLY_ODD & s->odd;
        feedin ^= LF_POLY_EVEN & s->even;
        s->even = s->even << 1 | evenParity32(feedin);
        const quint32 t = s->odd;
        s->odd = s->even;
        s->even = t;
        ret |= feed << (i ^ 24);
    }
    return ret;
}

quint32 lfsrRollbackWord(Crypto1State *s, quint32 in, bool feedback)
{
    quint32 ret = 0;
    for(int i = 31; i >= 0; --i)
    {
        s->odd &= 0xffffff;
        const quint32 t = s->odd;
        s->odd = s->even;
        s->even = t;

        quint32 out = s->even & 1;
        out ^= LF_POLY_EVEN & (s->even >>= 1);
        out ^= LF_POLY_ODD & s->odd;
        out ^= bebit(in, i) ? 1u : 0u;
        const quint8 ks = quint8(crypto1Filter(s->odd));
        out ^= feedback ? quint32(ks) : 0u;
        s->even |= quint32(evenParity32(out)) << 23;
        ret |= quint32(ks) << (i ^ 24);
    }
    return ret;
}

bool parityMatches(quint64 key, quint32 uid, quint32 nt, int parErr)
{
    Crypto1State s;
    crypto1Create(&s, key);
    crypto1Word(&s, nt ^ uid, false);          // reproduce the observed window
    const quint32 next = crypto1Word(&s, 0, false);
    const int ksbit0 = int((next >> 24) & 1u);
    const int expected = int(evenParity32(nt & 0xFF) ^ 1u);
    return (ksbit0 ^ (parErr & 1)) == expected;
}

QVector<quint64> solveNoncePair(quint32 uid, quint32 nt, quint32 ntEnc, int parErr,
                                const RecoveryLimits &limits,
                                const std::function<bool()> &cancel,
                                bool *wasCanceled)
{
    if(wasCanceled)
        *wasCanceled = false;

    QVector<quint64> result;
    const quint32 ks = nt ^ ntEnc;
    const quint32 in = nt ^ uid;

    bool stopped = false;
    std::vector<Crypto1State> states = lfsrRecovery32(ks, in, cancel, &stopped);
    if(stopped)
    {
        if(wasCanceled)
            *wasCanceled = true;
        return result;
    }

    const int stride = qMax(1, limits.cancelCheckEvery);
    for(size_t i = 0; i < states.size(); ++i)
    {
        if(cancel && (int(i) % stride) == 0 && i && cancel())
        {
            if(wasCanceled)
                *wasCanceled = true;
            break;
        }
        Crypto1State s = states[i];
        lfsrRollbackWord(&s, in, false);
        const quint64 key = crypto1GetLfsr(s);
        if(key == 0)
            continue;
        if(parErr != -1 && !parityMatches(key, uid, nt, parErr))
            continue;
        result.append(key);
        if(result.size() >= limits.maxCandidates)
            break;
    }
    return result;
}

} // namespace mfgen3
