#include "ntagmemory.h"

#include <QChar>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QStringList>

namespace Ntag {

void MemoryImage::configure(const Capability &capacity)
{
    m_cap = capacity;
    m_pages.clear();
    for(int page = 0; page < capacity.pages; page++)
    {
        MemoryPage entry;
        entry.number = page;
        entry.pageClass = classifyPage(capacity, page);
        entry.writable = entry.pageClass == PageClass::Writable;
        m_pages.append(entry);
    }
}

void MemoryImage::clear()
{
    m_cap = Capability();
    m_pages.clear();
}

MemoryPage *MemoryImage::page(int number)
{
    if(number < 0 || number >= m_pages.size())
        return nullptr;
    return &m_pages[number];
}

const MemoryPage *MemoryImage::page(int number) const
{
    if(number < 0 || number >= m_pages.size())
        return nullptr;
    return &m_pages.at(number);
}

bool MemoryImage::setPageData(int number, const QByteArray &data, QString *error)
{
    MemoryPage *entry = page(number);
    if(!entry)
    {
        if(error)
            *error = QStringLiteral("page %1 is outside the detected layout").arg(number);
        return false;
    }
    if(data.size() != 4)
    {
        if(error)
            *error = QStringLiteral("page %1 data must be 4 bytes").arg(number);
        return false;
    }
    entry->original = data;
    entry->edited = data;
    entry->state = PageState::Read;
    entry->writeState = WriteState::None;
    entry->verified = false;
    entry->error.clear();
    return true;
}

void MemoryImage::markReadFailed(int number, const QString &error)
{
    MemoryPage *entry = page(number);
    if(!entry)
        return;
    entry->state = PageState::Failed;
    entry->error = error;
}

bool MemoryImage::editByte(int number, int offset, quint8 value, QString *error)
{
    MemoryPage *entry = page(number);
    if(!entry || offset < 0 || offset > 3)
    {
        if(error)
            *error = QStringLiteral("byte position is outside the page");
        return false;
    }
    if(!entry->readable())
    {
        if(error)
            *error = QStringLiteral("page %1 has not been read successfully").arg(number);
        return false;
    }
    if(!entry->writable)
    {
        if(error)
            *error = QStringLiteral("page %1 is read-only (%2)")
                    .arg(number).arg(pageClassName(entry->pageClass));
        return false;
    }
    entry->edited[offset] = static_cast<char>(value);
    entry->writeState = WriteState::None;
    entry->verified = false;
    entry->error.clear();
    return true;
}

bool MemoryImage::setBytes(int firstPage, int offset, const QByteArray &bytes,
                           QString *error)
{
    if(bytes.isEmpty())
    {
        if(error)
            *error = QStringLiteral("no bytes to write");
        return false;
    }
    if(offset < 0 || offset > 3)
    {
        if(error)
            *error = QStringLiteral("offset must be 0..3");
        return false;
    }

    // Validate the whole range before changing anything.
    for(int i = 0; i < bytes.size(); i++)
    {
        const int target = firstPage + (offset + i) / 4;
        const MemoryPage *entry = page(target);
        if(!entry)
        {
            if(error)
                *error = QStringLiteral("byte %1 targets page %2 outside the layout")
                        .arg(i).arg(target);
            return false;
        }
        if(!entry->readable())
        {
            if(error)
                *error = QStringLiteral("page %1 has not been read successfully").arg(target);
            return false;
        }
        if(!entry->writable)
        {
            if(error)
                *error = QStringLiteral("page %1 is read-only (%2)")
                        .arg(target).arg(pageClassName(entry->pageClass));
            return false;
        }
    }

    for(int i = 0; i < bytes.size(); i++)
    {
        const int target = firstPage + (offset + i) / 4;
        const int index = (offset + i) % 4;
        MemoryPage *entry = page(target);
        entry->edited[index] = bytes.at(i);
        entry->writeState = WriteState::None;
        entry->verified = false;
        entry->error.clear();
    }
    return true;
}

void MemoryImage::markWritten(int number, bool ok, const QString &error)
{
    MemoryPage *entry = page(number);
    if(!entry)
        return;
    entry->writeState = ok ? WriteState::Written : WriteState::Failed;
    entry->error = error;
    if(ok)
        entry->verified = false;
}

void MemoryImage::markVerified(int number, bool ok)
{
    MemoryPage *entry = page(number);
    if(!entry)
        return;
    entry->verified = ok;
}

void MemoryImage::revertPage(int number)
{
    MemoryPage *entry = page(number);
    if(!entry || entry->original.size() != 4)
        return;
    entry->edited = entry->original;
    entry->writeState = WriteState::None;
    entry->verified = false;
    entry->error.clear();
}

void MemoryImage::revertAll()
{
    for(int number : readablePages())
        revertPage(number);
}

QList<int> MemoryImage::dirtyPages() const
{
    QList<int> result;
    for(const MemoryPage &entry : m_pages)
        if(entry.dirty())
            result.append(entry.number);
    return result;
}

QList<int> MemoryImage::readablePages() const
{
    QList<int> result;
    for(const MemoryPage &entry : m_pages)
        if(entry.readable())
            result.append(entry.number);
    return result;
}

QByteArray MemoryImage::originalBytes() const
{
    QByteArray out;
    for(const MemoryPage &entry : m_pages)
        out += entry.original.size() == 4 ? entry.original : QByteArray(4, '\0');
    return out;
}

QByteArray MemoryImage::editedBytes() const
{
    QByteArray out;
    for(const MemoryPage &entry : m_pages)
        out += entry.edited.size() == 4 ? entry.edited : QByteArray(4, '\0');
    return out;
}

bool parseHexBytes(const QString &text, QByteArray *bytes, QString *error)
{
    if(!bytes)
        return false;
    bytes->clear();

    QString cleaned;
    for(const QChar ch : text)
    {
        if(ch.isSpace() || ch == QLatin1Char(',') || ch == QLatin1Char(':')
                || ch == QLatin1Char('-') || ch == QLatin1Char('_'))
            continue;
        cleaned += ch;
    }
    cleaned.remove(QStringLiteral("0x"), Qt::CaseInsensitive);
    cleaned.remove(QStringLiteral("0X"), Qt::CaseInsensitive);
    if(cleaned.isEmpty())
    {
        if(error)
            *error = QStringLiteral("no hex bytes");
        return false;
    }
    if(cleaned.size() % 2 != 0)
    {
        if(error)
            *error = QStringLiteral("hex data has an odd number of digits");
        return false;
    }
    for(const QChar ch : cleaned)
    {
        const bool hex = (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
                (ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) ||
                (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'));
        if(!hex)
        {
            if(error)
                *error = QStringLiteral("invalid hex character '%1'").arg(ch);
            return false;
        }
    }
    *bytes = QByteArray::fromHex(cleaned.toLatin1());
    return true;
}

QByteArray buildDump(const QByteArray &version, const QList<QByteArray> &pages)
{
    if(pages.isEmpty() || pages.size() > 256)
        return QByteArray();
    for(const QByteArray &page : pages)
        if(page.size() != 4)
            return QByteArray();
    QByteArray file(56, '\0');
    if(!version.isEmpty())
        file.replace(0, qMin(8, version.size()), version.left(8));
    file[0x0B] = static_cast<char>(pages.size() - 1);
    for(const QByteArray &page : pages)
        file += page;
    return file;
}

bool ReadJobState::start(ReadJobMode newMode, int pageTotal)
{
    if(busy || newMode == ReadJobMode::None || pageTotal <= 0)
        return false;
    busy = true;
    cancelled = false;
    mode = newMode;
    done = 0;
    total = pageTotal;
    failures = 0;
    return true;
}

void ReadJobState::success(int count)
{
    if(!busy || count <= 0)
        return;
    done = qMin(total, done + count);
}

void ReadJobState::failure(int count)
{
    if(!busy || count <= 0)
        return;
    failures += count;
}

void ReadJobState::requestCancel()
{
    if(busy)
        cancelled = true;
}

void ReadJobState::finish()
{
    busy = false;
    cancelled = false;
    mode = ReadJobMode::None;
    done = 0;
    total = 0;
    failures = 0;
}

bool WriteJobState::start(WriteJobMode newMode, int pageTotal)
{
    if(busy || newMode == WriteJobMode::None || pageTotal <= 0)
        return false;
    busy = true;
    cancelled = false;
    retryUsed = false;
    mode = newMode;
    phase = WriteJobPhase::Idle;
    total = pageTotal;
    written = 0;
    failures = 0;
    currentPage = -1;
    return true;
}

void WriteJobState::beginPage(int page)
{
    if(!busy)
        return;
    currentPage = page;
    retryUsed = false;
}

void WriteJobState::success()
{
    if(!busy)
        return;
    written = qMin(total, written + 1);
    retryUsed = false;
}

void WriteJobState::failure()
{
    if(!busy)
        return;
    failures++;
    retryUsed = false;
}

void WriteJobState::requestCancel()
{
    if(busy)
        cancelled = true;
}

void WriteJobState::finish()
{
    busy = false;
    cancelled = false;
    retryUsed = false;
    mode = WriteJobMode::None;
    phase = WriteJobPhase::Idle;
    total = 0;
    written = 0;
    failures = 0;
    currentPage = -1;
}

DumpWorkspace::DumpWorkspace()
{
    // Prefer the system temp directory.  Some restricted sessions deny
    // directory creation there, so fall back to the app data directory and
    // finally to the working directory.  Every candidate is still a private
    // QTemporaryDir whose contents are removed on cleanup/shutdown.
    QStringList bases;
    bases << QDir::tempPath();
    const QString appData = QStandardPaths::writableLocation(
                QStandardPaths::AppLocalDataLocation);
    if(!appData.isEmpty())
        bases << appData;
    bases << QDir::currentPath();
    bases << QCoreApplication::applicationDirPath();

    for(const QString &base : bases)
    {
        if(base.isEmpty())
            continue;
        QDir dir(base);
        if(!dir.exists())
            QDir().mkpath(base);
        QScopedPointer<QTemporaryDir> candidate(new QTemporaryDir(
            QDir::toNativeSeparators(base) + QStringLiteral("/pm3gui-ntag-XXXXXX")));
        if(candidate->isValid())
        {
            m_dir.swap(candidate);
            return;
        }
    }
}

QString DumpWorkspace::path() const
{
    return m_dir ? m_dir->filePath(QStringLiteral("pm3gui-ntag-dump.bin"))
                 : QString();
}

QString DumpWorkspace::sidecarPath() const
{
    return m_dir ? m_dir->filePath(QStringLiteral("pm3gui-ntag-dump.json"))
                 : QString();
}

QString DumpWorkspace::simPath() const
{
    return m_dir ? m_dir->filePath(QStringLiteral("pm3gui-ntag-sim.bin"))
                 : QString();
}

bool DumpWorkspace::writeSimDump(const QByteArray &dump, QString *error)
{
    if(error)
        error->clear();
    if(!m_dir || !m_dir->isValid())
    {
        if(error)
            *error = QStringLiteral("no temporary directory is available");
        return false;
    }
    if(dump.isEmpty())
    {
        if(error)
            *error = QStringLiteral("the simulator dump is empty");
        return false;
    }
    QFile file(simPath());
    if(!file.open(QIODevice::WriteOnly))
    {
        if(error)
            *error = QStringLiteral("cannot create %1").arg(simPath());
        return false;
    }
    const qint64 written = file.write(dump);
    file.close();
    if(written != dump.size())
    {
        if(error)
            *error = QStringLiteral("cannot write the complete simulator dump");
        QFile::remove(simPath());
        return false;
    }
    return true;
}

void DumpWorkspace::clearDump()
{
    if(!m_dir || !m_dir->isValid())
        return;
    QDir dir(m_dir->path());
    const QStringList files = dir.entryList(
                QStringList() << QStringLiteral("pm3gui-ntag-dump*.bin")
                              << QStringLiteral("pm3gui-ntag-dump*.json")
                              << QStringLiteral("pm3gui-ntag-sim*.bin")
                              << QStringLiteral("pm3gui-ntag-sim*.json"),
                QDir::Files);
    for(const QString &name : files)
        dir.remove(name);
}

} // namespace Ntag
