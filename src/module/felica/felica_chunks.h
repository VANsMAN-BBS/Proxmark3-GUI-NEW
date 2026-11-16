#ifndef FELICA_CHUNKS_H
#define FELICA_CHUNKS_H

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace FelicaChunks {

/**
 * Read Without Encryption accepts at most 15 blocks per request; a 16-block
 * request is rejected with status FF A2. This helper splits any block range
 * into safe request windows (pair = first block, count).
 */
inline QVector<QPair<int, int> > plan(int totalBlocks, int maxBlocks = 15)
{
    QVector<QPair<int, int> > result;
    if (totalBlocks <= 0 || maxBlocks <= 0)
        return result;

    for (int first = 0; first < totalBlocks; first += maxBlocks)
        result.append(qMakePair(first, qMin(maxBlocks, totalBlocks - first)));
    return result;
}

/**
 * Build a FeliCa Read Without Encryption frame:
 *   06 <IDm 8> 01 <service LE16> <count> (<0x80|block_high, block_low> ...)
 *
 * Block list entries are two bytes each. `idmHex` is 16 hex characters.
 * Returns an empty array when the IDm is malformed.
 */
inline QByteArray readFrame(const QString &idmHex, int service, int firstBlock, int count)
{
    if (idmHex.size() != 16 || count <= 0)
        return QByteArray();

    QByteArray frame;
    frame.reserve(15 + count * 2);
    frame.append(static_cast<char>(0x06));

    for (int i = 0; i < 8; ++i)
    {
        bool ok = false;
        const int value = idmHex.mid(i * 2, 2).toInt(&ok, 16);
        if (!ok)
            return QByteArray();
        frame.append(static_cast<char>(value));
    }

    frame.append(static_cast<char>(0x01));
    frame.append(static_cast<char>(service & 0xFF));
    frame.append(static_cast<char>((service >> 8) & 0xFF));
    frame.append(static_cast<char>(count & 0xFF));
    for (int i = 0; i < count; ++i)
    {
        const int block = firstBlock + i;
        // Two-byte block element: 0x80 | block_high, block_low.
        frame.append(static_cast<char>(0x80 | ((block >> 8) & 0x7F)));
        frame.append(static_cast<char>(block & 0xFF));
    }
    return frame;
}

} // namespace FelicaChunks

#endif // FELICA_CHUNKS_H
