#include "amiibodata.h"

#include <QCryptographicHash>

namespace Amiibo {

namespace {

bool isRawImageSize(int size)
{
    return size == 180 || size == 540 || size == 924 || size == 1024 || size == 2048;
}

bool isSupportedDumpPages(int pages)
{
    // Complete NTAG213/215/216 dumps plus the client's 42-page default.
    // Keeping this list closed prevents a raw image (whose page-2 lock byte
    // happens to equal pages-1) from being mistaken for a dump file.
    return pages == 42 || pages == 45 || pages == 135 || pages == 231;
}

} // namespace

Info classify(const QByteArray &file)
{
    Info info;
    if(file.isEmpty())
    {
        info.error = QStringLiteral("empty file");
        return info;
    }

    // `hf mfu dump` format: 56-byte header, pages start at 0x38, the header
    // byte 0x0B holds the last page number.  Only accept the file when the
    // page count and the file length agree, so a raw 540-byte image cannot be
    // mistaken for a dump by accident.
    if(file.size() >= 56 + 4 * 4)
    {
        const int dumpPages = static_cast<quint8>(file.at(0x0B)) + 1;
        if(isSupportedDumpPages(dumpPages) && file.size() == 56 + dumpPages * 4)
        {
            info.kind = Kind::DumpFile;
            info.pages = dumpPages;
            info.payloadOffset = 56;
            info.header = file.left(56);
            info.payload = file.mid(56);
            return info;
        }
    }

    if(isRawImageSize(file.size()))
    {
        info.kind = Kind::RawImage;
        info.pages = file.size() / 4;
        info.payloadOffset = 0;
        info.payload = file;
        return info;
    }

    info.error = QStringLiteral("unsupported length %1 bytes").arg(file.size());
    return info;
}

QString sha256(const QByteArray &data)
{
    return QString::fromLatin1(
                QCryptographicHash::hash(data, QCryptographicHash::Sha256)
                .toHex());
}

QByteArray makeDump(const QByteArray &payload, int pages,
                    const QByteArray &version)
{
    if(payload.isEmpty() || payload.size() % 4 != 0)
        return QByteArray();
    if(pages <= 0)
        pages = payload.size() / 4;
    // Never truncate: the payload is authoritative.  When the requested page
    // count disagrees with the payload, follow the payload.
    if(pages * 4 != payload.size())
        pages = payload.size() / 4;
    if(pages > 256)
        return QByteArray(); // 0x0B is a single byte

    QByteArray file(56, '\0');
    if(!version.isEmpty())
        file.replace(0, qMin(8, version.size()), version.left(8));
    file[0x0B] = static_cast<char>(pages - 1);
    file += payload;
    return file;
}

bool requiresAsciiClientPath(const QString &path)
{
    for(const QChar ch : path)
        if(ch.unicode() > 0x7F)
            return true;
    return false;
}

} // namespace Amiibo
