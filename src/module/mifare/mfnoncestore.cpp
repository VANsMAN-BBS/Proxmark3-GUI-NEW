#include "mfnoncestore.h"
#include "mfgen3crypto.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

namespace mfgen3
{
namespace NonceStore
{

namespace
{
const char *kPm3GuiMagic = "PM3GUI_FM11_NONCES_V1";
const char *kPm3FileType = "fm11rf08s_nonces";

QString jsonError(const QJsonParseError &err)
{
    return QStringLiteral("json: %1 at offset %2").arg(err.errorString()).arg(err.offset);
}

QString slotName(int sector, int keyIndex)
{
    return QStringLiteral("%1.%2").arg(sector).arg(keyIndex == 0 ? QStringLiteral("a") : QStringLiteral("b"));
}

enum class HexResult
{
    Absent,
    Ok,
    Invalid,
};

HexResult parseHex32(const QString &text, quint32 *out, QString *error, const QString &field)
{
    const QString trimmed = text.trimmed();
    if(trimmed.isEmpty())
        return HexResult::Absent;
    bool ok = false;
    const quint32 value = trimmed.toUInt(&ok, 16);
    if(!ok || trimmed.size() > 8)
    {
        if(error)
            *error = QStringLiteral("%1: expected 8 hex digits, got \"%2\"").arg(field, trimmed);
        return HexResult::Invalid;
    }
    *out = value;
    return HexResult::Ok;
}

} // namespace

QString detectFormat(const QString &path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return QStringLiteral("unknown");
    const QByteArray head = file.read(4096);
    if(head.contains(kPm3GuiMagic))
        return QStringLiteral("pm3gui");
    if(head.contains(kPm3FileType))
        return QStringLiteral("pm3");
    return QStringLiteral("unknown");
}

bool loadPm3File(const QString &path, Pm3Evidence *out, QString *error)
{
    if(!out)
        return false;
    *out = Pm3Evidence();

    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error)
            *error = QStringLiteral("file: cannot open %1").arg(path);
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if(parseError.error != QJsonParseError::NoError)
    {
        if(error)
            *error = jsonError(parseError);
        return false;
    }
    if(!doc.isObject())
    {
        if(error)
            *error = QStringLiteral("root: expected a JSON object");
        return false;
    }
    const QJsonObject root = doc.object();

    const QString fileType = root.value(QStringLiteral("FileType")).toString();
    if(!fileType.startsWith(QLatin1String(kPm3FileType)))
    {
        if(error)
            *error = QStringLiteral("FileType: expected prefix \"%1\", got \"%2\"")
                     .arg(QLatin1String(kPm3FileType), fileType);
        return false;
    }

    // UID comes from the file name: hf-mf-<UID>-nonces*.json (case tolerant,
    // the fixed segments are lower case in every client version we support).
    const QString name = QFileInfo(path).fileName();
    static const QRegularExpression nameRe(
        QStringLiteral("^hf-mf-([0-9A-Fa-f]+)-nonces.*\\.json$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch nameMatch = nameRe.match(name);
    if(!nameMatch.hasMatch())
    {
        if(error)
            *error = QStringLiteral("filename: expected hf-mf-<UID>-nonces*.json, got \"%1\"").arg(name);
        return false;
    }
    const QString uidText = nameMatch.captured(1);
    bool uidOk = false;
    out->set.uid = uidText.toUInt(&uidOk, 16);
    if(!uidOk)
    {
        if(error)
            *error = QStringLiteral("filename UID: \"%1\" is not hexadecimal").arg(uidText);
        return false;
    }

    const QJsonValue ntValue = root.value(QStringLiteral("nt"));
    const QJsonValue ntEncValue = root.value(QStringLiteral("nt_enc"));
    if(!ntValue.isObject() || !ntEncValue.isObject())
    {
        if(error)
            *error = QStringLiteral("nt/nt_enc: both must be objects keyed by sector number");
        return false;
    }
    const QJsonObject ntObj = ntValue.toObject();
    const QJsonObject ntEncObj = ntEncValue.toObject();
    const QJsonObject parObj = root.value(QStringLiteral("par_err")).toObject();

    int usable = 0;
    for(int s = 0; s < kSectors; ++s)
    {
        const QString sectorKey = QString::number(s);
        const QJsonObject ntSector = ntObj.value(sectorKey).toObject();
        const QJsonObject encSector = ntEncObj.value(sectorKey).toObject();
        const QJsonObject parSector = parObj.value(sectorKey).toObject();
        for(int k = 0; k < 2; ++k)
        {
            const QString side = k == 0 ? QStringLiteral("a") : QStringLiteral("b");
            quint32 ntValue32 = 0;
            quint32 encValue32 = 0;
            const QString ntField = QStringLiteral("nt.%1").arg(slotName(s, k));
            const QString encField = QStringLiteral("nt_enc.%1").arg(slotName(s, k));
            const HexResult resultNt = parseHex32(ntSector.value(side).toString(), &ntValue32, error, ntField);
            const HexResult resultEnc = parseHex32(encSector.value(side).toString(), &encValue32, error, encField);
            if(resultNt == HexResult::Invalid || resultEnc == HexResult::Invalid)
                return false;
            const bool hasNt = resultNt == HexResult::Ok;
            const bool hasEnc = resultEnc == HexResult::Ok;
            out->set.nt[s][k] = hasNt ? ntValue32 : 0;
            out->set.ntEnc[s][k] = hasEnc ? encValue32 : 0;
            out->set.parErr[s][k] = -1;

            const QString parText = parSector.value(side).toString().trimmed();
            if(parText.size() >= 4)
            {
                const QChar last = parText.at(parText.size() - 1);
                if(last == QLatin1Char('0') || last == QLatin1Char('1'))
                {
                    // Reference correction (x1.h.W0): last bit XOR (even parity of
                    // the low ciphertext byte XOR 1); applied only for 4+ char fields.
                    const int lastBit = last.unicode() - '0';
                    const int parity = int(evenParity32(quint32(out->set.ntEnc[s][k] & 0xFFu)));
                    out->set.parErr[s][k] = qint8((lastBit ^ (parity ^ 1)) & 1);
                }
                else if(error)
                {
                    *error = QStringLiteral("par_err.%1: expected a trailing '0' or '1', got \"%2\"")
                             .arg(slotName(s, k), parText);
                    return false;
                }
            }
            if(out->set.nt[s][k] != 0 || out->set.ntEnc[s][k] != 0)
                ++usable;
        }
    }
    if(usable < 16)
    {
        if(error)
            *error = QStringLiteral("nt/nt_enc: only %1 usable nonce pairs, at least 16 required").arg(usable);
        return false;
    }

    if(root.contains(QStringLiteral("blocks")))
    {
        const QJsonObject blocksObj = root.value(QStringLiteral("blocks")).toObject();
        QByteArray image(1024, '\0');
        int parsed = 0;
        for(auto it = blocksObj.constBegin(); it != blocksObj.constEnd(); ++it)
        {
            bool blockOk = false;
            const int block = it.key().toInt(&blockOk);
            if(!blockOk || block < 0 || block >= 64)
            {
                if(error)
                    *error = QStringLiteral("blocks: invalid block index \"%1\"").arg(it.key());
                return false;
            }
            const QString hex = it.value().toString().trimmed();
            if(hex.size() != 32)
            {
                if(error)
                    *error = QStringLiteral("blocks.%1: expected 32 hex digits, got %2")
                             .arg(block).arg(hex.size());
                return false;
            }
            bool hexOk = true;
            for(int i = 0; i < 16; ++i)
            {
                bool byteOk = false;
                const uint value = hex.mid(i * 2, 2).toUInt(&byteOk, 16);
                if(!byteOk)
                {
                    hexOk = false;
                    break;
                }
                image[block * 16 + i] = char(value);
            }
            if(!hexOk)
            {
                if(error)
                    *error = QStringLiteral("blocks.%1: not hexadecimal").arg(block);
                return false;
            }
            ++parsed;
        }
        if(parsed == 64)
        {
            out->dump = image;
            out->hasDump = true;
        }
    }
    out->set.source = QStringLiteral("pm3");
    return true;
}

bool savePm3File(const QString &path, const NonceSet &set,
                 const QByteArray &dump, bool withData, QString *error)
{
    QJsonObject root;
    const bool hasImage = withData && dump.size() == 1024;
    root.insert(QStringLiteral("FileType"),
                QString(QLatin1String(kPm3FileType)) + (hasImage ? QStringLiteral("_with_data") : QString()));

    QJsonObject ntObj;
    QJsonObject ntEncObj;
    QJsonObject parObj;
    for(int s = 0; s < kSectors; ++s)
    {
        QJsonObject ntSide;
        QJsonObject encSide;
        QJsonObject parSide;
        for(int k = 0; k < 2; ++k)
        {
            const QString side = k == 0 ? QStringLiteral("a") : QStringLiteral("b");
            ntSide.insert(side, QStringLiteral("%1").arg(set.nt[s][k], 8, 16, QLatin1Char('0')).toUpper());
            encSide.insert(side, QStringLiteral("%1").arg(set.ntEnc[s][k], 8, 16, QLatin1Char('0')).toUpper());
            if(set.parErr[s][k] >= 0)
                // Four characters keep the "last character is the parity bit"
                // rule of the reference reader (it ignores shorter fields).
                parSide.insert(side, QStringLiteral("000%1").arg(set.parErr[s][k] & 1));
        }
        ntObj.insert(QString::number(s), ntSide);
        ntEncObj.insert(QString::number(s), encSide);
        if(!parSide.isEmpty())
            parObj.insert(QString::number(s), parSide);
    }
    root.insert(QStringLiteral("nt"), ntObj);
    root.insert(QStringLiteral("nt_enc"), ntEncObj);
    if(!parObj.isEmpty())
        root.insert(QStringLiteral("par_err"), parObj);

    if(hasImage)
    {
        QJsonObject blocks;
        for(int b = 0; b < 64; ++b)
            blocks.insert(QString::number(b), QString::fromLatin1(dump.mid(b * 16, 16).toHex()).toUpper());
        root.insert(QStringLiteral("blocks"), blocks);
    }

    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if(error)
            *error = QStringLiteral("file: cannot write %1").arg(path);
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool loadPm3GuiFile(const QString &path, NonceSet *out, QString *error)
{
    if(!out)
        return false;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error)
            *error = QStringLiteral("file: cannot open %1").arg(path);
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if(parseError.error != QJsonParseError::NoError)
    {
        if(error)
            *error = jsonError(parseError);
        return false;
    }
    const QJsonObject root = doc.object();

    const QString magic = root.value(QStringLiteral("magic")).toString();
    if(magic != QLatin1String(kPm3GuiMagic))
    {
        if(error)
            *error = QStringLiteral("magic: expected \"%1\", got \"%2\"")
                     .arg(QLatin1String(kPm3GuiMagic), magic);
        return false;
    }

    NonceSet set;
    set.sectors = root.value(QStringLiteral("sectors")).toInt(kSectors);
    if(set.sectors != kSectors)
    {
        if(error)
            *error = QStringLiteral("sectors: expected %1, got %2").arg(kSectors).arg(set.sectors);
        return false;
    }

    const QString uidText = root.value(QStringLiteral("uid")).toString().trimmed();
    bool uidOk = false;
    set.uid = uidText.toUInt(&uidOk, 16);
    if(!uidOk)
    {
        if(error)
            *error = QStringLiteral("uid: \"%1\" is not a 32-bit hexadecimal value").arg(uidText);
        return false;
    }
    set.backdoorKey = root.value(QStringLiteral("backdoorKey")).toString().toUpper();

    const QJsonArray ntArray = root.value(QStringLiteral("nt")).toArray();
    const QJsonArray encArray = root.value(QStringLiteral("ntEnc")).toArray();
    if(ntArray.size() != kSectors || encArray.size() != kSectors)
    {
        if(error)
            *error = QStringLiteral("nt/ntEnc: expected %1 rows, got %2/%3")
                     .arg(kSectors).arg(ntArray.size()).arg(encArray.size());
        return false;
    }
    const QJsonArray parArray = root.value(QStringLiteral("parErr")).toArray();

    for(int s = 0; s < kSectors; ++s)
    {
        const QJsonArray ntRow = ntArray.at(s).toArray();
        const QJsonArray encRow = encArray.at(s).toArray();
        if(ntRow.size() != 2 || encRow.size() != 2)
        {
            if(error)
                *error = QStringLiteral("nt[%1]/ntEnc[%1]: expected 2 values, got %2/%3")
                         .arg(s).arg(ntRow.size()).arg(encRow.size());
            return false;
        }
        for(int k = 0; k < 2; ++k)
        {
            const double ntValue = ntRow.at(k).toDouble(-1);
            const double encValue = encRow.at(k).toDouble(-1);
            if(ntValue < 0 || ntValue > 4294967295.0 || encValue < 0 || encValue > 4294967295.0)
            {
                if(error)
                    *error = QStringLiteral("nt/ntEnc[%1][%2]: expected 0..4294967295")
                             .arg(s).arg(k);
                return false;
            }
            set.nt[s][k] = quint32(ntValue);
            set.ntEnc[s][k] = quint32(encValue);
            set.parErr[s][k] = -1;
        }
        if(s < parArray.size())
        {
            const QJsonArray parRow = parArray.at(s).toArray();
            for(int k = 0; k < 2 && k < parRow.size(); ++k)
            {
                const int value = parRow.at(k).toInt(-1);
                if(value != -1 && value != 0 && value != 1)
                {
                    if(error)
                        *error = QStringLiteral("parErr[%1][%2]: expected -1, 0 or 1, got %3")
                                 .arg(s).arg(k).arg(value);
                    return false;
                }
                set.parErr[s][k] = qint8(value);
            }
        }
    }

    if(set.validPairs() < 16)
    {
        if(error)
            *error = QStringLiteral("nt/ntEnc: only %1 usable nonce pairs, at least 16 required")
                     .arg(set.validPairs());
        return false;
    }
    set.source = QStringLiteral("pm3gui");
    *out = set;
    return true;
}

bool savePm3GuiFile(const QString &path, const NonceSet &set, QString *error)
{
    QJsonObject root;
    root.insert(QStringLiteral("magic"), QLatin1String(kPm3GuiMagic));
    root.insert(QStringLiteral("uid"),
                QStringLiteral("%1").arg(set.uid, 8, 16, QLatin1Char('0')).toUpper());
    root.insert(QStringLiteral("sectors"), kSectors);
    root.insert(QStringLiteral("backdoorKey"), set.backdoorKey.toUpper());

    QJsonArray ntRows;
    QJsonArray encRows;
    QJsonArray parRows;
    for(int s = 0; s < kSectors; ++s)
    {
        QJsonArray ntRow;
        QJsonArray encRow;
        QJsonArray parRow;
        for(int k = 0; k < 2; ++k)
        {
            ntRow.append(double(set.nt[s][k]));
            encRow.append(double(set.ntEnc[s][k]));
            parRow.append(int(set.parErr[s][k]));
        }
        ntRows.append(ntRow);
        encRows.append(encRow);
        parRows.append(parRow);
    }
    root.insert(QStringLiteral("nt"), ntRows);
    root.insert(QStringLiteral("ntEnc"), encRows);
    root.insert(QStringLiteral("parErr"), parRows);

    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if(error)
            *error = QStringLiteral("file: cannot write %1").arg(path);
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QString validateNonceSet(const NonceSet &set)
{
    if(set.sectors != kSectors)
        return QStringLiteral("sectors: expected %1, got %2").arg(kSectors).arg(set.sectors);
    for(int s = 0; s < kSectors; ++s)
    {
        for(int k = 0; k < 2; ++k)
        {
            if(set.parErr[s][k] < -1 || set.parErr[s][k] > 1)
                return QStringLiteral("parErr[%1][%2]: expected -1, 0 or 1").arg(s).arg(k);
        }
    }
    if(set.validPairs() < 16)
        return QStringLiteral("nt/ntEnc: only %1 usable nonce pairs").arg(set.validPairs());
    return QString();
}

quint64 parseKey(const QString &text, bool *ok)
{
    static const QRegularExpression re(QStringLiteral("^[0-9A-Fa-f]{12}$"));
    const QString trimmed = text.trimmed();
    if(!re.match(trimmed).hasMatch())
    {
        if(ok)
            *ok = false;
        return 0;
    }
    const quint64 value = trimmed.toULongLong(ok, 16);
    return value & 0xFFFFFFFFFFFFULL;
}

QString formatKey(quint64 key)
{
    return QStringLiteral("%1").arg(key & 0xFFFFFFFFFFFFULL, 12, 16, QLatin1Char('0')).toUpper();
}

bool writeDicFile(const QString &path, const QVector<quint64> &keys, QString *error)
{
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if(error)
            *error = QStringLiteral("file: cannot write %1").arg(path);
        return false;
    }
    QByteArray buffer;
    buffer.reserve(keys.size() * 13);
    for(quint64 key : keys)
        buffer += formatKey(key).toLatin1() + "\n";
    file.write(buffer);
    file.close();
    return true;
}

QVector<quint64> readKeyList(const QString &path)
{
    QVector<quint64> keys;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return keys;
    static const QRegularExpression re(QStringLiteral("[0-9A-Fa-f]{12}"));
    const QString text = QString::fromLatin1(file.readAll());
    file.close();
    auto it = re.globalMatch(text);
    while(it.hasNext())
    {
        const QRegularExpressionMatch match = it.next();
        bool ok = false;
        const quint64 key = match.captured(0).toULongLong(&ok, 16);
        if(ok)
            keys.append(key & 0xFFFFFFFFFFFFULL);
    }
    return keys;
}

QByteArray buildKeyBin(const CrackResult &result)
{
    QByteArray image(192, '\0');
    for(int s = 0; s < kSectors; ++s)
    {
        for(int k = 0; k < 2; ++k)
        {
            bool ok = false;
            const quint64 key = parseKey(result.keys[s][k], &ok);
            if(!ok)
                continue;
            const int offset = (k == 0 ? 0 : 96) + s * 6;
            for(int i = 0; i < 6; ++i)
                image[offset + i] = char((key >> (8 * (5 - i))) & 0xFFu);
        }
    }
    return image;
}

bool loadKeyBin(const QString &path, QString keys[kSectors][2])
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray image = file.readAll();
    file.close();
    if(image.size() < 192)
        return false;
    for(int s = 0; s < kSectors; ++s)
    {
        for(int k = 0; k < 2; ++k)
        {
            const int offset = (k == 0 ? 0 : 96) + s * 6;
            quint64 key = 0;
            for(int i = 0; i < 6; ++i)
                key = (key << 8) | quint8(image[offset + i]);
            if(key != 0 && key != 0xFFFFFFFFFFFFULL)
                keys[s][k] = formatKey(key);
        }
    }
    return true;
}

QVector<quint64> defaultKeys()
{
    return QVector<quint64>{
        0xFFFFFFFFFFFFULL,
        0x000000000000ULL,
        0xA0A1A2A3A4A5ULL,
        0xD3F7D3F7D3F7ULL,
    };
}

} // namespace NonceStore
} // namespace mfgen3
