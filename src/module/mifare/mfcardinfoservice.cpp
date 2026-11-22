#include "mfcardinfoservice.h"

#include "mfgen3service.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace mfgen3
{

namespace
{
constexpr int kInfoTimeoutMs = 20000;
constexpr int kMfInfoTimeoutMs = 20000;

QString lineValue(const QString &output, const QString &label)
{
    const QRegularExpression re(QStringLiteral("(?:^|\\n)\\s*(?:\\[.\\]\\s*)?%1\\s*[:.]*\\s*([^\\r\\n]+)")
                                .arg(QRegularExpression::escape(label)));
    const QRegularExpressionMatch match = re.match(output);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}
} // namespace

CardInfoService::CardInfoService(const CardInfoHooks &hooks)
    : m_hooks(hooks)
{
}

void CardInfoService::log(const QString &text)
{
    if (m_hooks.log && !text.isEmpty())
        m_hooks.log(text);
}

CardIdentity CardInfoService::read()
{
    CardIdentity identity;

    if (!m_hooks.isConnected || !m_hooks.isConnected())
    {
        log(QCoreApplication::translate("CardInfoService", "PM3 is not connected"));
        return identity;
    }
    if (m_hooks.canceled && m_hooks.canceled())
        return identity;

    const QString infoCommand = cardInfoCommand();
    m_commands << infoCommand;
    m_infoOutput = m_hooks.exec ? m_hooks.exec(infoCommand, kInfoTimeoutMs) : QString();
    if(m_hooks.lastExecSucceeded && !m_hooks.lastExecSucceeded())
    {
        log(QCoreApplication::translate("CardInfoService", "hf 14a info failed; stopping"));
        return identity;
    }

    if (m_hooks.canceled && m_hooks.canceled())
    {
        log(QCoreApplication::translate("CardInfoService", "Card info canceled"));
        return identity;
    }
    if (m_infoOutput.isEmpty())
    {
        log(QCoreApplication::translate("CardInfoService", "hf 14a info failed or returned no output"));
        return identity;
    }

    identity = Gen3Service::classifyInfo(m_infoOutput, m_hooks.magicDetectionAvailable);
    if (!identity.detected)
    {
        log(QCoreApplication::translate("CardInfoService", "hf 14a info found no card"));
        return identity;
    }

    // `hf mf info` only makes sense for a MIFARE Classic layout.  Ultralight /
    // NTAG, DESFire, CPU and unknown cards keep the single `hf 14a info`
    // result instead of being probed blindly.
    if (!identity.classic)
    {
        log(QCoreApplication::translate("CardInfoService",
                                        "hf mf info skipped: card is not a MIFARE Classic layout"));
        return identity;
    }

    if (m_hooks.canceled && m_hooks.canceled())
    {
        log(QCoreApplication::translate("CardInfoService",
                                        "Card info canceled; hf mf info skipped"));
        return identity;
    }
    if (!m_hooks.isConnected || !m_hooks.isConnected())
    {
        log(QCoreApplication::translate("CardInfoService",
                                        "hf mf info skipped: PM3 disconnected"));
        return identity;
    }

    const QString mfCommand = mifareInfoCommand();
    m_commands << mfCommand;
    m_mfInfoOutput = m_hooks.exec ? m_hooks.exec(mfCommand, kMfInfoTimeoutMs) : QString();
    if(m_hooks.lastExecSucceeded && !m_hooks.lastExecSucceeded())
    {
        log(QCoreApplication::translate("CardInfoService",
                                        "hf mf info failed; keeping the hf 14a info result"));
        return identity;
    }

    if (m_hooks.canceled && m_hooks.canceled())
    {
        log(QCoreApplication::translate("CardInfoService",
                                        "hf mf info canceled; keeping the hf 14a info result"));
        return identity;
    }
    if (m_mfInfoOutput.isEmpty())
    {
        log(QCoreApplication::translate("CardInfoService",
                                        "hf mf info failed; keeping the hf 14a info result"));
        return identity;
    }

    refineFromMfInfo(&identity, m_mfInfoOutput);
    return identity;
}

void CardInfoService::refineFromMfInfo(CardIdentity *identity, const QString &output)
{
    if (!identity || output.isEmpty())
        return;

    const QString lower = output.toLower();

    // If the second command sees a different UID, the card was swapped between
    // the two probes; do not merge the two cards' data.
    const QRegularExpression uidRe(QStringLiteral("UID:\\s*([0-9A-Fa-f ]+)"));
    const QRegularExpressionMatch uidMatch = uidRe.match(output);
    if(uidMatch.hasMatch())
    {
        QString uid = uidMatch.captured(1);
        uid.remove(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")));
        if(!identity->uid.isEmpty() && uid.compare(identity->uid, Qt::CaseInsensitive) != 0)
        {
            const QString firstUid = identity->uid;
            *identity = CardIdentity();
            identity->evidence << QStringLiteral("hf mf info UID %1 differs from hf 14a info UID %2; identity cleared")
                                  .arg(uid.toUpper(), firstUid);
            return;
        }
    }

    // Explicit fingerprint: the firmware/backdoor probe itself reports the
    // Fudan part.  This is stronger than the static/weak nonce hint and is the
    // only place where hf mf info may promote the identity to Confirmed.
    const QRegularExpression fingerprintRe(
                QStringLiteral("Fingerprint[\\s\\S]{0,200}?FM11RF08S"),
                QRegularExpression::CaseInsensitiveOption);
    if (lower.contains(QStringLiteral("fudan fm11rf08s"))
            || fingerprintRe.match(output).hasMatch())
    {
        identity->kind = CardKind::FM11RF08S;
        identity->confidence = Confidence::Confirmed;
        identity->typeText = QStringLiteral("FM11RF08S");
        identity->family = QStringLiteral("classic1k");
        identity->classic = true;
        identity->magic = false;
        identity->sectors = kSectors;
        identity->capacity = 1024;
        identity->gen3Candidate = identity->uidBytes == 4;
        identity->evidence << QStringLiteral("hf mf info fingerprint: Fudan FM11RF08S");
    }

    const QRegularExpression backdoorRe(
                QStringLiteral("Backdoor key[.\\s:]*([0-9A-Fa-f]{12})"));
    const QRegularExpressionMatch backdoor = backdoorRe.match(output);
    if (backdoor.hasMatch())
        identity->evidence << QStringLiteral("hf mf info backdoor key: %1").arg(backdoor.captured(1).toUpper());

    if (lower.contains(QStringLiteral("static enc nonce")) && lower.contains(QStringLiteral("yes")))
    {
        identity->staticNonce = true;
        identity->evidence << QStringLiteral("hf mf info static encrypted nonce: yes");
    }

    const QString prng = lineValue(output, QStringLiteral("Prng"));
    if (!prng.isEmpty())
    {
        if (prng.contains(QStringLiteral("weak"), Qt::CaseInsensitive))
        {
            identity->prng = 1;
            identity->evidence << QStringLiteral("hf mf info PRNG: weak");
        }
        else if (prng.contains(QStringLiteral("hard"), Qt::CaseInsensitive))
        {
            identity->prng = 0;
            identity->evidence << QStringLiteral("hf mf info PRNG: hard");
        }
    }

    // Some firmware revisions print an explicit type line.  Only accept a
    // known MIFARE Classic variant and keep the SAK-derived layout otherwise.
    // The Fudan fingerprint above is stronger than a generic type line.
    const QString type = lineValue(output, QStringLiteral("TYPE"));
    if (!type.isEmpty() && identity->kind != CardKind::FM11RF08S)
    {
        const QString typeLower = type.toLower();
        if (typeLower.contains(QStringLiteral("classic 4k")))
        {
            identity->kind = CardKind::Classic4K;
            identity->family = QStringLiteral("classic4k");
            identity->classic = true;
            identity->sectors = 40;
            identity->capacity = 4096;
            identity->typeText = QStringLiteral("MIFARE Classic 4K");
        }
        else if (typeLower.contains(QStringLiteral("classic 1k")))
        {
            identity->kind = CardKind::Classic1K;
            identity->family = QStringLiteral("classic1k");
            identity->classic = true;
            identity->sectors = 16;
            identity->capacity = 1024;
            identity->typeText = QStringLiteral("MIFARE Classic 1K");
        }
        else if (typeLower.contains(QStringLiteral("mini")))
        {
            identity->kind = CardKind::ClassicMini;
            identity->family = QStringLiteral("classicmini");
            identity->classic = true;
            identity->sectors = 5;
            identity->capacity = 320;
            identity->typeText = QStringLiteral("MIFARE Mini");
        }
        if (identity->classic)
            identity->evidence << QStringLiteral("hf mf info type: %1").arg(type);
    }
}

} // namespace mfgen3
