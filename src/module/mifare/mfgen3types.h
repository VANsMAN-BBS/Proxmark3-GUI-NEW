#ifndef MFGEN3TYPES_H
#define MFGEN3TYPES_H

#include <QtGlobal>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QVector>
#include <functional>

/**
 * Shared value types for the MIFARE one-click read / FM11RF08S (Gen3) crack
 * pipeline. Kept header-only and Qt-free apart from core containers so the
 * queue/parsing layer and the tests can use it without a widget dependency.
 */
namespace mfgen3
{

/** MIFARE Classic 1K layout used by every Gen3 report and dump. */
constexpr int kSectors = 16;

/** Whitelisted Fudan backdoor keys, probed in this exact order. */
constexpr const char *kBackdoorKeys[4] =
{
    "A396EFA4E24F",
    "A31667A8CEC1",
    "518B3354E760",
    "73B9836CF168",
};

enum class FwFlavor
{
    New,   ///< --blk/-k/-a/-b argument style (fw == 0 || fw == 3 in the archive)
    Old,   ///< "chk *1 ? d" / "rdbl N A K" argument style
};

/**
 * Card family decided by the read-only identification tree.  The enum is kept
 * flat (instead of "classic/magic/NFC") so the A area and the routing tests can
 * talk about the exact family a user sees.
 */
enum class CardKind
{
    Unknown = 0,
    Classic1K,
    Classic4K,
    ClassicMini,
    FM11RF08S,
    Gen1A,
    Gen2,
    CUID,
    FUID,
    UFUID,
    UltralightNtag,
    DesFire,
    Iso14443a4Cpu,
};

/**
 * Evidence strength.  Only `Confirmed` may unlock an automatic write-free
 * read/Gen3 flow; `Suspected` is shown to the user and may gate a probe, never
 * a destructive action.
 */
enum class Confidence
{
    Unknown = 0,
    Suspected,
    Confirmed,
};

/** Flow chosen by one-click read / crack; the page turns this into text. */
enum class QuickFlow
{
    None = 0,
    IdentificationOnly,   ///< magic / unknown: public info only, no key read
    PublicInfoOnly,       ///< NTAG / DESFire / CPU / Classic 4K: existing info branch
    ClassicKnownKey,      ///< Classic 1K known-key read (dictionary or keys file)
    Gen3Crack,            ///< FM11RF08S pipeline (backdoor -> nonce -> fchk -> read)
    EvidenceVerify,       ///< imported nonce evidence verification
};

/** Evidence container, identical to the archived w2.c / nonce_set_t. */
struct NonceSet
{
    quint32 uid = 0;
    int sectors = kSectors;
    quint32 nt[kSectors][2] = {};
    quint32 ntEnc[kSectors][2] = {};
    qint8 parErr[kSectors][2];
    QString backdoorKey;
    QString source;   ///< "pm3", "pm3gui", "import-pm3", "import-pm3gui"

    NonceSet()
    {
        for(int s = 0; s < kSectors; ++s)
            for(int k = 0; k < 2; ++k)
                parErr[s][k] = -1;
    }

    /** Number of (sector, key) slots that carry a usable nonce pair. */
    int validPairs() const
    {
        int n = 0;
        for(int s = 0; s < sectors && s < kSectors; ++s)
            for(int k = 0; k < 2; ++k)
                if(nt[s][k] != 0 || ntEnc[s][k] != 0)
                    ++n;
        return n;
    }
};

/** Card identity parsed from the client's `hf 14a info` output. */
struct CardIdentity
{
    bool detected = false;
    QString uid;
    QString atqa;
    QString sak;
    QString typeText;      ///< human readable card family
    QString family;        ///< "classic1k" / "classic4k" / "classicmini" / "nfc" / "unknown"
    int sectors = 0;
    int capacity = 0;      ///< bytes when the layout is known (Classic Mini/1K/4K)
    bool classic = false;

    // ---- Stage 3.2 read-only decision tree --------------------------------
    CardKind kind = CardKind::Unknown;
    Confidence confidence = Confidence::Unknown;
    bool magic = false;                 ///< any magic capability was reported
    bool magicDetectionAvailable = false; ///< client printed the magic section
    int uidBytes = 0;                   ///< 4 / 7 / 10
    bool atsPresent = false;            ///< ISO14443-4 answer
    QString ats;
    QStringList possibleTypes;          ///< "Possible types:" block
    QStringList magicCapabilities;      ///< "Magic capabilities..." lines
    bool staticNonce = false;           ///< "Static nonce....... yes"
    int prng = -1;                      ///< -1 unknown/fail, 0 hard, 1 weak
    bool gen3Candidate = false;         ///< classic 1K + no magic + Gen3 evidence
    QStringList evidence;               ///< every read-only signal behind the verdict

    /** 4-byte UID as a number (first 8 hex digits), 0 when unknown. */
    quint32 uidValue() const
    {
        bool ok = false;
        const quint32 v = uid.left(8).toUInt(&ok, 16);
        return ok ? v : 0u;
    }

    bool isClassicFamily() const
    {
        return kind == CardKind::Classic1K || kind == CardKind::Classic4K
               || kind == CardKind::ClassicMini || kind == CardKind::FM11RF08S;
    }
};

/** Result object, mirrors the archived b1 structure plus UI extras. */
struct CrackResult
{
    QString backdoorKey;            ///< hit backdoor key (12 hex) or empty
    QByteArray dump;                ///< 1024-byte image when hasDump
    bool hasDump = false;
    QString keys[kSectors][2];
    /** True when the slot key was proven by an on-card command (fchk/read).
     *  A key that was merely inferred/copied must stay false. */
    bool verified[kSectors][2] = {};
    /** Evidence that produced this result (kept for the export menu). */
    NonceSet nonces;
    bool hasNonces = false;
    bool onlyData = false;          ///< data recovered, real keys unknown
    bool masked = false;            ///< trailer key bytes are zeroed
    bool partial = false;           ///< keys recovered but not every block read
    QString note;
    QString error;
    QString errorCode;              ///< empty on success
    qint64 elapsedMs = 0;
    int candidateTotal = 0;
    int recoveredKeys = 0;
    QString cardType;
    QString cardUid;
    QString keySource;              ///< "backdoor", "keys-file", "dictionary", "none"
    CardIdentity identity;          ///< read-only identity used for routing
    QuickFlow flow = QuickFlow::None;

    int verifiedKeyCount() const
    {
        int n = 0;
        for(int s = 0; s < kSectors; ++s)
            for(int k = 0; k < 2; ++k)
                if(verified[s][k] && !keys[s][k].isEmpty())
                    ++n;
        return n;
    }
    int inferredKeyCount() const
    {
        int n = 0;
        for(int s = 0; s < kSectors; ++s)
            for(int k = 0; k < 2; ++k)
                if(!verified[s][k] && !keys[s][k].isEmpty())
                    ++n;
        return n;
    }
    /** Recovered means independently verified on the card. */
    int recoveredKeyCount() const { return verifiedKeyCount(); }
    bool success() const { return error.isEmpty() && errorCode.isEmpty(); }
};

/** Error codes shared with the page (kept short for logs). */
namespace err
{
constexpr const char *kNone = "";
constexpr const char *kNotConnected = "not-connected";
constexpr const char *kNoCard = "no-card";
constexpr const char *kNotGen3 = "not-gen3";
constexpr const char *kNoEvidence = "no-evidence";
constexpr const char *kNoCandidates = "no-candidates";
constexpr const char *kVerifyMissed = "verify-missed";
constexpr const char *kReadFailed = "read-failed";
constexpr const char *kCanceled = "canceled";
constexpr const char *kException = "exception";
constexpr const char *kUnsupported = "unsupported-card";
} // namespace err

/**
 * Everything the service needs from its host. Filled by the page with the
 * existing client session; replaced by a scripted fake in the unit tests.
 */
struct TaskHooks
{
    std::function<bool()> isConnected;
    std::function<QString(const QString &cmd, int timeoutMs)> exec;
    std::function<bool()> cancel;
    std::function<void(int step, int total, const QString &text, int percent)> step;
    std::function<void(const QString &text)> log;
    std::function<QString()> workDir;
    /**
     * True when the installed client's `hf 14a info` reports the magic
     * capability block.  Without that block the client cannot rule out a
     * magic clone, so one-click read must not auto-start the Gen3 pipeline.
     */
    bool magicDetectionAvailable = true;

    bool connected() const { return isConnected ? isConnected() : true; }
    bool canceled() const { return cancel ? cancel() : false; }
};

} // namespace mfgen3

#endif // MFGEN3TYPES_H
