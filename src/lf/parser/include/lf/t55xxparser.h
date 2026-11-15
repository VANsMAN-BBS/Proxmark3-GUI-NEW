#ifndef LF_T55XXPARSER_H
#define LF_T55XXPARSER_H

#include <QString>
#include <QStringList>

#include "lf/lfparsetypes.h"

/**
 * Read-only T55xx / T5577 text and block0 parser.
 *
 * This is intentionally NOT registered in LFParserRegistry: a T55xx chip is a
 * writable transport, not a card protocol. It never produces write commands.
 */
namespace LfT55xx {

enum class Modulation {
    Unknown = 0,
    NRZ,
    PSK1,
    PSK2,
    PSK3,
    FSK1,
    FSK2,
    FSK1a,
    FSK2a,
    ASK,
    Biphase,
    BiphaseInverted
};

QString modulationName(Modulation modulation);

struct Block0
{
    bool valid = false;
    quint32 raw = 0;

    Lf::Optional<Modulation> modulation;
    bool extend = false;
    Lf::Optional<int> bitRateIndex;
    Lf::Optional<int> bitRateDivisor; // RF/<divisor>
    Lf::Optional<int> maxBlock;
    quint8 flagBits = 0;
    Lf::Optional<quint8> masterKey;

    QList<LfExtraField> fields;
    QStringList warnings;
};

struct DetectInfo
{
    bool detected = false;
    QString chipType;
    Lf::Optional<bool> passwordSet;
    Lf::Optional<quint32> block0;
    QString block0Hex;
    QString modulationText;
    Lf::Optional<int> bitRate;
    Lf::Optional<int> offset;
    Lf::Optional<bool> inverted;

    QList<LfExtraField> extraFields;
    QStringList warnings;
    QString sourceText;
};

/** Parses `lf t55xx detect` output. */
DetectInfo parseDetect(const QString &text);

/** Parses `lf t55xx info` output (same field labels in current clients). */
DetectInfo parseInfo(const QString &text);

/** Decodes an 8 hex digit block0 value. Invalid input yields valid == false. */
Block0 parseBlock0(const QString &block0Hex);

} // namespace LfT55xx

#endif // LF_T55XXPARSER_H
