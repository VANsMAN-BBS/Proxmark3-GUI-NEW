#include "mfgen3service.h"

#include "mfgen3crypto.h"
#include "mfnoncestore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace mfgen3
{

namespace
{

constexpr int kIsenTimeoutMs = 60000;
constexpr int kBlockTimeoutMs = 20000;
constexpr int kDumpTimeoutMs = 180000;
constexpr int kMaxVerifyTimeoutMs = 1800000;
constexpr int kMaxCandidates = 262144;

const QRegularExpression &rowRe()
{
    static const QRegularExpression re(
        QStringLiteral("(?im)^\\[\\+\\]\\s*\\d{1,3}\\s*\\|\\s*(\\d{1,3})\\s*\\|\\s*([0-9A-Fa-f]{12})"
                       "\\s*\\|\\s*(\\d+)\\s*\\|\\s*([0-9A-Fa-f]{12})\\s*\\|\\s*(\\d+)\\s*$"));
    return re;
}

const QRegularExpression &keyFoundRe()
{
    static const QRegularExpression re(
        QStringLiteral("(?i)Key\\s+([AB])\\s+for\\s+block\\s+(\\d+)\\s+found:\\s*([0-9A-F]{12})"));
    return re;
}

const QRegularExpression &hex12Re()
{
    static const QRegularExpression re(QStringLiteral("(?i)\\b[0-9A-F]{12}\\b"));
    return re;
}

const QRegularExpression &blockDataRe()
{
    static const QRegularExpression re(QStringLiteral("(?i)\\b(?:[0-9A-F]{2}[ \\t]){15}[0-9A-F]{2}\\b"));
    return re;
}

} // namespace

Gen3Service::Gen3Service(const TaskHooks &hooks) : m_hooks(hooks)
{
}

QString Gen3Service::keysDir() const
{
    QString base = m_hooks.workDir ? m_hooks.workDir() : QString();
    if(base.isEmpty())
        base = QDir::currentPath();
    return QDir(base).filePath(QStringLiteral("pm3gui/keys"));
}

QString Gen3Service::tmpDir() const
{
    QString base = m_hooks.workDir ? m_hooks.workDir() : QString();
    if(base.isEmpty())
        base = QDir::currentPath();
    return QDir(base).filePath(QStringLiteral("pm3gui/tmp"));
}

QString Gen3Service::cardKeyFilePath(quint32 uid) const
{
    return QDir(keysDir()).filePath(QStringLiteral("%1.bin").arg(uid, 8, 16, QLatin1Char('0')).toUpper());
}

QString Gen3Service::dumpUidFromFileName(const QString &fileName)
{
    static const QRegularExpression nameRe(
        QStringLiteral("^hf-mf-([0-9A-Fa-f]{8,20})-dump\\.bin$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = nameRe.match(QFileInfo(fileName).fileName());
    return match.hasMatch() ? match.captured(1).toUpper() : QString();
}

QString Gen3Service::cardKeysDir() const
{
    return keysDir();
}

QVector<quint64> Gen3Service::candidates(int sector, int keyIndex) const
{
    if(sector < 0 || sector >= kSectors || keyIndex < 0 || keyIndex > 1)
        return QVector<quint64>();
    return m_candidates[sector][keyIndex];
}

QString Gen3Service::exec(const QString &cmd, int timeoutMs)
{
    if(canceled())
        return QString();
    // The page logs the command as "[PM3] > ..." right before executing it;
    // a second "$" copy would only duplicate that line in the detail log.
    if(!m_hooks.exec)
        return QString();
    const QString output = m_hooks.exec(cmd, timeoutMs);
    if(output.trimmed().isEmpty())
        logLine(QStringLiteral("(!) ") + QCoreApplication::translate("Gen3Service", "命令无输出（超时或失败）"));
    return output;
}

void Gen3Service::reportStep(int step, int total, const QString &text, int percent)
{
    logLine(QStringLiteral("[%1/%2] %3").arg(step).arg(total).arg(text));
    if(m_hooks.step)
        m_hooks.step(step, total, text, percent);
}

void Gen3Service::logLine(const QString &text)
{
    if(m_hooks.log && !text.isEmpty())
        m_hooks.log(text);
}

QString Gen3Service::newestNonceFile(qint64 sinceMs) const
{
    QString base = m_hooks.workDir ? m_hooks.workDir() : QString();
    if(base.isEmpty())
        base = QDir::currentPath();
    QDir dir(base);
    static const QRegularExpression nameRe(QStringLiteral("^hf-mf-.*nonces.*\\.json$"),
                                           QRegularExpression::CaseInsensitiveOption);
    const QFileInfoList files = dir.entryInfoList(QDir::Files, QDir::Time);
    QString best;
    qint64 bestTime = -1;
    // A little slack: some filesystems report coarser modification stamps than
    // the millisecond clock used here, which would hide a file written during
    // the very command we are waiting for.
    const qint64 since = sinceMs > 2000 ? sinceMs - 2000 : 0;
    for(const QFileInfo &info : files)
    {
        if(!nameRe.match(info.fileName()).hasMatch())
            continue;
        const qint64 stamp = info.lastModified().toMSecsSinceEpoch();
        if(since > 0 && stamp < since)
            continue;
        if(stamp > bestTime)
        {
            bestTime = stamp;
            best = info.absoluteFilePath();
        }
    }
    return best;
}

CardIdentity Gen3Service::classifyInfo(const QString &infoOutput, bool magicDetectionAvailable)
{
    CardIdentity id;
    id.magicDetectionAvailable = magicDetectionAvailable;
    if(infoOutput.trimmed().isEmpty())
        return id;

    const QRegularExpression uidRe(QStringLiteral("UID:\\s*((?:[0-9A-Fa-f]{2}\\s*)+)"));
    const QRegularExpression atqaRe(QStringLiteral("ATQA:\\s*([0-9A-Fa-f]{2})\\s+([0-9A-Fa-f]{2})"));
    const QRegularExpression sakRe(QStringLiteral("SAK:\\s*([0-9A-Fa-f]{2})"));
    const QRegularExpression atsRe(QStringLiteral("ATS:\\s*(?:\\[\\d+\\]\\s*)?([0-9A-Fa-f]{2}(?:\\s+[0-9A-Fa-f]{2})+)"));
    const QRegularExpression typeRe(QStringLiteral("^\\s*\\[\\+\\]\\s{2,}(\\S.*?)\\s*$"));
    const QRegularExpression magicRe(QStringLiteral("Magic capabilities[\\s.\\-:]*([^\\r\\n]+)"));
    const QRegularExpression staticRe(QStringLiteral("Static nonce[\\s.\\-:]*([A-Za-z]+)"));
    const QRegularExpression prngRe(QStringLiteral("Prng detection[\\s.\\-:]*([A-Za-z]+)"));

    QRegularExpressionMatch match = uidRe.match(infoOutput);
    if(match.hasMatch())
        id.uid = match.captured(1).remove(QRegularExpression(QStringLiteral("\\s"))).toUpper();
    match = atqaRe.match(infoOutput);
    if(match.hasMatch())
        id.atqa = (match.captured(1) + match.captured(2)).toUpper();
    match = sakRe.match(infoOutput);
    if(match.hasMatch())
        id.sak = match.captured(1).toUpper();
    match = atsRe.match(infoOutput);
    if(match.hasMatch())
    {
        id.atsPresent = true;
        id.ats = match.captured(1).simplified().toUpper();
    }

    // "Possible types:" block and the magic / PRNG evidence lines.
    bool inTypes = false;
    const QStringList lines = infoOutput.split(QLatin1Char('\n'));
    for(const QString &rawLine : lines)
    {
        const QString line = rawLine;
        if(line.contains(QStringLiteral("Possible types:")))
        {
            inTypes = true;
            continue;
        }
        if(inTypes)
        {
            match = typeRe.match(line);
            if(match.hasMatch())
            {
                id.possibleTypes << match.captured(1).trimmed();
                continue;
            }
            if(line.contains(QStringLiteral("[+]")) || line.contains(QStringLiteral("[=]")))
                inTypes = false;
        }
        match = magicRe.match(line);
        if(match.hasMatch())
        {
            id.magicCapabilities << match.captured(1).trimmed();
            continue;
        }
        match = staticRe.match(line);
        if(match.hasMatch())
        {
            id.staticNonce = match.captured(1).compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
            continue;
        }
        match = prngRe.match(line);
        if(match.hasMatch())
        {
            const QString value = match.captured(1).toLower();
            if(value == QStringLiteral("weak"))
                id.prng = 1;
            else if(value == QStringLiteral("hard"))
                id.prng = 0;
        }
    }

    if(id.uid.isEmpty())
        return id;

    id.detected = true;
    id.uidBytes = id.uid.size() / 2;
    id.evidence << QStringLiteral("UID %1 bytes").arg(id.uidBytes);
    if(!id.atqa.isEmpty())
        id.evidence << QStringLiteral("ATQA ") + id.atqa;
    if(!id.sak.isEmpty())
        id.evidence << QStringLiteral("SAK ") + id.sak;
    if(id.atsPresent)
        id.evidence << QStringLiteral("ATS present (ISO14443-4)");
    for(const QString &type : id.possibleTypes)
        id.evidence << QStringLiteral("Possible type: ") + type;
    for(const QString &cap : id.magicCapabilities)
        id.evidence << QStringLiteral("Magic: ") + cap;
    if(id.staticNonce)
        id.evidence << QStringLiteral("Static nonce: yes");
    if(id.prng == 1)
        id.evidence << QStringLiteral("PRNG: weak");
    else if(id.prng == 0)
        id.evidence << QStringLiteral("PRNG: hard");

    bool sakOk = false;
    const int sak = id.sak.toInt(&sakOk, 16);
    if(!sakOk)
    {
        id.kind = CardKind::Unknown;
        id.confidence = Confidence::Unknown;
        id.family = QStringLiteral("unknown");
        id.typeText = QCoreApplication::translate("Gen3Service", "未知卡型");
        id.evidence << QStringLiteral("SAK missing or invalid");
        return id;
    }

    const bool classicSak = sak == 0x08 || sak == 0x88 || sak == 0x18 || sak == 0x98 || sak == 0x09;

    auto setClassicLayout = [&]() {
        if(sak == 0x08 || sak == 0x88)
        {
            id.family = QStringLiteral("classic1k");
            id.sectors = 16;
            id.capacity = 1024;
        }
        else if(sak == 0x18 || sak == 0x98)
        {
            id.family = QStringLiteral("classic4k");
            id.sectors = 40;
            id.capacity = 4096;
        }
        else
        {
            id.family = QStringLiteral("classicmini");
            id.sectors = 5;
            id.capacity = 320;
        }
        id.classic = true;
    };

    // ---- 1. Magic capabilities win over every protocol hint -----------------
    if(!id.magicCapabilities.isEmpty())
    {
        id.magic = true;
        if(classicSak)
            setClassicLayout();
        else
            id.family = QStringLiteral("unknown");

        const QString caps = id.magicCapabilities.join(QStringLiteral(" ")).toLower();
        if(caps.contains(QStringLiteral("gen 1")))
        {
            id.kind = CardKind::Gen1A;
            id.confidence = Confidence::Confirmed;
            id.typeText = QStringLiteral("Gen 1a/1b magic card");
        }
        else if(caps.contains(QStringLiteral("write once")) || caps.contains(QStringLiteral("fuid")))
        {
            id.kind = CardKind::FUID;
            id.confidence = Confidence::Confirmed;
            id.typeText = QStringLiteral("FUID/UFUID (write-once) magic card");
            id.evidence << QStringLiteral("FUID and UFUID share the write-once flag; the exact variant is not readable without a write test");
        }
        else if(caps.contains(QStringLiteral("gen 2")) || caps.contains(QStringLiteral("gen2"))
                || caps.contains(QStringLiteral("cuid")))
        {
            id.kind = CardKind::Gen2;
            id.confidence = Confidence::Confirmed;
            id.typeText = QStringLiteral("Gen 2 / CUID magic card");
            id.evidence << QStringLiteral("Gen 2 and CUID share this flag; CUID is a Suspected variant, not separately provable read-only");
        }
        else
        {
            // "Gen 3 / APDU", Gen 4 GDM/USCUID, QL88, super card... are magic
            // cards, but explicitly *not* the Fudan FM11RF08S this pipeline
            // targets.
            id.kind = CardKind::Unknown;
            id.confidence = Confidence::Suspected;
            id.typeText = QCoreApplication::translate("Gen3Service", "其他魔术卡（只读识别）");
            if(caps.contains(QStringLiteral("gen 3")) || caps.contains(QStringLiteral("apdu")))
                id.evidence << QStringLiteral("Magic Gen3/APDU is not an FM11RF08S");
        }
        id.gen3Candidate = false;
        return id;
    }

    // ---- 2. ISO14443-4 / DESFire / CPU --------------------------------------
    if(sak == 0x20 || sak == 0x24 || sak == 0x28)
    {
        const bool desfireHint = id.possibleTypes.join(QStringLiteral(" ")).contains(
                                     QStringLiteral("DESFire"), Qt::CaseInsensitive);
        if(desfireHint)
        {
            id.kind = CardKind::DesFire;
            id.confidence = Confidence::Confirmed;
            id.typeText = QStringLiteral("MIFARE DESFire");
        }
        else if(id.atsPresent)
        {
            id.kind = CardKind::Iso14443a4Cpu;
            id.confidence = Confidence::Suspected;
            id.typeText = QStringLiteral("ISO14443-4 CPU card");
            id.evidence << QStringLiteral("ATS present, no DESFire product hint: CPU/JCOP suspected");
        }
        else
        {
            id.kind = CardKind::Unknown;
            id.confidence = Confidence::Unknown;
            id.typeText = QCoreApplication::translate("Gen3Service", "未知卡型");
        }
        id.family = QStringLiteral("iso14443-4");
        return id;
    }

    // ---- 3. Ultralight / NTAG ------------------------------------------------
    if(sak == 0x00)
    {
        const QString types = id.possibleTypes.join(QStringLiteral(" "));
        const bool typeHint = types.contains(QStringLiteral("Ultralight"), Qt::CaseInsensitive)
                              || types.contains(QStringLiteral("NTAG"), Qt::CaseInsensitive);
        if(id.atqa == QStringLiteral("4400") || typeHint || id.uidBytes == 7)
        {
            id.kind = CardKind::UltralightNtag;
            id.confidence = (id.atqa == QStringLiteral("4400") || typeHint)
                            ? Confidence::Confirmed : Confidence::Suspected;
            id.typeText = QStringLiteral("MIFARE Ultralight / NTAG");
            id.family = QStringLiteral("nfc");
            return id;
        }
        id.kind = CardKind::Unknown;
        id.family = QStringLiteral("unknown");
        id.typeText = QCoreApplication::translate("Gen3Service", "未知卡型");
        return id;
    }

    // ---- 4. MIFARE Classic family -------------------------------------------
    if(classicSak)
    {
        setClassicLayout();
        const QString types = id.possibleTypes.join(QStringLiteral(" "));
        const bool typeHint = types.contains(QStringLiteral("MIFARE Classic"), Qt::CaseInsensitive);
        const bool atqaAgrees = (id.family == QStringLiteral("classic1k") && id.atqa == QStringLiteral("0004"))
                                || (id.family == QStringLiteral("classic4k") && id.atqa == QStringLiteral("0002"))
                                || (id.family == QStringLiteral("classicmini") && id.atqa == QStringLiteral("0004"));
        const bool atqaConflicts = !id.atqa.isEmpty() && !atqaAgrees;
        if(atqaConflicts)
            id.evidence << QStringLiteral("ATQA/SAK disagree: protocol layout is Suspected");
        if(id.atqa.isEmpty())
            id.evidence << QStringLiteral("ATQA missing: protocol layout is Suspected");

        id.confidence = (!atqaConflicts && !id.atqa.isEmpty())
                        ? Confidence::Confirmed : Confidence::Suspected;
        Q_UNUSED(typeHint);

        id.kind = id.family == QStringLiteral("classic4k") ? CardKind::Classic4K
                  : id.family == QStringLiteral("classicmini") ? CardKind::ClassicMini
                  : CardKind::Classic1K;
        id.typeText = id.family == QStringLiteral("classic4k") ? QStringLiteral("MIFARE Classic 4K")
                      : id.family == QStringLiteral("classicmini") ? QStringLiteral("MIFARE Mini")
                      : QStringLiteral("MIFARE Classic 1K");

        // Gen3 suspicion is read-only evidence only.  The backdoor probe in
        // crackGen3() is the only thing allowed to promote this to Confirmed.
        const bool gen3Evidence = id.staticNonce || id.prng == 1;
        if(gen3Evidence && magicDetectionAvailable
                && (id.family == QStringLiteral("classic1k") || id.family == QStringLiteral("classic4k")))
        {
            id.kind = CardKind::FM11RF08S;
            id.confidence = Confidence::Suspected;
            id.typeText = QStringLiteral("FM11RF08S (suspected)");
            id.evidence << QStringLiteral("FM11RF08S suspicion: static/weak nonce evidence; backdoor key not probed yet");
        }
        else if(gen3Evidence)
        {
            id.evidence << QStringLiteral("Gen3-like nonce evidence, but magic detection is unavailable; auto Gen3 disabled");
        }

        id.gen3Candidate = magicDetectionAvailable
                           && id.family == QStringLiteral("classic1k")
                           && id.uidBytes == 4
                           && gen3Evidence;
        if(!magicDetectionAvailable)
            id.evidence << QStringLiteral("Magic capability detection unavailable: magic clone not ruled out");
        return id;
    }

    id.kind = CardKind::Unknown;
    id.confidence = Confidence::Unknown;
    id.family = QStringLiteral("unknown");
    id.typeText = QCoreApplication::translate("Gen3Service", "未知卡型");
    id.evidence << QStringLiteral("No matching ATQA/SAK/ATS rule");
    return id;
}

CardIdentity Gen3Service::identifyCard()
{
    const QString output = exec(QStringLiteral("hf 14a info"), 20000);
    if(output.isEmpty())
        return CardIdentity();
    return classifyInfo(output, m_hooks.magicDetectionAvailable);
}

QString Gen3Service::probeBackdoor()
{
    m_probeFile.clear();
    QStringList tried;
    for(int i = 0; i < 4; ++i)
    {
        if(canceled())
            return QString();
        const QString key = QString::fromLatin1(kBackdoorKeys[i]);
        const qint64 start = QDateTime::currentMSecsSinceEpoch();
        exec(QStringLiteral("hf mf isen --collect_fm11rf08s -k ") + key, kIsenTimeoutMs);
        const QString file = newestNonceFile(start);
        if(!file.isEmpty())
        {
            QString error;
            NonceStore::Pm3Evidence evidence;
            if(NonceStore::loadPm3File(file, &evidence, &error))
            {
                m_probeFile = file;
                logLine(QCoreApplication::translate("Gen3Service", "命中后门密钥 %1（证据文件 %2）").arg(key, QFileInfo(file).fileName()));
                return key;
            }
            logLine(QCoreApplication::translate("Gen3Service", "证据文件校验失败：%1").arg(error));
        }
        tried << key;
    }
    logLine(QCoreApplication::translate("Gen3Service", "已尝试的后门密钥：%1").arg(tried.join(QStringLiteral(", "))));
    return QString();
}

bool Gen3Service::loadPm3Evidence(const QString &path, NonceSet *set, QByteArray *dump, bool *hasDump)
{
    QString error;
    NonceStore::Pm3Evidence evidence;
    if(!NonceStore::loadPm3File(path, &evidence, &error))
    {
        logLine(QCoreApplication::translate("Gen3Service", "解析证据失败：%1").arg(error));
        return false;
    }
    *set = evidence.set;
    *dump = evidence.dump;
    *hasDump = evidence.hasDump;
    return true;
}

bool Gen3Service::readOnlyData(const QString &backdoorKey, CrackResult *result)
{
    const qint64 start = QDateTime::currentMSecsSinceEpoch();
    exec(QStringLiteral("hf mf isen --collect_fm11rf08s_with_data -k ") + backdoorKey, kIsenTimeoutMs);
    const QString file = newestNonceFile(start);
    if(file.isEmpty())
    {
        logLine(QCoreApplication::translate("Gen3Service", "后门直读未产生新的证据文件"));
        return false;
    }
    NonceSet ignored;
    QByteArray dump;
    bool hasDump = false;
    if(!loadPm3Evidence(file, &ignored, &dump, &hasDump) || !hasDump)
        return false;

    QByteArray image = dump;
    clearTrailerKeys(&image);
    result->dump = image;
    result->hasDump = true;
    result->onlyData = true;
    result->masked = true;
    result->note = QCoreApplication::translate("Gen3Service", "当前固件/证据不含静态加密 nonce，仅获取到数据；真 KeyA/KeyB 未恢复");
    return true;
}

void Gen3Service::clearTrailerKeys(QByteArray *dump)
{
    if(!dump || dump->size() != 1024)
        return;
    for(int s = 0; s < kSectors; ++s)
    {
        const int base = s * 4 * 16 + 3 * 16;
        for(int i = 0; i < 6; ++i)
            (*dump)[base + i] = '\0';
        for(int i = 10; i < 16; ++i)
            (*dump)[base + i] = '\0';
    }
}

void Gen3Service::backfillTrailerKeys(QByteArray *dump, const CrackResult &result)
{
    if(!dump || dump->size() != 1024)
        return;
    for(int s = 0; s < kSectors; ++s)
    {
        const int base = s * 4 * 16 + 3 * 16;
        bool ok = false;
        const quint64 keyA = NonceStore::parseKey(result.keys[s][0], &ok);
        if(ok)
            for(int i = 0; i < 6; ++i)
                (*dump)[base + i] = char((keyA >> (8 * (5 - i))) & 0xFFu);
        ok = false;
        const quint64 keyB = NonceStore::parseKey(result.keys[s][1], &ok);
        if(ok)
            for(int i = 0; i < 6; ++i)
                (*dump)[base + 10 + i] = char((keyB >> (8 * (5 - i))) & 0xFFu);
    }
}

void Gen3Service::solveAll(const NonceSet &set, CrackResult *result)
{
    int done = 0;
    RecoveryLimits limits;
    limits.maxCandidates = kMaxCandidates;
    const std::function<bool()> cancel = m_hooks.cancel ? m_hooks.cancel : std::function<bool()>();

    for(int s = 0; s < kSectors; ++s)
    {
        for(int k = 0; k < 2; ++k)
        {
            if(canceled())
            {
                result->errorCode = err::kCanceled;
                result->error = QCoreApplication::translate("Gen3Service", "已停止");
                return;
            }
            ++done;
            if(set.nt[s][k] == 0 && set.ntEnc[s][k] == 0)
                continue;

            reportStep(3, 5,
                       QCoreApplication::translate("Gen3Service", "恢复候选 扇区 %1 %2").arg(s).arg(k == 0 ? QStringLiteral("KeyA") : QStringLiteral("KeyB")),
                       5 + (done * 20) / (kSectors * 2));
            bool wasCanceled = false;
            QVector<quint64> list = solveNoncePair(set.uid, set.nt[s][k], set.ntEnc[s][k],
                                                   set.parErr[s][k], limits, cancel, &wasCanceled);
            if(wasCanceled)
            {
                result->errorCode = err::kCanceled;
                result->error = QCoreApplication::translate("Gen3Service", "已停止");
                return;
            }
            if(list.isEmpty() && set.parErr[s][k] != -1)
            {
                logLine(QCoreApplication::translate("Gen3Service", "扇区 %1 %2 的 par_err 过滤后无候选，放宽后重算")
                        .arg(s).arg(k == 0 ? QStringLiteral("A") : QStringLiteral("B")));
                list = solveNoncePair(set.uid, set.nt[s][k], set.ntEnc[s][k], -1, limits, cancel, &wasCanceled);
            }
            m_candidates[s][k] = list;
            result->candidateTotal += list.size();
            logLine(QCoreApplication::translate("Gen3Service", "扇区 %1 %2：候选 %3 个").arg(s)
                    .arg(k == 0 ? QStringLiteral("A") : QStringLiteral("B")).arg(list.size()));
        }
    }
}

QVector<quint64> Gen3Service::orderCandidates(const QVector<quint64> &candidates, quint32 uid) const
{
    Q_UNUSED(uid);
    QVector<quint64> ordered;
    ordered.reserve(candidates.size());
    QSet<quint64> seen;
    QSet<quint64> pool;
    pool.reserve(candidates.size());
    for(quint64 key : candidates)
        pool.insert(key);

    auto take = [&](const QVector<quint64> &source) {
        for(quint64 key : source)
        {
            if(pool.contains(key) && !seen.contains(key))
            {
                seen.insert(key);
                ordered.append(key);
            }
        }
    };
    take(m_hitKeys);
    take(m_cardKeys);
    take(m_libraryKeys);
    for(quint64 key : candidates)
    {
        if(!seen.contains(key))
        {
            seen.insert(key);
            ordered.append(key);
        }
    }
    return ordered;
}

void Gen3Service::rememberHit(quint64 key)
{
    if(key == 0 || m_hitKeys.contains(key))
        return;
    m_hitKeys.prepend(key);
}

QString Gen3Service::verifySector(const QVector<quint64> &ordered, int sector, int keyIndex, FwFlavor flavor)
{
    if(ordered.isEmpty() || canceled())
        return QString();

    QString base = m_hooks.workDir ? m_hooks.workDir() : QString();
    if(base.isEmpty())
        base = QDir::currentPath();
    QDir work(base);
    const QString fileName = QStringLiteral("fm11_sec%1%2.dic")
                             .arg(sector).arg(keyIndex == 0 ? QStringLiteral("A") : QStringLiteral("B"));
    const QString dicPath = work.filePath(fileName);

    QString error;
    if(!NonceStore::writeDicFile(dicPath, ordered, &error))
    {
        logLine(QCoreApplication::translate("Gen3Service", "写入字典失败：%1").arg(error));
        return QString();
    }
    // Debug copy next to the other GUI artefacts (never used by the device).
    QDir().mkpath(tmpDir());
    QFile::copy(dicPath, QDir(tmpDir()).filePath(fileName));

    const int count = ordered.size();
    const int timeoutMs = qMin(kMaxVerifyTimeoutMs, (1000 * count) / 120 + 90000);
    QString output;
    if(flavor == FwFlavor::New)
    {
        output = exec(QStringLiteral("hf mf fchk --1k %1 --blk %2 -f %3")
                      .arg(keyIndex == 0 ? QStringLiteral("-a") : QStringLiteral("-b"))
                      .arg(sector * 4)
                      .arg(fileName), timeoutMs);
    }
    else
    {
        output = exec(QStringLiteral("hf mf chk *1 ? d ") + QDir::toNativeSeparators(dicPath), timeoutMs);
    }
    QFile::remove(dicPath);

    const int block = sector * 4;
    // Level 1: table row "[+]  Sec | Blk | keyA | res | keyB | res"
    auto it = rowRe().globalMatch(output);
    bool sawRow = false;
    while(it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        const int rowBlock = match.captured(1).toInt();
        if(rowBlock != block)
            continue;
        sawRow = true;
        const bool foundA = match.captured(3) == QLatin1String("1");
        const bool foundB = match.captured(5) == QLatin1String("1");
        if(keyIndex == 0 && foundA)
            return match.captured(2).toUpper();
        if(keyIndex == 1 && foundB)
            return match.captured(4).toUpper();
        // A KeyA hit must never stand in for an unverified KeyB: the caller
        // counts only independently verified slots.
    }
    // Level 2: "Key A for block 4 found: XXXXXXXXXXXX"
    auto it2 = keyFoundRe().globalMatch(output);
    while(it2.hasNext())
    {
        const QRegularExpressionMatch match = it2.next();
        if(match.captured(2).toInt() == block)
            return match.captured(3).toUpper();
    }
    // Level 3: last 12-hex token
    QString last;
    auto it3 = hex12Re().globalMatch(output);
    while(it3.hasNext())
        last = it3.next().captured(0).toUpper();
    if(!last.isEmpty() && !sawRow)
        return last;
    return QString();
}

void Gen3Service::verifyCandidates(const NonceSet &set, CrackResult *result, FwFlavor flavor)
{
    int done = 0;
    const int total = kSectors * 2;
    RecoveryLimits limits;
    limits.maxCandidates = kMaxCandidates;
    const std::function<bool()> cancel = m_hooks.cancel ? m_hooks.cancel : std::function<bool()>();

    for(int s = 0; s < kSectors; ++s)
    {
        for(int k = 0; k < 2; ++k)
        {
            if(canceled())
            {
                result->errorCode = err::kCanceled;
                result->error = QCoreApplication::translate("Gen3Service", "已停止");
                return;
            }
            ++done;
            if(m_candidates[s][k].isEmpty())
                continue;

            const QVector<quint64> ordered = orderCandidates(m_candidates[s][k], set.uid);
            reportStep(4, 5,
                       QCoreApplication::translate("Gen3Service", "验真 扇区 %1 %2（候选 %3）")
                       .arg(s).arg(k == 0 ? QStringLiteral("KeyA") : QStringLiteral("KeyB")).arg(ordered.size()),
                       30 + (done * 50) / total);
            QString hit = verifySector(ordered, s, k, flavor);
            if(hit.isEmpty() && set.parErr[s][k] != -1)
            {
                // Safety net: the parity hint may not match this firmware's
                // semantics. One unfiltered retry per group keeps the true key
                // reachable without doubling every sector unconditionally.
                bool wasCanceled = false;
                QVector<quint64> unfiltered = solveNoncePair(set.uid, set.nt[s][k], set.ntEnc[s][k],
                                                             -1, limits, cancel, &wasCanceled);
                if(!unfiltered.isEmpty() && unfiltered.size() != ordered.size())
                {
                    logLine(QCoreApplication::translate("Gen3Service", "扇区 %1 %2 未命中，使用未过滤候选重试（%3 个）")
                            .arg(s).arg(k == 0 ? QStringLiteral("A") : QStringLiteral("B")).arg(unfiltered.size()));
                    hit = verifySector(orderCandidates(unfiltered, set.uid), s, k, flavor);
                }
            }
            if(!hit.isEmpty())
            {
                result->keys[s][k] = hit;
                result->verified[s][k] = true;
                bool ok = false;
                const quint64 key = NonceStore::parseKey(hit, &ok);
                if(ok)
                    rememberHit(key);
                logLine(QCoreApplication::translate("Gen3Service", "命中 扇区 %1 %2 = %3").arg(s)
                        .arg(k == 0 ? QStringLiteral("A") : QStringLiteral("B")).arg(hit));
            }
        }
    }
    result->recoveredKeys = result->recoveredKeyCount();
}

QString Gen3Service::readBlock(int block, const QString &key, FwFlavor flavor)
{
    QString output;
    if(flavor == FwFlavor::New)
        output = exec(QStringLiteral("hf mf rdbl --blk %1 -k %2").arg(block).arg(key), kBlockTimeoutMs);
    else
        output = exec(QStringLiteral("hf mf rdbl %1 A %2").arg(block).arg(key), kBlockTimeoutMs);

    const QRegularExpressionMatch match = blockDataRe().match(output);
    if(!match.hasMatch())
        return QString();
    return match.captured(0).remove(QRegularExpression(QStringLiteral("\\s"))).toUpper();
}

bool Gen3Service::readWholeCard(CrackResult *result, FwFlavor flavor, qint64 sessionStart)
{
    if(result->hasDump && result->dump.size() == 1024)
    {
        backfillTrailerKeys(&result->dump, *result);
        return true;
    }

    // Reuse a fresh dump written by the client (isen/dump) when it belongs to
    // *this* card and was written after the task started.  The file name is the
    // only UID binding available; a time-only match would adopt another card's
    // image and then overwrite its trailer keys.
    QString base = m_hooks.workDir ? m_hooks.workDir() : QString();
    if(base.isEmpty())
        base = QDir::currentPath();
    const QString expectedUid = result->cardUid.trimmed().toUpper();
    const QFileInfoList dumps = QDir(base).entryInfoList(QStringList() << QStringLiteral("hf-mf-*-dump.bin"),
                                                         QDir::Files, QDir::Time);
    for(const QFileInfo &info : dumps)
    {
        if(info.size() != 1024)
            continue;
        if(info.lastModified().toMSecsSinceEpoch() < sessionStart)
            break;              // sorted by time: nothing newer follows
        const QString dumpUid = dumpUidFromFileName(info.fileName());
        if(expectedUid.isEmpty() || dumpUid != expectedUid)
        {
            logLine(QCoreApplication::translate("Gen3Service", "忽略 UID 不匹配的客户端 dump：%1（当前卡片 %2）")
                    .arg(info.fileName(), expectedUid.isEmpty() ? QStringLiteral("-") : expectedUid));
            continue;
        }
        QFile file(info.absoluteFilePath());
        if(!file.open(QIODevice::ReadOnly))
            continue;
        const QByteArray image = file.readAll();
        file.close();
        if(image.size() != 1024)
            continue;
        result->dump = image;
        result->hasDump = true;
        backfillTrailerKeys(&result->dump, *result);
        logLine(QCoreApplication::translate("Gen3Service", "复用客户端 dump：%1").arg(info.fileName()));
        return true;
    }

    QByteArray image(1024, '\0');
    int got = 0;
    const QString fallbackKey = result->backdoorKey;
    for(int s = 0; s < kSectors; ++s)
    {
        if(canceled())
        {
            result->errorCode = err::kCanceled;
            result->error = QCoreApplication::translate("Gen3Service", "已停止");
            return false;
        }
        QString key = result->keys[s][0];
        if(key.isEmpty())
            key = fallbackKey;
        if(key.isEmpty())
            continue;
        for(int b = 0; b < 4; ++b)
        {
            const int block = s * 4 + b;
            const QString text = readBlock(block, key, flavor);
            reportStep(5, 5, QCoreApplication::translate("Gen3Service", "读取块 %1").arg(block), 80 + (block * 18) / 64);
            if(text.size() != 32)
                continue;
            for(int i = 0; i < 16; ++i)
            {
                bool ok = false;
                const uint value = text.mid(i * 2, 2).toUInt(&ok, 16);
                if(!ok)
                    continue;
                image[block * 16 + i] = char(value);
            }
            ++got;
        }
    }

    if(got == 0)
        return false;
    result->dump = image;
    result->hasDump = true;
    result->partial = got < 64;
    backfillTrailerKeys(&result->dump, *result);
    if(result->partial)
        logLine(QCoreApplication::translate("Gen3Service", "只读到 %1/64 块，结果标记为部分成功").arg(got));
    return true;
}

CrackResult Gen3Service::crackGen3(FwFlavor flavor)
{
    const CardIdentity identity = identifyCard();
    return crackGen3WithIdentity(flavor, identity);
}

CrackResult Gen3Service::crackGen3WithIdentity(FwFlavor flavor, const CardIdentity &identity)
{
    CrackResult result;
    QElapsedTimer timer;
    timer.start();
    const qint64 sessionStartMs = QDateTime::currentMSecsSinceEpoch();
    result.flow = QuickFlow::Gen3Crack;
    result.identity = identity;
    result.cardType = identity.typeText;
    result.cardUid = identity.uid;

    m_hitKeys.clear();
    m_cardKeys.clear();
    m_libraryKeys.clear();
    for(int s = 0; s < kSectors; ++s)
        for(int k = 0; k < 2; ++k)
            m_candidates[s][k].clear();

    if(!m_hooks.connected())
    {
        result.errorCode = err::kNotConnected;
        result.error = QCoreApplication::translate("Gen3Service", "PM3 未连接");
        return result;
    }

    if(!identity.detected)
    {
        result.errorCode = err::kNoCard;
        result.error = QCoreApplication::translate("Gen3Service", "没有检测到卡片，请放卡后重试");
        result.elapsedMs = timer.elapsed();
        return result;
    }

    // A magic card must never enter the FM11RF08S backdoor pipeline.  The
    // magic capability block is read-only evidence from `hf 14a info`; when
    // it is present the card is identified only, never probed/cracked.
    if(identity.magic)
    {
        result.errorCode = err::kUnsupported;
        result.error = QCoreApplication::translate("Gen3Service",
                                                   "这是魔术卡（%1），只做只读识别，不执行后门探测/破解/写卡").arg(identity.typeText);
        result.note = QCoreApplication::translate("Gen3Service",
                                                  "魔术卡不进入 FM11RF08S 流程；未发送解锁、写 UID、写块或擦除命令");
        result.elapsedMs = timer.elapsed();
        return result;
    }

    if(identity.isClassicFamily() == false)
    {
        result.errorCode = err::kUnsupported;
        result.error = QCoreApplication::translate("Gen3Service",
                                                   "%1：当前只读取卡片公开信息，不做 FM11RF08S 破解").arg(identity.typeText);
        result.flow = QuickFlow::PublicInfoOnly;
        result.elapsedMs = timer.elapsed();
        return result;
    }

    // The FM11RF08S evidence/read pipeline is fixed to the 1K layout (16
    // sectors, 1024 bytes).  A 4K/Mini card must not be fed into it.
    if(identity.family != QLatin1String("classic1k"))
    {
        result.errorCode = err::kUnsupported;
        result.error = QCoreApplication::translate("Gen3Service",
                                                   "%1：三代流程仅支持 Classic 1K / FM11RF08S（16 扇区）").arg(identity.typeText);
        result.flow = QuickFlow::PublicInfoOnly;
        result.elapsedMs = timer.elapsed();
        return result;
    }

    reportStep(1, 5, QCoreApplication::translate("Gen3Service", "探测后门密钥"), 1);
    const QString backdoorKey = probeBackdoor();
    if(backdoorKey.isEmpty())
    {
        // The read-only identity said "candidate"; the probe now rules out the
        // Fudan backdoor, so show the underlying Classic family instead of a
        // false FM11RF08S label.
        result.identity.kind = identity.family == QStringLiteral("classic4k") ? CardKind::Classic4K
                               : identity.family == QStringLiteral("classicmini") ? CardKind::ClassicMini
                               : CardKind::Classic1K;
        result.identity.confidence = Confidence::Confirmed;
        result.identity.typeText = identity.family == QStringLiteral("classic4k") ? QStringLiteral("MIFARE Classic 4K")
                                   : identity.family == QStringLiteral("classicmini") ? QStringLiteral("MIFARE Mini")
                                   : QStringLiteral("MIFARE Classic 1K");
        result.identity.gen3Candidate = false;
        result.identity.evidence << QStringLiteral("Backdoor probe: 0/4 Fudan keys matched; FM11RF08S ruled out");
        result.errorCode = err::kNotGen3;
        result.error = QCoreApplication::translate("Gen3Service", "未命中任何后门密钥，该卡可能不是 FM11RF08S 三代卡");
        result.elapsedMs = timer.elapsed();
        return result;
    }
    result.backdoorKey = backdoorKey;
    result.identity.kind = CardKind::FM11RF08S;
    result.identity.confidence = Confidence::Confirmed;
    result.identity.typeText = QStringLiteral("FM11RF08S");
    result.identity.magic = false;
    result.identity.gen3Candidate = true;
    result.identity.evidence << QStringLiteral("Backdoor key %1 matched (FM11RF08S confirmed)").arg(backdoorKey);
    reportStep(1, 5, QCoreApplication::translate("Gen3Service", "命中后门密钥 %1").arg(backdoorKey), 5);

    NonceSet set;
    QByteArray preImage;
    bool hasPreImage = false;
    if(!loadPm3Evidence(m_probeFile, &set, &preImage, &hasPreImage))
    {
        if(readOnlyData(backdoorKey, &result))
        {
            result.elapsedMs = timer.elapsed();
            return result;
        }
        result.errorCode = err::kNoEvidence;
        result.error = QCoreApplication::translate("Gen3Service", "后门密钥已命中，但采集不到有效的静态加密 nonce，也未读到数据块");
        result.elapsedMs = timer.elapsed();
        return result;
    }
    set.backdoorKey = backdoorKey;
    result.nonces = set;
    result.hasNonces = true;
    if(hasPreImage && preImage.size() == 1024)
    {
        QByteArray masked = preImage;
        clearTrailerKeys(&masked);
        result.dump = masked;
        result.hasDump = true;
        result.onlyData = true;
        result.masked = true;
        result.note = QCoreApplication::translate("Gen3Service", "后门已读出数据块，正在恢复真密钥…");
    }

    reportStep(2, 5, QCoreApplication::translate("Gen3Service", "采集静态加密 nonce"), 10);
    logLine(QCoreApplication::translate("Gen3Service", "证据：%1 对 nonce（UID %2）")
            .arg(set.validPairs()).arg(QStringLiteral("%1").arg(set.uid, 8, 16, QLatin1Char('0')).toUpper()));

    reportStep(3, 5, QCoreApplication::translate("Gen3Service", "本地恢复候选密钥"), 12);
    solveAll(set, &result);
    if(!result.errorCode.isEmpty())
    {
        result.elapsedMs = timer.elapsed();
        return result;
    }
    if(result.candidateTotal == 0)
    {
        result.errorCode = err::kNoCandidates;
        result.error = QCoreApplication::translate("Gen3Service", "没有算出任何候选密钥，请检查证据文件中的 nt/nt_enc 是否成对、UID 是否匹配");
        result.elapsedMs = timer.elapsed();
        return result;
    }
    logLine(QCoreApplication::translate("Gen3Service", "候选合计 %1 个，开始上卡验真").arg(result.candidateTotal));

    // Seed the priority list with the card-specific cache and the key library.
    NonceStore::loadKeyBin(cardKeyFilePath(set.uid), result.keys);
    for(int s = 0; s < kSectors; ++s)
    {
        for(int k = 0; k < 2; ++k)
        {
            bool ok = false;
            const quint64 key = NonceStore::parseKey(result.keys[s][k], &ok);
            if(ok)
                m_cardKeys.append(key);
            result.keys[s][k].clear();
        }
    }
    m_libraryKeys = NonceStore::readKeyList(QDir(cardKeysDir()).filePath(QStringLiteral("known_keys.txt")));

    reportStep(4, 5, QCoreApplication::translate("Gen3Service", "上卡验真密钥"), 30);
    verifyCandidates(set, &result, flavor);
    if(!result.errorCode.isEmpty())
    {
        result.elapsedMs = timer.elapsed();
        return result;
    }
    const int recovered = result.recoveredKeyCount();
    result.recoveredKeys = recovered;
    logLine(QCoreApplication::translate("Gen3Service", "验真完成：%1/32 把密钥").arg(recovered));

    reportStep(5, 5, QCoreApplication::translate("Gen3Service", "读取全卡数据"), 80);
    const bool readOk = readWholeCard(&result, flavor, sessionStartMs);

    if(recovered > 0)
    {
        // Persist the recovered keys for the fast path of the next run.
        QDir().mkpath(keysDir());
        const QByteArray image = NonceStore::buildKeyBin(result);
        QFile keyFile(cardKeyFilePath(set.uid));
        if(keyFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            keyFile.write(image);
            keyFile.close();
        }
    }

    if(!readOk)
    {
        result.errorCode = recovered > 0 ? err::kReadFailed : err::kVerifyMissed;
        result.error = recovered > 0
                       ? QCoreApplication::translate("Gen3Service", "真密钥已恢复，但读块失败")
                       : QCoreApplication::translate("Gen3Service", "验真全部未命中：请核对 UID、字典是否下发到客户端目录，或卡片是否贴合");
        result.elapsedMs = timer.elapsed();
        return result;
    }

    result.onlyData = recovered == 0;
    result.masked = recovered == 0;
    result.keySource = recovered > 0 ? QStringLiteral("backdoor") : QString();
    if(result.partial)
        result.note = QCoreApplication::translate("Gen3Service", "部分成功：密钥已恢复，但有个别块未能读取");
    else if(recovered == 0)
        result.note = QCoreApplication::translate("Gen3Service", "仅获取到数据，真密钥未恢复");
    else
        result.note = QCoreApplication::translate("Gen3Service", "破解成功：真密钥与整卡数据已就绪");
    result.elapsedMs = timer.elapsed();
    return result;
}

CrackResult Gen3Service::verifyEvidence(const NonceSet &set, FwFlavor flavor)
{
    CrackResult result;
    QElapsedTimer timer;
    timer.start();
    const qint64 sessionStartMs = QDateTime::currentMSecsSinceEpoch();
    result.flow = QuickFlow::EvidenceVerify;

    if(!m_hooks.connected())
    {
        result.errorCode = err::kNotConnected;
        result.error = QCoreApplication::translate("Gen3Service", "PM3 未连接");
        return result;
    }
    const QString invalid = NonceStore::validateNonceSet(set);
    if(!invalid.isEmpty())
    {
        result.errorCode = err::kNoEvidence;
        result.error = invalid;
        return result;
    }

    result.identity.detected = set.uid != 0;
    result.identity.uid = QStringLiteral("%1").arg(set.uid, 8, 16, QLatin1Char('0')).toUpper();
    result.identity.kind = CardKind::FM11RF08S;
    result.identity.confidence = Confidence::Suspected;
    result.identity.typeText = QStringLiteral("FM11RF08S (imported evidence)");
    result.identity.classic = true;
    result.identity.family = QStringLiteral("classic1k");
    result.identity.sectors = kSectors;
    result.identity.evidence << QStringLiteral("Imported nonce evidence for UID %1").arg(result.identity.uid);
    if(!result.identity.uid.isEmpty())
    {
        result.cardUid = result.identity.uid;
        result.cardType = result.identity.typeText;
    }

    m_hitKeys.clear();
    m_cardKeys.clear();
    m_libraryKeys.clear();
    for(int s = 0; s < kSectors; ++s)
        for(int k = 0; k < 2; ++k)
            m_candidates[s][k].clear();

    result.backdoorKey = set.backdoorKey;
    result.nonces = set;
    result.hasNonces = true;
    reportStep(1, 3, QCoreApplication::translate("Gen3Service", "恢复候选密钥"), 5);
    solveAll(set, &result);
    if(!result.errorCode.isEmpty())
    {
        result.elapsedMs = timer.elapsed();
        return result;
    }
    if(result.candidateTotal == 0)
    {
        result.errorCode = err::kNoCandidates;
        result.error = QCoreApplication::translate("Gen3Service", "候选密钥为 0，证据里的 nt/nt_enc 可能不配对");
        result.elapsedMs = timer.elapsed();
        return result;
    }

    NonceStore::loadKeyBin(cardKeyFilePath(set.uid), result.keys);
    for(int s = 0; s < kSectors; ++s)
    {
        for(int k = 0; k < 2; ++k)
        {
            bool ok = false;
            const quint64 key = NonceStore::parseKey(result.keys[s][k], &ok);
            if(ok)
                m_cardKeys.append(key);
            result.keys[s][k].clear();
        }
    }
    m_libraryKeys = NonceStore::readKeyList(QDir(cardKeysDir()).filePath(QStringLiteral("known_keys.txt")));

    reportStep(2, 3, QCoreApplication::translate("Gen3Service", "上卡验真密钥"), 30);
    verifyCandidates(set, &result, flavor);
    if(!result.errorCode.isEmpty())
    {
        result.elapsedMs = timer.elapsed();
        return result;
    }
    result.recoveredKeys = result.recoveredKeyCount();

    reportStep(3, 3, QCoreApplication::translate("Gen3Service", "读取全卡数据"), 80);
    const bool readOk = readWholeCard(&result, flavor, sessionStartMs);
    if(!readOk)
    {
        result.errorCode = err::kVerifyMissed;
        result.error = QCoreApplication::translate("Gen3Service", "验真未命中或读块失败");
        result.elapsedMs = timer.elapsed();
        return result;
    }
    result.keySource = QStringLiteral("import");
    result.note = QCoreApplication::translate("Gen3Service", "候选验证完成");
    result.elapsedMs = timer.elapsed();
    return result;
}

bool Gen3Service::readWithKnownKey(quint64 key, CrackResult *result, FwFlavor flavor)
{
    const QString keyText = NonceStore::formatKey(key);
    QByteArray image(1024, '\0');
    int got = 0;
    bool keyAWorked[kSectors] = {};
    bool keyBWorked[kSectors] = {};
    for(int s = 0; s < kSectors; ++s)
    {
        if(canceled())
        {
            result->errorCode = err::kCanceled;
            result->error = QCoreApplication::translate("Gen3Service", "已停止");
            return false;
        }
        QString sectorKey = keyText;
        for(int b = 0; b < 4; ++b)
        {
            const int block = s * 4 + b;
            bool usedKeyB = false;
            QString text = readBlock(block, sectorKey, flavor);
            if(text.size() != 32 && b == 0)
            {
                // KeyA failed: retry the sector header with KeyB before giving up.
                if(flavor == FwFlavor::New)
                {
                    const QString output = exec(QStringLiteral("hf mf rdbl --blk %1 -b -k %2").arg(block).arg(sectorKey),
                                                kBlockTimeoutMs);
                    const QRegularExpressionMatch match = blockDataRe().match(output);
                    if(match.hasMatch())
                    {
                        text = match.captured(0).remove(QRegularExpression(QStringLiteral("\\s"))).toUpper();
                        usedKeyB = true;
                    }
                }
            }
            reportStep(2, 3, QCoreApplication::translate("Gen3Service", "读取块 %1").arg(block), 10 + (block * 60) / 64);
            if(text.size() != 32)
                continue;
            if(b == 0)
            {
                // Record which key actually opened the sector instead of
                // publishing the same value as both KeyA and KeyB.
                if(usedKeyB)
                    keyBWorked[s] = true;
                else
                    keyAWorked[s] = true;
            }
            for(int i = 0; i < 16; ++i)
            {
                bool ok = false;
                const uint value = text.mid(i * 2, 2).toUInt(&ok, 16);
                if(!ok)
                    continue;
                image[block * 16 + i] = char(value);
            }
            ++got;
        }
    }
    if(got == 0)
        return false;
    result->dump = image;
    result->hasDump = true;
    result->partial = got < 64;
    for(int s = 0; s < kSectors; ++s)
    {
        if(keyAWorked[s])
        {
            result->keys[s][0] = keyText;
            result->verified[s][0] = true;
        }
        if(keyBWorked[s])
        {
            result->keys[s][1] = keyText;
            result->verified[s][1] = true;
        }
    }
    backfillTrailerKeys(&result->dump, *result);
    return true;
}

CrackResult Gen3Service::quickRead(FwFlavor flavor)
{
    CrackResult result;
    QElapsedTimer timer;
    timer.start();

    if(!m_hooks.connected())
    {
        result.errorCode = err::kNotConnected;
        result.error = QCoreApplication::translate("Gen3Service", "PM3 未连接");
        return result;
    }

    reportStep(1, 3, QCoreApplication::translate("Gen3Service", "识别卡片"), 1);
    const CardIdentity identity = identifyCard();
    result.identity = identity;
    result.cardType = identity.typeText;
    result.cardUid = identity.uid;
    if(!identity.detected)
    {
        result.errorCode = err::kNoCard;
        result.error = QCoreApplication::translate("Gen3Service", "没有检测到卡片，请放卡后重试");
        result.elapsedMs = timer.elapsed();
        return result;
    }
    logLine(QStringLiteral("Identity: %1 | confidence=%2 | %3")
            .arg(identity.typeText)
            .arg(identity.confidence == Confidence::Confirmed ? QStringLiteral("confirmed")
                 : identity.confidence == Confidence::Suspected ? QStringLiteral("suspected")
                 : QStringLiteral("unknown"))
            .arg(identity.evidence.join(QStringLiteral("; "))));

    // ---- Magic cards: read-only identification only ------------------------
    if(identity.magic)
    {
        result.flow = QuickFlow::IdentificationOnly;
        result.errorCode = err::kUnsupported;
        result.error = QCoreApplication::translate("Gen3Service", "%1：魔术卡只做只读识别，不读整卡").arg(identity.typeText);
        result.note = QCoreApplication::translate("Gen3Service",
                                                  "显示 UID/ATQA/SAK/扇区；未发送密钥试探、后门探测、解锁、写 UID、写块或擦除命令");
        result.elapsedMs = timer.elapsed();
        return result;
    }

    if(identity.family == QLatin1String("classic1k"))
    {
        // 1) Known-key fast path: keys/<UID>.bin, then the default key set.
        NonceStore::loadKeyBin(cardKeyFilePath(identity.uidValue()), result.keys);
        QVector<quint64> cachedKeys;
        QVector<quint64> tryKeys;
        for(int s = 0; s < kSectors; ++s)
        {
            for(int k = 0; k < 2; ++k)
            {
                bool ok = false;
                const quint64 key = NonceStore::parseKey(result.keys[s][k], &ok);
                result.keys[s][k].clear();
                if(!ok)
                    continue;
                if(!cachedKeys.contains(key))
                    cachedKeys.append(key);
                if(!tryKeys.contains(key))
                    tryKeys.append(key);
            }
        }
        for(quint64 key : NonceStore::defaultKeys())
        {
            if(!tryKeys.contains(key))
                tryKeys.append(key);
        }

        reportStep(2, 3, QCoreApplication::translate("Gen3Service", "尝试已知密钥"), 8);
        quint64 working = 0;
        for(quint64 key : tryKeys)
        {
            if(canceled())
                break;
            const QString text = readBlock(0, NonceStore::formatKey(key), flavor);
            if(text.size() == 32)
            {
                working = key;
                break;
            }
        }
        if(working != 0)
        {
            result.flow = QuickFlow::ClassicKnownKey;
            result.keySource = cachedKeys.contains(working) ? QStringLiteral("keys-file")
                                                            : QStringLiteral("dictionary");
            reportStep(3, 3, QCoreApplication::translate("Gen3Service", "使用已知密钥读全卡"), 20);
            if(readWithKnownKey(working, &result, flavor))
            {
                result.note = QCoreApplication::translate("Gen3Service", "使用已知密钥 %1 读取完成").arg(NonceStore::formatKey(working));
                result.elapsedMs = timer.elapsed();
                return result;
            }
            if(!result.errorCode.isEmpty())
            {
                result.elapsedMs = timer.elapsed();
                return result;
            }
        }
        if(canceled())
        {
            result.errorCode = err::kCanceled;
            result.error = QCoreApplication::translate("Gen3Service", "已停止");
            result.elapsedMs = timer.elapsed();
            return result;
        }

        // 2) No usable key.  The Gen3 backdoor pipeline is started only when
        //    the read-only evidence qualifies (Classic 1K + 4-byte UID + no
        //    magic flag + static/weak nonce evidence) and the client can
        //    actually report magic capabilities.
        if(identity.gen3Candidate)
        {
            logLine(QCoreApplication::translate("Gen3Service", "已知密钥不可用；只读证据符合 FM11RF08S 候选，进入三代流程"));
            CrackResult gen3 = crackGen3WithIdentity(flavor, identity);
            gen3.cardType = identity.typeText;
            gen3.cardUid = identity.uid;
            if(!gen3.success() && gen3.errorCode == err::kNotGen3)
            {
                gen3.error = QCoreApplication::translate("Gen3Service", "该卡不是可自动破解的 FM11RF08S，且没有可用密钥；"
                                     "请在高级功能区用字典/嵌套等方式获取密钥");
            }
            return gen3;
        }

        result.flow = QuickFlow::ClassicKnownKey;
        result.errorCode = err::kUnsupported;
        result.error = QCoreApplication::translate("Gen3Service", "没有可用密钥；只读证据不足以判定 FM11RF08S，未启动后门探测");
        result.note = QCoreApplication::translate("Gen3Service", "如需尝试三代流程，请点击“Crack FM11RF08S”；否则使用高级功能区字典/嵌套/硬嵌套");
        result.elapsedMs = timer.elapsed();
        return result;
    }

    // Classic 4K / Mini / Ultralight / NTAG / DESFire / CPU / unknown: keep the
    // existing safe branches (public information only).
    result.flow = QuickFlow::PublicInfoOnly;
    result.errorCode = err::kUnsupported;
    result.error = QCoreApplication::translate("Gen3Service", "%1：当前只读取卡片公开信息，不做整卡读取").arg(identity.typeText);
    result.note = QCoreApplication::translate("Gen3Service", "UID/ATQA/SAK 已显示；如需操作请使用下方高级功能区");
    result.elapsedMs = timer.elapsed();
    return result;
}

} // namespace mfgen3
