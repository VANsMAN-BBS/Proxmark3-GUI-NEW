#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "module/mifare/mfcardinfoservice.h"
#include "module/mifare/mfcardtypesel.h"
#include "module/mifare/mfgen3crypto.h"
#include "module/mifare/mfgen3service.h"
#include "module/mifare/mfgen3types.h"
#include "module/mifare/mfnoncestore.h"
#include "module/mifare/mfpm3log.h"

namespace
{
const quint32 kUid = 0xA1B2C3D4u;
const QString kBackdoorHit = QStringLiteral("A31667A8CEC1");

QString keyText(quint64 key)
{
    return mfgen3::NonceStore::formatKey(key);
}

/** Build the (nt, ntEnc, parErr) triple that a Gen3 card would expose. */
mfgen3::NonceSet makeEvidence(quint64 seed, int startKey)
{
    mfgen3::NonceSet set;
    set.uid = kUid;
    set.backdoorKey = kBackdoorHit;
    for(int s = 0; s < mfgen3::kSectors; ++s)
    {
        for(int k = 0; k < 2; ++k)
        {
            const quint64 key = seed + quint64(s * 2 + k) * 0x010101ULL;
            const quint32 nt = quint32(0x11223344u + s * 0x101u + k * 0x11u);
            mfgen3::Crypto1State state;
            mfgen3::crypto1Create(&state, key);
            const quint32 ks = mfgen3::crypto1Word(&state, nt ^ kUid, false);
            set.nt[s][k] = nt;
            set.ntEnc[s][k] = nt ^ ks;

            mfgen3::Crypto1State after;
            mfgen3::crypto1Create(&after, key);
            mfgen3::crypto1Word(&after, nt ^ kUid, false);
            const quint32 next = mfgen3::crypto1Word(&after, 0, false);
            const int parityBit = int((next >> 24) & 1u);
            const int parErr = int(mfgen3::evenParity32(nt & 0xFF)
                                   ^ mfgen3::evenParity32(set.ntEnc[s][k] & 0xFF)
                                   ^ quint32(parityBit)) & 1;
            set.parErr[s][k] = qint8(parErr);
        }
    }
    Q_UNUSED(startKey);
    return set;
}

quint64 trueKey(quint64 seed, int sector, int keyIndex)
{
    return seed + quint64(sector * 2 + keyIndex) * 0x010101ULL;
}

/**
 * Scripted stand-in for the PM3 client: answers exactly like the commands the
 * service issues, using the synthetic evidence as ground truth.
 */
class FakePm3
{
public:
    FakePm3(const QString &dir, const mfgen3::NonceSet &evidence, quint64 seed)
        : m_workDir(dir), m_evidence(evidence), m_seed(seed)
    {
    }

    void setInfoSak(const QString &sak, const QString &uid = QStringLiteral("A1 B2 C3 D4"))
    {
        m_sak = sak;
        m_uid = uid;
    }
    void setInfoAtqa(const QString &atqa) { m_atqa = atqa; }
    void setInfoText(const QString &text) { m_infoText = text; }
    void setDefaultKeyWorks(bool works) { m_defaultKeyWorks = works; }
    void setBackdoorKeys(const QStringList &keys) { m_backdoorKeys = keys; }
    void setRejectDefaultKeys(bool reject) { m_rejectDefaultKeys = reject; }
    void setKeyBEchoesKeyA(bool on) { m_keyBEchoesKeyA = on; }
    /** Write a client dump while `isen` runs, as the real client does. */
    void setDumpOnProbe(const QString &uid, const QByteArray &image)
    {
        m_dumpUid = uid;
        m_dumpOnProbe = image;
    }
    int execCount() const { return m_execCount; }
    QStringList commands() const { return m_commands; }

    QString exec(const QString &cmd, int timeoutMs)
    {
        Q_UNUSED(timeoutMs);
        ++m_execCount;
        m_commands << cmd;

        if(cmd.startsWith(QStringLiteral("hf 14a info")))
        {
            if(!m_infoText.isEmpty())
                return m_infoText;
            return QStringLiteral("[+] UID: %1\n[+] ATQA: %2\n[+] SAK: %3 [1]\n")
                   .arg(m_uid, m_atqa, m_sak);
        }

        if(cmd.startsWith(QStringLiteral("hf mf isen --collect_fm11rf08s")))
        {
            const QString key = cmd.section(QStringLiteral("-k "), 1).trimmed();
            if(!m_backdoorKeys.contains(key))
                return QStringLiteral("[-] no evidence\n");
            const QString uidHex = QStringLiteral("%1").arg(kUid, 8, 16, QLatin1Char('0')).toUpper();
            const QString path = QDir(m_workDir).filePath(
                                     QStringLiteral("hf-mf-%1-nonces.json").arg(uidHex));
            QString error;
            if(!mfgen3::NonceStore::savePm3File(path, m_evidence, QByteArray(), false, &error))
                return QStringLiteral("[-] write failed\n");
            if(!m_dumpOnProbe.isEmpty())
            {
                QFile dump(QDir(m_workDir).filePath(
                               QStringLiteral("hf-mf-%1-dump.bin").arg(m_dumpUid)));
                if(dump.open(QIODevice::WriteOnly | QIODevice::Truncate))
                {
                    dump.write(m_dumpOnProbe);
                    dump.close();
                }
            }
            return QStringLiteral("[+] Saved nonce/data evidence JSON: %1\n").arg(QFileInfo(path).fileName());
        }

        if(cmd.startsWith(QStringLiteral("hf mf fchk --1k")))
        {
            const bool keyA = cmd.contains(QStringLiteral(" -a "));
            const int block = cmd.section(QStringLiteral("--blk "), 1).section(QLatin1Char(' '), 0, 0).toInt();
            const QString dicName = cmd.section(QStringLiteral("-f "), 1).trimmed();
            const int sector = block / 4;
            const int index = keyA ? 0 : 1;
            QFile dic(QDir(m_workDir).filePath(dicName));
            const QString key = keyText((!keyA && m_keyBEchoesKeyA)
                                        ? trueKey(m_seed, sector, 0)
                                        : trueKey(m_seed, sector, index));
            bool found = false;
            if(dic.open(QIODevice::ReadOnly))
            {
                const QString content = QString::fromLatin1(dic.readAll());
                dic.close();
                found = content.contains(key);
            }
            QString out = QStringLiteral("[+] -----+-----+--------------+---+--------------+----\n");
            out += QStringLiteral("[+]  Sec | Blk | key A        |res| key B        |res\n");
            if(found)
            {
                if(keyA)
                    out += QStringLiteral("[+]   %1 | %2 | %3 | 1 | FFFFFFFFFFFF | 0\n")
                           .arg(sector, 3, 10, QLatin1Char('0')).arg(block, 3, 10, QLatin1Char('0')).arg(key);
                else
                    out += QStringLiteral("[+]   %1 | %2 | FFFFFFFFFFFF | 0 | %3 | 1\n")
                           .arg(sector, 3, 10, QLatin1Char('0')).arg(block, 3, 10, QLatin1Char('0')).arg(key);
            }
            else
            {
                out += QStringLiteral("[+]   %1 | %2 | ------------ | 0 | ------------ | 0\n")
                       .arg(sector, 3, 10, QLatin1Char('0')).arg(block, 3, 10, QLatin1Char('0'));
            }
            return out;
        }

        if(cmd.startsWith(QStringLiteral("hf mf rdbl")))
        {
            int block = -1;
            if(cmd.contains(QStringLiteral("--blk")))
                block = cmd.section(QStringLiteral("--blk "), 1).section(QLatin1Char(' '), 0, 0).toInt();
            else
                block = cmd.section(QLatin1Char(' '), 3, 3).toInt();
            const QString key = cmd.section(QStringLiteral("-k "), 1).trimmed();
            if(block < 0)
                return QString();
            const QStringList defaultKeys = { QStringLiteral("FFFFFFFFFFFF"), QStringLiteral("000000000000"),
                                              QStringLiteral("A0A1A2A3A4A5"), QStringLiteral("D3F7D3F7D3F7") };
            if((m_rejectDefaultKey || m_rejectDefaultKeys) && defaultKeys.contains(key))
                return QStringLiteral("[-] Auth error\n");
            QStringList bytes;
            for(int i = 0; i < 16; ++i)
                bytes << QStringLiteral("%1").arg((block * 16 + i) & 0xFF, 2, 16, QLatin1Char('0')).toUpper();
            return QStringLiteral("[+] %1\n").arg(bytes.join(QLatin1Char(' ')));
        }
        return QString();
    }

    void setRejectDefaultKey(bool reject) { m_rejectDefaultKey = reject; }

private:
    QString m_workDir;
    mfgen3::NonceSet m_evidence;
    quint64 m_seed;
    QString m_sak = QStringLiteral("08");
    QString m_uid = QStringLiteral("A1 B2 C3 D4");
    QString m_atqa = QStringLiteral("00 04");
    QString m_infoText;
    QStringList m_backdoorKeys;
    bool m_defaultKeyWorks = true;
    bool m_rejectDefaultKey = false;
    bool m_rejectDefaultKeys = false;
    bool m_keyBEchoesKeyA = false;
    QByteArray m_dumpOnProbe;
    QString m_dumpUid;
    int m_execCount = 0;
    QStringList m_commands;
};

mfgen3::TaskHooks makeHooks(FakePm3 *fake, QStringList *log = nullptr)
{
    mfgen3::TaskHooks hooks;
    hooks.isConnected = []() { return true; };
    hooks.exec = [fake](const QString &cmd, int timeoutMs) { return fake->exec(cmd, timeoutMs); };
    hooks.cancel = []() { return false; };
    hooks.workDir = [fake]() { return QString(); };
    if(log)
    {
        hooks.log = [log](const QString &text) { log->append(text); };
    }
    hooks.step = [](int, int, const QString &, int) {};
    return hooks;
}

QString infoBlock(const QString &uid, const QString &atqa, const QString &sak,
                  const QStringList &types = QStringList(),
                  const QStringList &extraLines = QStringList())
{
    QString out = QStringLiteral("[+] UID: %1\n[+] ATQA: %2\n[+] SAK: %3 [1]\n")
                  .arg(uid, atqa, sak);
    if(!types.isEmpty())
    {
        out += QStringLiteral("[+] Possible types:\n");
        for(const QString &type : types)
            out += QStringLiteral("[+]    ") + type + QLatin1Char('\n');
    }
    for(const QString &line : extraLines)
        out += line + QLatin1Char('\n');
    return out;
}
} // namespace

class TstMifareGen3 : public QObject
{
    Q_OBJECT

private slots:
    // ---- crapto1 port -----------------------------------------------------
    void crypto1RoundTrip();
    void solveNoncePairRecoversKey();
    void solveNoncePairParityFilter();
    void solveNoncePairCancel();

    // ---- evidence formats -------------------------------------------------
    void pm3EvidenceParsing();
    void pm3EvidenceFieldErrors();
    void pm3EvidenceBlocks();
    void pm3GuiRoundTrip();
    void pm3GuiValidationErrors();
    void formatConversionKeepsValues();
    void formatDetection();
    void dicAndKeyBin();

    // ---- service ----------------------------------------------------------
    void crackGen3Flow();
    void crackGen3WithoutBackdoor();
    void crackGen3DoesNotFabricateKeyB();
    void crackGen3CountsProvenEqualKeyB();
    void dumpUidMatchesFileName();
    void readWholeCardIgnoresForeignDump();
    void readWholeCardReusesMatchingDump();
    void quickReadUnsupportedCard();
    void quickReadUsesKnownKey();

    // ---- Stage 3.2 identity + routing -------------------------------------
    void identityMatrixClassicAndGen3();
    void identityMatrixMagic();
    void identityMatrixNfcAndUnknown();
    void identitySakAloneIsSuspected();
    void identityMagicDetectionUnavailable();
    void identityRealFm11rf08sFixture();
    void quickReadMagicIsReadOnly();
    void quickReadGen3Gate();
    void quickReadClassicNoKeyNoGen3();
    void quickReadOtherFamilies();
    void crackGen3RefusesMagic();
    void crackGen3Refuses4K();
    void quickReadCancelStops();

    // ---- Stage 3.2.3 card-info workflow -----------------------------------
    void cardInfoWorkflowSerialAndConditional();
    void cardInfoWorkflowDoesNotProbeNonClassic();
    void cardInfoWorkflowStopsOnFailureCancelDisconnect();
    void cardInfoWorkflowRefinesFm11rf08s();
    void cardTypeSelectionPriority();

    // ---- Stage 3.1 PM3 detail-log sync ------------------------------------
    void logSyncDoesNotChangeExecutionCount();
};

void TstMifareGen3::crypto1RoundTrip()
{
    const quint64 key = 0xA0A1A2A3A4A5ULL;
    mfgen3::Crypto1State state;
    mfgen3::crypto1Create(&state, key);
    QCOMPARE(mfgen3::crypto1GetLfsr(state), key);

    const quint32 in = 0x12345678u;
    mfgen3::Crypto1State before = state;
    const quint32 word = mfgen3::crypto1Word(&state, in, false);
    mfgen3::lfsrRollbackWord(&state, in, false);
    QCOMPARE(state.odd, before.odd);
    QCOMPARE(state.even, before.even);
    QCOMPARE(mfgen3::crypto1GetLfsr(state), key);
    QVERIFY(word != 0);
}

void TstMifareGen3::solveNoncePairRecoversKey()
{
    const quint64 key = 0xA0A1A2A3A4A5ULL;
    const quint32 nt = 0x11223344u;
    mfgen3::Crypto1State state;
    mfgen3::crypto1Create(&state, key);
    const quint32 ks = mfgen3::crypto1Word(&state, nt ^ kUid, false);
    const quint32 ntEnc = nt ^ ks;

    mfgen3::RecoveryLimits limits;
    const QVector<quint64> candidates = mfgen3::solveNoncePair(kUid, nt, ntEnc, -1, limits,
                                                               std::function<bool()>());
    QVERIFY(candidates.size() > 1000);           // the 2^16-ish state space
    QVERIFY(candidates.size() <= limits.maxCandidates);
    QVERIFY(candidates.contains(key));
}

void TstMifareGen3::solveNoncePairParityFilter()
{
    const quint64 key = 0xA0A1A2A3A4A5ULL;
    const quint32 nt = 0x11223344u;
    mfgen3::Crypto1State state;
    mfgen3::crypto1Create(&state, key);
    const quint32 ntEnc = nt ^ mfgen3::crypto1Word(&state, nt ^ kUid, false);

    int parErr = 0;
    for(int candidate = 0; candidate < 2; ++candidate)
    {
        parErr = candidate;
        if(mfgen3::parityMatches(key, kUid, nt, parErr))
            break;
    }
    QVERIFY(mfgen3::parityMatches(key, kUid, nt, parErr));

    mfgen3::RecoveryLimits limits;
    const QVector<quint64> unfiltered = mfgen3::solveNoncePair(kUid, nt, ntEnc, -1, limits,
                                                               std::function<bool()>());
    const QVector<quint64> filtered = mfgen3::solveNoncePair(kUid, nt, ntEnc, parErr, limits,
                                                             std::function<bool()>());
    QVERIFY(filtered.contains(key));
    QVERIFY(filtered.size() < unfiltered.size());
    QVERIFY(!mfgen3::parityMatches(key, kUid, nt, parErr ^ 1));
}

void TstMifareGen3::solveNoncePairCancel()
{
    const quint64 key = 0xA0A1A2A3A4A5ULL;
    const quint32 nt = 0x44556677u;
    mfgen3::Crypto1State state;
    mfgen3::crypto1Create(&state, key);
    const quint32 ntEnc = nt ^ mfgen3::crypto1Word(&state, nt ^ kUid, false);

    mfgen3::RecoveryLimits limits;
    bool canceled = false;
    const QVector<quint64> candidates = mfgen3::solveNoncePair(
                                            kUid, nt, ntEnc, -1, limits,
                                            []() { return true; }, &canceled);
    QVERIFY(canceled);
    QCOMPARE(candidates.size(), 0);
}

void TstMifareGen3::pm3EvidenceParsing()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const mfgen3::NonceSet evidence = makeEvidence(0xABC000ULL, 0);
    const QString uidHex = QStringLiteral("%1").arg(kUid, 8, 16, QLatin1Char('0')).toUpper();
    const QString path = dir.filePath(QStringLiteral("hf-mf-%1-nonces.json").arg(uidHex));

    QString error;
    QVERIFY2(mfgen3::NonceStore::savePm3File(path, evidence, QByteArray(), false, &error),
             qPrintable(error));

    mfgen3::NonceStore::Pm3Evidence loaded;
    QVERIFY2(mfgen3::NonceStore::loadPm3File(path, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.set.uid, kUid);
    QCOMPARE(loaded.set.validPairs(), 32);
    QCOMPARE(loaded.set.nt[3][1], evidence.nt[3][1]);
    QCOMPARE(loaded.set.ntEnc[3][1], evidence.ntEnc[3][1]);
    QCOMPARE(int(loaded.set.parErr[3][1]), int(evidence.parErr[3][1]));
    QVERIFY(!loaded.hasDump);
}

void TstMifareGen3::pm3EvidenceFieldErrors()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const QString uidHex = QStringLiteral("%1").arg(kUid, 8, 16, QLatin1Char('0')).toUpper();
    const QString path = dir.filePath(QStringLiteral("hf-mf-%1-nonces.json").arg(uidHex));
    const mfgen3::NonceSet evidence = makeEvidence(0xABC000ULL, 0);
    QString error;
    QVERIFY(mfgen3::NonceStore::savePm3File(path, evidence, QByteArray(), false, &error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    // 1) wrong FileType
    {
        QJsonObject broken = root;
        broken.insert(QStringLiteral("FileType"), QStringLiteral("mifare_nonces"));
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(QJsonDocument(broken).toJson());
        out.close();
        mfgen3::NonceStore::Pm3Evidence loaded;
        QVERIFY(!mfgen3::NonceStore::loadPm3File(path, &loaded, &error));
        QVERIFY(error.contains(QStringLiteral("FileType")));
    }
    // 2) malformed nonce hex
    {
        QJsonObject broken = root;
        QJsonObject nt = broken.value(QStringLiteral("nt")).toObject();
        QJsonObject sector = nt.value(QStringLiteral("0")).toObject();
        sector.insert(QStringLiteral("a"), QStringLiteral("ZZ"));
        nt.insert(QStringLiteral("0"), sector);
        broken.insert(QStringLiteral("nt"), nt);
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(QJsonDocument(broken).toJson());
        out.close();
        mfgen3::NonceStore::Pm3Evidence loaded;
        QVERIFY(!mfgen3::NonceStore::loadPm3File(path, &loaded, &error));
        QVERIFY(error.contains(QStringLiteral("nt.0.a")));
    }
    // 3) too few usable pairs
    {
        QJsonObject broken = root;
        broken.insert(QStringLiteral("nt"), QJsonObject());
        broken.insert(QStringLiteral("nt_enc"), QJsonObject());
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(QJsonDocument(broken).toJson());
        out.close();
        mfgen3::NonceStore::Pm3Evidence loaded;
        QVERIFY(!mfgen3::NonceStore::loadPm3File(path, &loaded, &error));
        QVERIFY(error.contains(QStringLiteral("usable nonce pairs")));
    }
    // 4) file name without the UID segment
    {
        const QString wrongName = dir.filePath(QStringLiteral("nonces.json"));
        QVERIFY(mfgen3::NonceStore::savePm3File(wrongName, evidence, QByteArray(), false, &error));
        mfgen3::NonceStore::Pm3Evidence loaded;
        QVERIFY(!mfgen3::NonceStore::loadPm3File(wrongName, &loaded, &error));
        QVERIFY(error.contains(QStringLiteral("filename")));
    }
}

void TstMifareGen3::pm3EvidenceBlocks()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const QString uidHex = QStringLiteral("%1").arg(kUid, 8, 16, QLatin1Char('0')).toUpper();
    const QString path = dir.filePath(QStringLiteral("hf-mf-%1-nonces.json").arg(uidHex));
    QByteArray image(1024, '\0');
    for(int i = 0; i < 1024; ++i)
        image[i] = char(i & 0xFF);
    const mfgen3::NonceSet evidence = makeEvidence(0xABC000ULL, 0);
    QString error;
    QVERIFY(mfgen3::NonceStore::savePm3File(path, evidence, image, true, &error));

    mfgen3::NonceStore::Pm3Evidence loaded;
    QVERIFY2(mfgen3::NonceStore::loadPm3File(path, &loaded, &error), qPrintable(error));
    QVERIFY(loaded.hasDump);
    QCOMPARE(loaded.dump.size(), 1024);
    QCOMPARE(loaded.dump, image);

    // A truncated block table must not be reported as a complete image.
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();
    QJsonObject blocks = root.value(QStringLiteral("blocks")).toObject();
    blocks.remove(QStringLiteral("63"));
    root.insert(QStringLiteral("blocks"), blocks);
    QFile out(path);
    QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    out.write(QJsonDocument(root).toJson());
    out.close();
    mfgen3::NonceStore::Pm3Evidence partial;
    QVERIFY(mfgen3::NonceStore::loadPm3File(path, &partial, &error));
    QVERIFY(!partial.hasDump);
}

void TstMifareGen3::pm3GuiRoundTrip()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const QString path = dir.filePath(QStringLiteral("fm11_nonce_A1B2C3D4.json"));
    const mfgen3::NonceSet evidence = makeEvidence(0x123400ULL, 0);
    QString error;
    QVERIFY2(mfgen3::NonceStore::savePm3GuiFile(path, evidence, &error), qPrintable(error));

    mfgen3::NonceSet loaded;
    QVERIFY2(mfgen3::NonceStore::loadPm3GuiFile(path, &loaded, &error), qPrintable(error));
    QCOMPARE(loaded.uid, evidence.uid);
    QCOMPARE(loaded.backdoorKey, evidence.backdoorKey);
    for(int s = 0; s < mfgen3::kSectors; ++s)
    {
        for(int k = 0; k < 2; ++k)
        {
            QCOMPARE(loaded.nt[s][k], evidence.nt[s][k]);
            QCOMPARE(loaded.ntEnc[s][k], evidence.ntEnc[s][k]);
            QCOMPARE(int(loaded.parErr[s][k]), int(evidence.parErr[s][k]));
        }
    }
}

void TstMifareGen3::pm3GuiValidationErrors()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const QString path = dir.filePath(QStringLiteral("gui.json"));
    const mfgen3::NonceSet evidence = makeEvidence(0x123400ULL, 0);
    QString error;
    QVERIFY(mfgen3::NonceStore::savePm3GuiFile(path, evidence, &error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    file.close();

    auto writeBroken = [&](const QJsonObject &broken) {
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write(QJsonDocument(broken).toJson());
        out.close();
    };

    {   // magic
        QJsonObject broken = root;
        broken.insert(QStringLiteral("magic"), QStringLiteral("PM3GUI_FM11_NONCES_V0"));
        writeBroken(broken);
        mfgen3::NonceSet loaded;
        QVERIFY(!mfgen3::NonceStore::loadPm3GuiFile(path, &loaded, &error));
        QVERIFY(error.contains(QStringLiteral("magic")));
    }
    {   // sector count
        QJsonObject broken = root;
        broken.insert(QStringLiteral("sectors"), 4);
        writeBroken(broken);
        mfgen3::NonceSet loaded;
        QVERIFY(!mfgen3::NonceStore::loadPm3GuiFile(path, &loaded, &error));
        QVERIFY(error.contains(QStringLiteral("sectors")));
    }
    {   // parErr range
        QJsonObject broken = root;
        QJsonArray rows = broken.value(QStringLiteral("parErr")).toArray();
        QJsonArray row = rows.at(0).toArray();
        row.replace(0, 7);
        rows.replace(0, row);
        broken.insert(QStringLiteral("parErr"), rows);
        writeBroken(broken);
        mfgen3::NonceSet loaded;
        QVERIFY(!mfgen3::NonceStore::loadPm3GuiFile(path, &loaded, &error));
        QVERIFY(error.contains(QStringLiteral("parErr")));
    }
}

void TstMifareGen3::formatConversionKeepsValues()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const QString uidHex = QStringLiteral("%1").arg(kUid, 8, 16, QLatin1Char('0')).toUpper();
    const QString pm3Path = dir.filePath(QStringLiteral("hf-mf-%1-nonces.json").arg(uidHex));
    const QString guiPath = dir.filePath(QStringLiteral("converted.json"));
    const mfgen3::NonceSet evidence = makeEvidence(0x777000ULL, 0);
    QString error;
    QVERIFY(mfgen3::NonceStore::savePm3File(pm3Path, evidence, QByteArray(), false, &error));

    mfgen3::NonceStore::Pm3Evidence pm3;
    QVERIFY(mfgen3::NonceStore::loadPm3File(pm3Path, &pm3, &error));
    QVERIFY(mfgen3::NonceStore::savePm3GuiFile(guiPath, pm3.set, &error));
    mfgen3::NonceSet gui;
    QVERIFY(mfgen3::NonceStore::loadPm3GuiFile(guiPath, &gui, &error));
    for(int s = 0; s < mfgen3::kSectors; ++s)
    {
        for(int k = 0; k < 2; ++k)
        {
            QCOMPARE(gui.nt[s][k], pm3.set.nt[s][k]);
            QCOMPARE(gui.ntEnc[s][k], pm3.set.ntEnc[s][k]);
            QCOMPARE(int(gui.parErr[s][k]), int(pm3.set.parErr[s][k]));
        }
    }
}

void TstMifareGen3::formatDetection()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const QString uidHex = QStringLiteral("%1").arg(kUid, 8, 16, QLatin1Char('0')).toUpper();
    const QString pm3Path = dir.filePath(QStringLiteral("hf-mf-%1-nonces.json").arg(uidHex));
    const QString guiPath = dir.filePath(QStringLiteral("gui.json"));
    const QString junkPath = dir.filePath(QStringLiteral("junk.json"));
    const mfgen3::NonceSet evidence = makeEvidence(0x1000ULL, 0);
    QString error;
    QVERIFY(mfgen3::NonceStore::savePm3File(pm3Path, evidence, QByteArray(), false, &error));
    QVERIFY(mfgen3::NonceStore::savePm3GuiFile(guiPath, evidence, &error));
    QFile junk(junkPath);
    QVERIFY(junk.open(QIODevice::WriteOnly));
    junk.write("{\"hello\":1}");
    junk.close();

    QCOMPARE(mfgen3::NonceStore::detectFormat(pm3Path), QStringLiteral("pm3"));
    QCOMPARE(mfgen3::NonceStore::detectFormat(guiPath), QStringLiteral("pm3gui"));
    QCOMPARE(mfgen3::NonceStore::detectFormat(junkPath), QStringLiteral("unknown"));
}

void TstMifareGen3::dicAndKeyBin()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const QString dicPath = dir.filePath(QStringLiteral("fm11_sec0A.dic"));
    QVector<quint64> keys{0xA0A1A2A3A4A5ULL, 0xFFFFFFFFFFFFULL};
    QString error;
    QVERIFY(mfgen3::NonceStore::writeDicFile(dicPath, keys, &error));
    QFile dic(dicPath);
    QVERIFY(dic.open(QIODevice::ReadOnly));
    QCOMPARE(QString::fromLatin1(dic.readAll()), QStringLiteral("A0A1A2A3A4A5\nFFFFFFFFFFFF\n"));
    dic.close();
    QCOMPARE(mfgen3::NonceStore::readKeyList(dicPath).size(), 2);

    mfgen3::CrackResult result;
    result.keys[0][0] = QStringLiteral("A0A1A2A3A4A5");
    result.keys[15][1] = QStringLiteral("112233445566");
    const QByteArray image = mfgen3::NonceStore::buildKeyBin(result);
    QCOMPARE(image.size(), 192);
    const QString binPath = dir.filePath(QStringLiteral("key.bin"));
    QFile bin(binPath);
    QVERIFY(bin.open(QIODevice::WriteOnly));
    bin.write(image);
    bin.close();

    QString loaded[mfgen3::kSectors][2];
    QVERIFY(mfgen3::NonceStore::loadKeyBin(binPath, loaded));
    QCOMPARE(loaded[0][0], QStringLiteral("A0A1A2A3A4A5"));
    QCOMPARE(loaded[15][1], QStringLiteral("112233445566"));
    QCOMPARE(loaded[0][1], QString());
}

void TstMifareGen3::crackGen3Flow()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const quint64 seed = 0x500000ULL;
    const mfgen3::NonceSet evidence = makeEvidence(seed, 0);

    FakePm3 fake(dir.path(), evidence, seed);
    fake.setBackdoorKeys(QStringList() << kBackdoorHit);
    QStringList logs;
    mfgen3::TaskHooks hooks = makeHooks(&fake, &logs);
    hooks.workDir = [&dir]() { return dir.path(); };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CrackResult result = service.crackGen3(mfgen3::FwFlavor::New);

    QVERIFY2(result.success(), qPrintable(result.error + QStringLiteral("\nCMDS:\n")
                                            + fake.commands().join(QStringLiteral("\n"))
                                            + QStringLiteral("\nLOG:\n") + logs.join(QStringLiteral("\n"))));
    QCOMPARE(result.backdoorKey, kBackdoorHit);
    QCOMPARE(result.recoveredKeyCount(), 32);
    QVERIFY(result.hasDump);
    QCOMPARE(result.dump.size(), 1024);
    QVERIFY(result.candidateTotal > 0);
    QVERIFY(!result.onlyData);
    for(int s = 0; s < mfgen3::kSectors; ++s)
    {
        for(int k = 0; k < 2; ++k)
            QCOMPARE(result.keys[s][k], keyText(trueKey(seed, s, k)));
    }
    // The recovered keys must be written back into the trailer blocks.
    QCOMPARE(QString::fromLatin1(result.dump.mid(3 * 16, 6).toHex()).toUpper(),
             keyText(trueKey(seed, 0, 0)));
    QCOMPARE(QString::fromLatin1(result.dump.mid(3 * 16 + 10, 6).toHex()).toUpper(),
             keyText(trueKey(seed, 0, 1)));
    // The card key cache is created for the next run.
    QVERIFY(QFileInfo::exists(service.cardKeyFilePath(evidence.uid)));
}

void TstMifareGen3::crackGen3WithoutBackdoor()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const quint64 seed = 0x600000ULL;
    const mfgen3::NonceSet evidence = makeEvidence(seed, 0);
    FakePm3 fake(dir.path(), evidence, seed);
    fake.setBackdoorKeys(QStringList());   // nothing matches
    mfgen3::TaskHooks hooks = makeHooks(&fake);
    hooks.workDir = [&dir]() { return dir.path(); };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CrackResult result = service.crackGen3(mfgen3::FwFlavor::New);
    QVERIFY(!result.success());
    QCOMPARE(result.errorCode, QString::fromLatin1(mfgen3::err::kNotGen3));
    QVERIFY(!result.hasDump);
}

void TstMifareGen3::crackGen3DoesNotFabricateKeyB()
{
    // Both slots carry the same nonce pair, so the solver returns identical
    // candidate lists.  The old code copied the KeyA result into KeyB without
    // touching the card and counted both as recovered.
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const quint64 seed = 0x700000ULL;
    mfgen3::NonceSet evidence = makeEvidence(seed, 0);
    for(int s = 0; s < mfgen3::kSectors; ++s)
    {
        evidence.nt[s][1] = evidence.nt[s][0];
        evidence.ntEnc[s][1] = evidence.ntEnc[s][0];
        evidence.parErr[s][1] = evidence.parErr[s][0];
    }

    FakePm3 fake(dir.path(), evidence, seed);
    fake.setBackdoorKeys(QStringList() << kBackdoorHit);
    mfgen3::TaskHooks hooks = makeHooks(&fake);
    hooks.workDir = [&dir]() { return dir.path(); };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CrackResult result = service.crackGen3(mfgen3::FwFlavor::New);
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.recoveredKeyCount(), 16);
    QCOMPARE(result.verifiedKeyCount(), 16);
    QCOMPARE(result.inferredKeyCount(), 0);
    for(int s = 0; s < mfgen3::kSectors; ++s)
    {
        QCOMPARE(result.keys[s][0], keyText(trueKey(seed, s, 0)));
        QVERIFY(result.keys[s][1].isEmpty());
        QVERIFY(result.verified[s][0]);
        QVERIFY(!result.verified[s][1]);
    }
}

void TstMifareGen3::crackGen3CountsProvenEqualKeyB()
{
    // genuine KeyA == KeyB: both slots must be verified and counted.
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const quint64 seed = 0x710000ULL;
    mfgen3::NonceSet evidence = makeEvidence(seed, 0);
    for(int s = 0; s < mfgen3::kSectors; ++s)
    {
        const quint64 key = trueKey(seed, s, 0);
        const quint32 nt = 0x55667788u + quint32(s);
        mfgen3::Crypto1State state;
        mfgen3::crypto1Create(&state, key);
        evidence.nt[s][1] = nt;
        evidence.ntEnc[s][1] = nt ^ mfgen3::crypto1Word(&state, nt ^ kUid, false);
        evidence.parErr[s][1] = -1;
    }

    FakePm3 fake(dir.path(), evidence, seed);
    fake.setBackdoorKeys(QStringList() << kBackdoorHit);
    fake.setKeyBEchoesKeyA(true);
    mfgen3::TaskHooks hooks = makeHooks(&fake);
    hooks.workDir = [&dir]() { return dir.path(); };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CrackResult result = service.crackGen3(mfgen3::FwFlavor::New);
    QVERIFY2(result.success(), qPrintable(result.error));
    QCOMPARE(result.recoveredKeyCount(), 32);
    for(int s = 0; s < mfgen3::kSectors; ++s)
    {
        QCOMPARE(result.keys[s][1], result.keys[s][0]);
        QVERIFY(result.verified[s][1]);
    }
}

void TstMifareGen3::dumpUidMatchesFileName()
{
    QCOMPARE(mfgen3::Gen3Service::dumpUidFromFileName(QStringLiteral("hf-mf-A1B2C3D4-dump.bin")),
             QStringLiteral("A1B2C3D4"));
    QCOMPARE(mfgen3::Gen3Service::dumpUidFromFileName(QStringLiteral("hf-mf-a1b2c3d4-dump.bin")),
             QStringLiteral("A1B2C3D4"));
    QCOMPARE(mfgen3::Gen3Service::dumpUidFromFileName(QStringLiteral("hf-mf-04A1B2C3D4E5F6-dump.bin")),
             QStringLiteral("04A1B2C3D4E5F6"));
    QVERIFY(mfgen3::Gen3Service::dumpUidFromFileName(QStringLiteral("hf-mf-A1B2C3D4-nonces.json")).isEmpty());
    QVERIFY(mfgen3::Gen3Service::dumpUidFromFileName(QStringLiteral("dump.bin")).isEmpty());
}

void TstMifareGen3::readWholeCardIgnoresForeignDump()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const quint64 seed = 0x720000ULL;
    const mfgen3::NonceSet evidence = makeEvidence(seed, 0);
    FakePm3 fake(dir.path(), evidence, seed);
    fake.setBackdoorKeys(QStringList() << kBackdoorHit);
    const QByteArray foreign(1024, char(0xEE));
    fake.setDumpOnProbe(QStringLiteral("DEADBEEF"), foreign);
    QStringList logs;
    mfgen3::TaskHooks hooks = makeHooks(&fake, &logs);
    hooks.workDir = [&dir]() { return dir.path(); };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CrackResult result = service.crackGen3(mfgen3::FwFlavor::New);
    QVERIFY2(result.success(), qPrintable(result.error));
    QVERIFY(result.hasDump);
    QCOMPARE(result.dump.left(16), QByteArray::fromHex("000102030405060708090A0B0C0D0E0F"));
    QVERIFY2(!result.dump.startsWith(foreign.left(16)), "a foreign UID's dump must not be adopted");
    QVERIFY2(logs.join(QLatin1Char('\n')).contains(QStringLiteral("忽略 UID 不匹配")),
             qPrintable(logs.join(QLatin1Char('\n'))));
}

void TstMifareGen3::readWholeCardReusesMatchingDump()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const quint64 seed = 0x730000ULL;
    const mfgen3::NonceSet evidence = makeEvidence(seed, 0);
    FakePm3 fake(dir.path(), evidence, seed);
    fake.setBackdoorKeys(QStringList() << kBackdoorHit);
    QByteArray matching(1024, char(0x5A));
    matching[16] = char(0x11);   // block 1 is not a trailer, stays untouched
    fake.setDumpOnProbe(QStringLiteral("A1B2C3D4"), matching);
    QStringList logs;
    mfgen3::TaskHooks hooks = makeHooks(&fake, &logs);
    hooks.workDir = [&dir]() { return dir.path(); };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CrackResult result = service.crackGen3(mfgen3::FwFlavor::New);
    QVERIFY2(result.success(), qPrintable(result.error));
    QVERIFY(result.hasDump);
    QCOMPARE(result.dump.mid(16, 16), matching.mid(16, 16));
    QVERIFY2(logs.join(QLatin1Char('\n')).contains(QStringLiteral("复用客户端 dump")),
             qPrintable(logs.join(QLatin1Char('\n'))));
}

void TstMifareGen3::quickReadUnsupportedCard()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const mfgen3::NonceSet evidence = makeEvidence(0x10ULL, 0);
    FakePm3 fake(dir.path(), evidence, 0x10ULL);
    fake.setInfoSak(QStringLiteral("20"));
    mfgen3::TaskHooks hooks = makeHooks(&fake);
    hooks.workDir = [&dir]() { return dir.path(); };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CrackResult result = service.quickRead(mfgen3::FwFlavor::New);
    QVERIFY(!result.success());
    QCOMPARE(result.errorCode, QString::fromLatin1(mfgen3::err::kUnsupported));
    QCOMPARE(result.cardUid, QStringLiteral("A1B2C3D4"));
}

void TstMifareGen3::quickReadUsesKnownKey()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const mfgen3::NonceSet evidence = makeEvidence(0x20ULL, 0);
    FakePm3 fake(dir.path(), evidence, 0x20ULL);
    fake.setInfoSak(QStringLiteral("08"));
    mfgen3::TaskHooks hooks = makeHooks(&fake);
    hooks.workDir = [&dir]() { return dir.path(); };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CrackResult result = service.quickRead(mfgen3::FwFlavor::New);
    QVERIFY2(result.success(), qPrintable(result.error));
    QVERIFY(result.hasDump);
    QCOMPARE(result.dump.size(), 1024);
    QCOMPARE(result.keySource, QStringLiteral("dictionary"));
}

void TstMifareGen3::identityMatrixClassicAndGen3()
{
    // Normal Classic 1K: ATQA + SAK agree, no magic, hard PRNG.
    {
        const QString info = infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 04"),
                                       QStringLiteral("08"),
                                       QStringList() << QStringLiteral("MIFARE Classic 1K")
                                                     << QStringLiteral("MIFARE Classic 1K CL2"),
                                       QStringList() << QStringLiteral("[+] Prng detection..... hard"));
        const mfgen3::CardIdentity id = mfgen3::Gen3Service::classifyInfo(info, true);
        QVERIFY(id.detected);
        QCOMPARE(int(id.kind), int(mfgen3::CardKind::Classic1K));
        QCOMPARE(int(id.confidence), int(mfgen3::Confidence::Confirmed));
        QCOMPARE(id.uidBytes, 4);
        QCOMPARE(id.sectors, 16);
        QVERIFY(!id.magic);
        QVERIFY(!id.gen3Candidate);
        QVERIFY(id.isClassicFamily());
    }
    // Static nonce evidence: FM11RF08S suspected, Gen3 candidate (not confirmed).
    {
        const QString info = infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 04"),
                                       QStringLiteral("08"),
                                       QStringList() << QStringLiteral("MIFARE Classic 1K"),
                                       QStringList() << QStringLiteral("[+] Static nonce....... yes"));
        const mfgen3::CardIdentity id = mfgen3::Gen3Service::classifyInfo(info, true);
        QCOMPARE(int(id.kind), int(mfgen3::CardKind::FM11RF08S));
        QCOMPARE(int(id.confidence), int(mfgen3::Confidence::Suspected));
        QVERIFY(id.gen3Candidate);
        QVERIFY(id.evidence.join(QStringLiteral(" ")).contains(QStringLiteral("not probed")));
    }
    // Classic 4K: recognized, but the 1K Gen3 flow must not be offered.
    {
        const QString info = infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 02"),
                                       QStringLiteral("18"),
                                       QStringList() << QStringLiteral("MIFARE Classic 4K"));
        const mfgen3::CardIdentity id = mfgen3::Gen3Service::classifyInfo(info, true);
        QCOMPARE(int(id.kind), int(mfgen3::CardKind::Classic4K));
        QCOMPARE(id.sectors, 40);
        QVERIFY(!id.gen3Candidate);
    }
    // 4K with Gen3-like evidence is still not a 1K Gen3 candidate.
    {
        const QString info = infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 02"),
                                       QStringLiteral("18"),
                                       QStringList() << QStringLiteral("MIFARE Classic 4K"),
                                       QStringList() << QStringLiteral("[+] Static nonce....... yes"));
        const mfgen3::CardIdentity id = mfgen3::Gen3Service::classifyInfo(info, true);
        QCOMPARE(int(id.kind), int(mfgen3::CardKind::FM11RF08S));
        QCOMPARE(int(id.confidence), int(mfgen3::Confidence::Suspected));
        QCOMPARE(id.sectors, 40);
        QVERIFY(!id.gen3Candidate);
    }
}

void TstMifareGen3::identityMatrixMagic()
{
    const struct
    {
        const char *cap;
        mfgen3::CardKind kind;
        mfgen3::Confidence confidence;
    } cases[] = {
        { "Gen 1a", mfgen3::CardKind::Gen1A, mfgen3::Confidence::Confirmed },
        { "Gen 2 / CUID", mfgen3::CardKind::Gen2, mfgen3::Confidence::Confirmed },
        { "Write Once / FUID", mfgen3::CardKind::FUID, mfgen3::Confidence::Confirmed },
        { "Gen 3 / APDU ( possibly )", mfgen3::CardKind::Unknown, mfgen3::Confidence::Suspected },
    };
    for(const auto &item : cases)
    {
        const QString info = infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 04"),
                                       QStringLiteral("08"),
                                       QStringList() << QStringLiteral("MIFARE Classic 1K"),
                                       QStringList() << (QStringLiteral("[+] Magic capabilities... ") + item.cap));
        const mfgen3::CardIdentity id = mfgen3::Gen3Service::classifyInfo(info, true);
        QVERIFY2(id.magic, item.cap);
        QCOMPARE(int(id.kind), int(item.kind));
        QCOMPARE(int(id.confidence), int(item.confidence));
        QVERIFY(!id.gen3Candidate);
        QVERIFY(id.kind != mfgen3::CardKind::FM11RF08S);
    }
    // The write-once flag covers FUID and UFUID; the evidence names both.
    {
        const QString info = infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 04"),
                                       QStringLiteral("08"), QStringList(),
                                       QStringList() << QStringLiteral("[+] Magic capabilities... Write Once / FUID"));
        const mfgen3::CardIdentity id = mfgen3::Gen3Service::classifyInfo(info, true);
        QVERIFY(id.evidence.join(QStringLiteral(" ")).contains(QStringLiteral("UFUID")));
    }
}

void TstMifareGen3::identityMatrixNfcAndUnknown()
{
    // Ultralight / NTAG
    {
        const QString info = infoBlock(QStringLiteral("04 A2 33 4A 55 66 80"), QStringLiteral("44 00"),
                                       QStringLiteral("00"),
                                       QStringList() << QStringLiteral("MIFARE Ultralight"));
        const mfgen3::CardIdentity id = mfgen3::Gen3Service::classifyInfo(info, true);
        QCOMPARE(int(id.kind), int(mfgen3::CardKind::UltralightNtag));
        QCOMPARE(int(id.confidence), int(mfgen3::Confidence::Confirmed));
    }
    // DESFire: ATS + DESFire product hint.
    {
        const QString info = infoBlock(QStringLiteral("04 A2 33 4A 55 66 80"), QStringLiteral("03 44"),
                                       QStringLiteral("20"),
                                       QStringList() << QStringLiteral("MIFARE DESFire"),
                                       QStringList() << QStringLiteral("[+]  ATS: [6] 75 77 81 02 80 02"));
        const mfgen3::CardIdentity id = mfgen3::Gen3Service::classifyInfo(info, true);
        QCOMPARE(int(id.kind), int(mfgen3::CardKind::DesFire));
        QVERIFY(!id.classic);
    }
    // ISO14443-4 CPU: ATS but no DESFire hint.
    {
        const QString info = infoBlock(QStringLiteral("04 A2 33 4A 55 66 80"), QStringLiteral("00 44"),
                                       QStringLiteral("20"), QStringList(),
                                       QStringList() << QStringLiteral("[+]  ATS: [6] 75 77 81 02 80 02"));
        const mfgen3::CardIdentity id = mfgen3::Gen3Service::classifyInfo(info, true);
        QCOMPARE(int(id.kind), int(mfgen3::CardKind::Iso14443a4Cpu));
        QCOMPARE(int(id.confidence), int(mfgen3::Confidence::Suspected));
    }
    // No rule matches: Unknown, never guessed.
    {
        const QString info = infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 44"),
                                       QStringLiteral("26"));
        const mfgen3::CardIdentity id = mfgen3::Gen3Service::classifyInfo(info, true);
        QCOMPARE(int(id.kind), int(mfgen3::CardKind::Unknown));
        QCOMPARE(int(id.confidence), int(mfgen3::Confidence::Unknown));
        QVERIFY(!id.gen3Candidate);
    }
}

void TstMifareGen3::identitySakAloneIsSuspected()
{
    const QString info = QStringLiteral("[+] UID: A1 B2 C3 D4\n[+] SAK: 08 [1]\n");
    const mfgen3::CardIdentity id = mfgen3::Gen3Service::classifyInfo(info, true);
    QCOMPARE(int(id.kind), int(mfgen3::CardKind::Classic1K));
    QCOMPARE(int(id.confidence), int(mfgen3::Confidence::Suspected));
    QVERIFY(id.evidence.join(QStringLiteral(" ")).contains(QStringLiteral("ATQA missing")));
}

void TstMifareGen3::identityMagicDetectionUnavailable()
{
    const QString info = infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 04"),
                                   QStringLiteral("08"), QStringList(),
                                   QStringList() << QStringLiteral("[+] Static nonce....... yes"));
    const mfgen3::CardIdentity id = mfgen3::Gen3Service::classifyInfo(info, false);
    QCOMPARE(int(id.kind), int(mfgen3::CardKind::Classic1K));
    QVERIFY(!id.gen3Candidate);
    QVERIFY(id.evidence.join(QStringLiteral(" ")).contains(QStringLiteral("Magic capability detection unavailable")));
}

void TstMifareGen3::identityRealFm11rf08sFixture()
{
    // Captured from the real PM3 iceman client on COM14 with a Fudan
    // FM11RF08S (UID 8A B8 53 79) on the antenna.  This is a hardware-derived
    // fixture, not a synthetic one; it pins the exact wording the client uses
    // (including the two leading spaces before UID/SAK and the absent magic
    // capability block for a non-magic Gen3 card).
    const QString info = QStringLiteral(
        "[=] ---------- ISO14443-A Information ----------\n"
        "[+]  UID: 8A B8 53 79   ( ONUID, re-used )\n"
        "[+] ATQA: 00 04\n"
        "[+]  SAK: 08 [2]\n"
        "[+] Possible types:\n"
        "[+]    MIFARE Classic 1K\n"
        "[=] \n"
        "[=] Proprietary non iso14443-4 card found\n"
        "[=] RATS not supported\n"
        "[+] Prng detection..... weak\n");
    const mfgen3::CardIdentity id = mfgen3::Gen3Service::classifyInfo(info, true);
    QVERIFY(id.detected);
    QCOMPARE(id.uid, QStringLiteral("8AB85379"));
    QCOMPARE(id.atqa, QStringLiteral("0004"));
    QCOMPARE(id.sak, QStringLiteral("08"));
    QCOMPARE(id.uidBytes, 4);
    QCOMPARE(id.sectors, 16);
    QCOMPARE(int(id.kind), int(mfgen3::CardKind::FM11RF08S));
    QCOMPARE(int(id.confidence), int(mfgen3::Confidence::Suspected));
    QVERIFY(!id.magic);
    QVERIFY(id.gen3Candidate);
    QVERIFY(id.evidence.join(QStringLiteral(" ")).contains(QStringLiteral("PRNG: weak")));

    // The same card through a client without the magic section cannot rule out
    // a clone, so one-click must not auto-start Gen3.
    const mfgen3::CardIdentity noMagic = mfgen3::Gen3Service::classifyInfo(info, false);
    QCOMPARE(int(noMagic.kind), int(mfgen3::CardKind::Classic1K));
    QVERIFY(!noMagic.gen3Candidate);
}

void TstMifareGen3::quickReadMagicIsReadOnly()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const mfgen3::NonceSet evidence = makeEvidence(0x30ULL, 0);
    FakePm3 fake(dir.path(), evidence, 0x30ULL);
    fake.setInfoText(infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 04"),
                               QStringLiteral("08"),
                               QStringList() << QStringLiteral("MIFARE Classic 1K"),
                               QStringList() << QStringLiteral("[+] Magic capabilities... Gen 1a")));
    mfgen3::TaskHooks hooks = makeHooks(&fake);
    hooks.workDir = [&dir]() { return dir.path(); };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CrackResult result = service.quickRead(mfgen3::FwFlavor::New);
    QVERIFY(!result.success());
    QCOMPARE(result.errorCode, QString::fromLatin1(mfgen3::err::kUnsupported));
    QCOMPARE(int(result.flow), int(mfgen3::QuickFlow::IdentificationOnly));
    QCOMPARE(int(result.identity.kind), int(mfgen3::CardKind::Gen1A));
    QVERIFY(result.identity.magic);
    for(const QString &cmd : fake.commands())
    {
        QVERIFY2(!cmd.contains(QStringLiteral("rdbl")), qPrintable(cmd));
        QVERIFY2(!cmd.contains(QStringLiteral("isen")), qPrintable(cmd));
        QVERIFY2(!cmd.contains(QStringLiteral("csetuid")), qPrintable(cmd));
        QVERIFY2(!cmd.contains(QStringLiteral("cload")), qPrintable(cmd));
    }
}

void TstMifareGen3::quickReadGen3Gate()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const quint64 seed = 0x700000ULL;
    const mfgen3::NonceSet evidence = makeEvidence(seed, 0);
    FakePm3 fake(dir.path(), evidence, seed);
    fake.setBackdoorKeys(QStringList() << kBackdoorHit);
    fake.setRejectDefaultKeys(true);
    fake.setInfoText(infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 04"),
                               QStringLiteral("08"),
                               QStringList() << QStringLiteral("MIFARE Classic 1K"),
                               QStringList() << QStringLiteral("[+] Static nonce....... yes")));
    mfgen3::TaskHooks hooks = makeHooks(&fake);
    hooks.workDir = [&dir]() { return dir.path(); };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CrackResult result = service.quickRead(mfgen3::FwFlavor::New);
    QVERIFY2(result.success(), qPrintable(result.error + QStringLiteral("\n")
                                          + fake.commands().join(QStringLiteral("\n"))));
    QCOMPARE(int(result.flow), int(mfgen3::QuickFlow::Gen3Crack));
    QCOMPARE(int(result.identity.kind), int(mfgen3::CardKind::FM11RF08S));
    QCOMPARE(int(result.identity.confidence), int(mfgen3::Confidence::Confirmed));
    QCOMPARE(result.backdoorKey, kBackdoorHit);
    bool sawIsen = false;
    for(const QString &cmd : fake.commands())
        sawIsen = sawIsen || cmd.startsWith(QStringLiteral("hf mf isen --collect_fm11rf08s"));
    QVERIFY(sawIsen);
}

void TstMifareGen3::quickReadClassicNoKeyNoGen3()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const mfgen3::NonceSet evidence = makeEvidence(0x80ULL, 0);
    FakePm3 fake(dir.path(), evidence, 0x80ULL);
    fake.setBackdoorKeys(QStringList() << kBackdoorHit);
    fake.setRejectDefaultKeys(true);
    fake.setInfoText(infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 04"),
                               QStringLiteral("08"),
                               QStringList() << QStringLiteral("MIFARE Classic 1K"),
                               QStringList() << QStringLiteral("[+] Prng detection..... hard")));
    mfgen3::TaskHooks hooks = makeHooks(&fake);
    hooks.workDir = [&dir]() { return dir.path(); };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CrackResult result = service.quickRead(mfgen3::FwFlavor::New);
    QVERIFY(!result.success());
    QCOMPARE(result.errorCode, QString::fromLatin1(mfgen3::err::kUnsupported));
    QCOMPARE(int(result.flow), int(mfgen3::QuickFlow::ClassicKnownKey));
    for(const QString &cmd : fake.commands())
        QVERIFY2(!cmd.contains(QStringLiteral("isen")), qPrintable(cmd));
}

void TstMifareGen3::quickReadOtherFamilies()
{
    struct Case
    {
        QString info;
        mfgen3::CardKind kind;
        mfgen3::QuickFlow flow;
    };
    const QVector<Case> cases = {
        { infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 02"), QStringLiteral("18"),
                    QStringList() << QStringLiteral("MIFARE Classic 4K")),
          mfgen3::CardKind::Classic4K, mfgen3::QuickFlow::PublicInfoOnly },
        { infoBlock(QStringLiteral("04 A2 33 4A 55 66 80"), QStringLiteral("44 00"), QStringLiteral("00"),
                    QStringList() << QStringLiteral("MIFARE Ultralight")),
          mfgen3::CardKind::UltralightNtag, mfgen3::QuickFlow::PublicInfoOnly },
        { infoBlock(QStringLiteral("04 A2 33 4A 55 66 80"), QStringLiteral("03 44"), QStringLiteral("20"),
                    QStringList() << QStringLiteral("MIFARE DESFire"),
                    QStringList() << QStringLiteral("[+]  ATS: [6] 75 77 81 02 80 02")),
          mfgen3::CardKind::DesFire, mfgen3::QuickFlow::PublicInfoOnly },
        { infoBlock(QStringLiteral("04 A2 33 4A 55 66 80"), QStringLiteral("00 44"), QStringLiteral("20"),
                    QStringList(), QStringList() << QStringLiteral("[+]  ATS: [6] 75 77 81 02 80 02")),
          mfgen3::CardKind::Iso14443a4Cpu, mfgen3::QuickFlow::PublicInfoOnly },
        { infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 44"), QStringLiteral("26")),
          mfgen3::CardKind::Unknown, mfgen3::QuickFlow::PublicInfoOnly },
    };
    for(const Case &item : cases)
    {
        QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
        QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
        const mfgen3::NonceSet evidence = makeEvidence(0x90ULL, 0);
        FakePm3 fake(dir.path(), evidence, 0x90ULL);
        fake.setInfoText(item.info);
        mfgen3::TaskHooks hooks = makeHooks(&fake);
        hooks.workDir = [&dir]() { return dir.path(); };
        mfgen3::Gen3Service service(hooks);
        const mfgen3::CrackResult result = service.quickRead(mfgen3::FwFlavor::New);
        QVERIFY2(!result.success(), qPrintable(item.info));
        QCOMPARE(result.errorCode, QString::fromLatin1(mfgen3::err::kUnsupported));
        QCOMPARE(int(result.identity.kind), int(item.kind));
        QCOMPARE(int(result.flow), int(item.flow));
        for(const QString &cmd : fake.commands())
        {
            QVERIFY2(!cmd.contains(QStringLiteral("rdbl")), qPrintable(cmd));
            QVERIFY2(!cmd.contains(QStringLiteral("isen")), qPrintable(cmd));
        }
    }
}

void TstMifareGen3::crackGen3RefusesMagic()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const mfgen3::NonceSet evidence = makeEvidence(0xA0ULL, 0);
    FakePm3 fake(dir.path(), evidence, 0xA0ULL);
    fake.setBackdoorKeys(QStringList() << kBackdoorHit);
    fake.setInfoText(infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 04"),
                               QStringLiteral("08"),
                               QStringList() << QStringLiteral("MIFARE Classic 1K"),
                               QStringList() << QStringLiteral("[+] Magic capabilities... Gen 2 / CUID")));
    mfgen3::TaskHooks hooks = makeHooks(&fake);
    hooks.workDir = [&dir]() { return dir.path(); };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CrackResult result = service.crackGen3(mfgen3::FwFlavor::New);
    QVERIFY(!result.success());
    QCOMPARE(result.errorCode, QString::fromLatin1(mfgen3::err::kUnsupported));
    QCOMPARE(int(result.flow), int(mfgen3::QuickFlow::Gen3Crack));
    for(const QString &cmd : fake.commands())
        QVERIFY2(!cmd.contains(QStringLiteral("isen")), qPrintable(cmd));
}

void TstMifareGen3::crackGen3Refuses4K()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const mfgen3::NonceSet evidence = makeEvidence(0xA8ULL, 0);
    FakePm3 fake(dir.path(), evidence, 0xA8ULL);
    fake.setBackdoorKeys(QStringList() << kBackdoorHit);
    fake.setInfoText(infoBlock(QStringLiteral("A1 B2 C3 D4"), QStringLiteral("00 02"),
                               QStringLiteral("18"),
                               QStringList() << QStringLiteral("MIFARE Classic 4K")));
    mfgen3::TaskHooks hooks = makeHooks(&fake);
    hooks.workDir = [&dir]() { return dir.path(); };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CrackResult result = service.crackGen3(mfgen3::FwFlavor::New);
    QVERIFY(!result.success());
    QCOMPARE(result.errorCode, QString::fromLatin1(mfgen3::err::kUnsupported));
    QCOMPARE(int(result.flow), int(mfgen3::QuickFlow::PublicInfoOnly));
    for(const QString &cmd : fake.commands())
        QVERIFY2(!cmd.contains(QStringLiteral("isen")), qPrintable(cmd));
}

void TstMifareGen3::quickReadCancelStops()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const mfgen3::NonceSet evidence = makeEvidence(0xB0ULL, 0);
    FakePm3 fake(dir.path(), evidence, 0xB0ULL);
    mfgen3::TaskHooks hooks = makeHooks(&fake);
    hooks.cancel = []() { return true; };
    hooks.workDir = [&dir]() { return dir.path(); };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CrackResult result = service.quickRead(mfgen3::FwFlavor::New);
    QVERIFY(!result.success());
    QVERIFY(result.errorCode == QString::fromLatin1(mfgen3::err::kNoCard)
            || result.errorCode == QString::fromLatin1(mfgen3::err::kCanceled));
    QCOMPARE(fake.execCount(), 0);
}

void TstMifareGen3::logSyncDoesNotChangeExecutionCount()
{
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/gen3tmp-XXXXXX"));
    QVERIFY2(dir.isValid(), qPrintable(dir.errorString()));
    const mfgen3::NonceSet evidence = makeEvidence(0x20ULL, 0);
    FakePm3 fake(dir.path(), evidence, 0x20ULL);
    fake.setInfoSak(QStringLiteral("08"));

    mfgen3::TaskHooks hooks = makeHooks(&fake);
    hooks.workDir = [&dir]() { return dir.path(); };

    // Attach the Stage 3.1 log compactor to the service log callback.
    mfgen3::Pm3LogFilter filter;
    filter.beginTask(42);
    QStringList compacted;
    hooks.log = [&](const QString &text) {
        compacted << filter.feed(42, text + QLatin1Char('\n'));
    };

    mfgen3::Gen3Service service(hooks);
    const mfgen3::CardIdentity identity = service.identifyCard();
    QVERIFY(identity.detected);

    // Listening to the output must not issue a single extra PM3 command.
    QCOMPARE(fake.execCount(), 1);
    QCOMPARE(fake.commands().size(), 1);
    QCOMPARE(fake.commands().at(0), QStringLiteral("hf 14a info"));
}

void TstMifareGen3::cardInfoWorkflowSerialAndConditional()
{
    const QString classic =
            QStringLiteral("[+]  UID: A1 B2 C3 D4\n[+] ATQA: 00 04\n[+]  SAK: 08 [2]\n");
    const QString mfInfo =
            QStringLiteral("[=] --- ISO14443-a Information ---\n"
                           "[+]  UID: A1 B2 C3 D4\n[+] ATQA: 00 04\n[+]  SAK: 08 [2]\n"
                           "[+] Backdoor key..... A396EFA4E24F\n"
                           "[=] --- Fingerprint\n[+] Fudan FM11RF08S 0490\n"
                           "[+] Prng....... weak\n[+] Static enc nonce... yes\n");

    QStringList commands;
    mfgen3::CardInfoHooks hooks;
    hooks.isConnected = []() { return true; };
    hooks.canceled = []() { return false; };
    hooks.magicDetectionAvailable = true;
    hooks.exec = [&](const QString &cmd, int) {
        commands << cmd;
        if(cmd == mfgen3::CardInfoService::cardInfoCommand())
            return classic;
        if(cmd == mfgen3::CardInfoService::mifareInfoCommand())
            return mfInfo;
        return QString();
    };

    mfgen3::CardInfoService service(hooks);
    const mfgen3::CardIdentity id = service.read();

    QCOMPARE(commands.size(), 2);
    QCOMPARE(commands.at(0), QStringLiteral("hf 14a info"));
    QCOMPARE(commands.at(1), QStringLiteral("hf mf info"));
    QVERIFY(!commands.at(0).contains(QStringLiteral("-nsv")));
    QCOMPARE(service.executedCommands(), commands);
    QVERIFY(id.detected);
    QCOMPARE(id.kind, mfgen3::CardKind::FM11RF08S);
    QCOMPARE(id.confidence, mfgen3::Confidence::Confirmed);
    QVERIFY(id.classic);
    QCOMPARE(id.capacity, 1024);
}

void TstMifareGen3::cardInfoWorkflowDoesNotProbeNonClassic()
{
    const QString ultralight =
            QStringLiteral("[+]  UID: 04 A1 B2 C3 D4 E5 F6\n[+] ATQA: 44 00\n[+]  SAK: 00 [2]\n"
                           "[+] Possible types:\n[+]   MIFARE Ultralight\n");
    QStringList commands;
    mfgen3::CardInfoHooks hooks;
    hooks.isConnected = []() { return true; };
    hooks.canceled = []() { return false; };
    hooks.magicDetectionAvailable = true;
    hooks.exec = [&](const QString &cmd, int) { commands << cmd; return ultralight; };

    mfgen3::CardInfoService service(hooks);
    const mfgen3::CardIdentity id = service.read();
    QCOMPARE(commands.size(), 1);
    QCOMPARE(commands.at(0), QStringLiteral("hf 14a info"));
    QVERIFY(id.detected);
    QCOMPARE(id.kind, mfgen3::CardKind::UltralightNtag);
}

void TstMifareGen3::cardInfoWorkflowStopsOnFailureCancelDisconnect()
{
    const QString classic =
            QStringLiteral("[+]  UID: A1 B2 C3 D4\n[+] ATQA: 00 04\n[+]  SAK: 08 [2]\n");

    // 1. First command returns no output at all.
    {
        QStringList commands;
        mfgen3::CardInfoHooks hooks;
        hooks.isConnected = []() { return true; };
        hooks.canceled = []() { return false; };
        hooks.exec = [&](const QString &cmd, int) { commands << cmd; return QString(); };
        mfgen3::CardInfoService service(hooks);
        QVERIFY(!service.read().detected);
        QCOMPARE(commands.size(), 1);
    }

    // 2. First command has no UID (no card).
    {
        QStringList commands;
        mfgen3::CardInfoHooks hooks;
        hooks.isConnected = []() { return true; };
        hooks.canceled = []() { return false; };
        hooks.exec = [&](const QString &cmd, int) {
            commands << cmd;
            return QStringLiteral("[!] No tag found\n");
        };
        mfgen3::CardInfoService service(hooks);
        QVERIFY(!service.read().detected);
        QCOMPARE(commands.size(), 1);
    }

    // 3. Cancel while the first command is running.
    {
        QStringList commands;
        bool canceled = false;
        mfgen3::CardInfoHooks hooks;
        hooks.isConnected = []() { return true; };
        hooks.canceled = [&]() { return canceled; };
        hooks.exec = [&](const QString &cmd, int) { commands << cmd; canceled = true; return classic; };
        mfgen3::CardInfoService service(hooks);
        service.read();
        QCOMPARE(commands.size(), 1);
    }

    // 4. Disconnect after the first command.
    {
        QStringList commands;
        bool connected = true;
        mfgen3::CardInfoHooks hooks;
        hooks.isConnected = [&]() { return connected; };
        hooks.canceled = []() { return false; };
        hooks.exec = [&](const QString &cmd, int) {
            commands << cmd;
            connected = false;
            return classic;
        };
        mfgen3::CardInfoService service(hooks);
        service.read();
        QCOMPARE(commands.size(), 1);
    }

    // 5. The command wrapper reports failure even though partial text exists.
    {
        QStringList commands;
        bool execOk = false;
        mfgen3::CardInfoHooks hooks;
        hooks.isConnected = []() { return true; };
        hooks.canceled = []() { return false; };
        hooks.lastExecSucceeded = [&]() { return execOk; };
        hooks.exec = [&](const QString &cmd, int) { commands << cmd; return classic; };
        mfgen3::CardInfoService service(hooks);
        service.read();
        QCOMPARE(commands.size(), 1);
    }
}

void TstMifareGen3::cardInfoWorkflowRefinesFm11rf08s()
{
    mfgen3::CardIdentity id;
    id.detected = true;
    id.uid = QStringLiteral("8AB85379");
    id.uidBytes = 4;
    id.atqa = QStringLiteral("0004");
    id.sak = QStringLiteral("08");
    id.classic = true;
    id.family = QStringLiteral("classic1k");
    id.kind = mfgen3::CardKind::Classic1K;
    id.confidence = mfgen3::Confidence::Confirmed;
    id.sectors = 16;
    id.capacity = 1024;

    const QString mfInfo =
            QStringLiteral("[+] Backdoor key..... A396EFA4E24F\n"
                           "[=] --- Fingerprint\n[+] Fudan FM11RF08S 0490\n"
                           "[=] --- Magic Tag Information\n[=] <n/a>\n"
                           "[=] --- PRNG Information\n[+] Prng....... weak\n"
                           "[+] Static enc nonce... yes\n");
    mfgen3::CardInfoService::refineFromMfInfo(&id, mfInfo);

    QCOMPARE(id.kind, mfgen3::CardKind::FM11RF08S);
    QCOMPARE(id.confidence, mfgen3::Confidence::Confirmed);
    QVERIFY(id.staticNonce);
    QCOMPARE(id.prng, 1);
    QVERIFY(id.evidence.join(QStringLiteral("; ")).contains(QStringLiteral("Fudan FM11RF08S")));
    QVERIFY(id.evidence.join(QStringLiteral("; ")).contains(QStringLiteral("A396EFA4E24F")));

    // A card swap between the two commands must not merge the old identity.
    mfgen3::CardIdentity other = id;
    mfgen3::CardInfoService::refineFromMfInfo(
                &other, QStringLiteral("[+]  UID: 11 22 33 44\n[+] Fudan FM11RF08S\n"));
    QVERIFY(!other.detected);
    QVERIFY(other.evidence.join(QStringLiteral("; ")).contains(QStringLiteral("differs")));
}

void TstMifareGen3::cardTypeSelectionPriority()
{
    mfcardtype::SelectionState state;
    QCOMPARE(state.type, 1);
    QVERIFY(!mfcardtype::applyDetected(&state, 4, QStringLiteral("A1B2C3D4"), false));
    QCOMPARE(state.type, 1);

    QVERIFY(mfcardtype::applyDetected(&state, 4, QStringLiteral("A1B2C3D4"), true));
    QCOMPARE(state.type, 4);
    QVERIFY(!state.userLocked);

    mfcardtype::selectManually(&state, 0);
    QVERIFY(state.userLocked);
    // Same card: a later automatic refresh must not undo the manual choice.
    QVERIFY(!mfcardtype::applyDetected(&state, 1, QStringLiteral("A1B2C3D4"), true));
    QCOMPARE(state.type, 0);

    // A different card clears the manual override and applies its layout.
    QVERIFY(mfcardtype::applyDetected(&state, 1, QStringLiteral("11223344"), true));
    QCOMPARE(state.type, 1);
    QVERIFY(!state.userLocked);

    mfgen3::CardIdentity mini;
    mini.kind = mfgen3::CardKind::ClassicMini;
    QCOMPARE(mfcardtype::typeIdForIdentity(mini), 0);
    mfgen3::CardIdentity fourK;
    fourK.kind = mfgen3::CardKind::Classic4K;
    QCOMPARE(mfcardtype::typeIdForIdentity(fourK), 4);
    mfgen3::CardIdentity gen3;
    gen3.kind = mfgen3::CardKind::FM11RF08S;
    QCOMPARE(mfcardtype::typeIdForIdentity(gen3), 1);
    mfgen3::CardIdentity nfc;
    nfc.kind = mfgen3::CardKind::UltralightNtag;
    QCOMPARE(mfcardtype::typeIdForIdentity(nfc), -1);
}

QTEST_GUILESS_MAIN(TstMifareGen3)
#include "tst_mifaregen3.moc"
