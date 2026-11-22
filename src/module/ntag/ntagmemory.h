#ifndef NTAGMEMORY_H
#define NTAGMEMORY_H

#include <QByteArray>
#include <QList>
#include <QScopedPointer>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include "ntagmodel.h"

/**
 * Editable memory image for the NTAG page.
 *
 * `original` always holds the bytes of the last successful read and is never
 * overwritten by an edit.  `edited` is what the user sees and may write.
 * Only pages classified as Writable and successfully read can be edited.
 */
namespace Ntag {

enum class WriteState {
    None,
    Written,
    Failed
};

struct MemoryPage
{
    int number = -1;
    QByteArray original;
    QByteArray edited;
    PageState state = PageState::Unknown;
    PageClass pageClass = PageClass::Unknown;
    bool writable = false;
    WriteState writeState = WriteState::None;
    bool verified = false;
    QString error;

    bool readable() const { return state == PageState::Read && edited.size() == 4; }
    bool dirty() const
    {
        return readable() && original.size() == 4 && edited != original;
    }
};

class MemoryImage
{
public:
    void configure(const Capability &capacity);
    void clear();

    const Capability &capability() const { return m_cap; }
    int pageCount() const { return m_pages.size(); }
    bool valid() const { return m_cap.pages > 0; }

    MemoryPage *page(int number);
    const MemoryPage *page(int number) const;

    /** Stores a successful read (original == edited == data). */
    bool setPageData(int number, const QByteArray &data, QString *error = nullptr);
    void markReadFailed(int number, const QString &error);

    /** Edits one byte of the edited snapshot (not the original). */
    bool editByte(int number, int offset, quint8 value, QString *error = nullptr);

    /** Edits a byte range; it may span consecutive pages. */
    bool setBytes(int firstPage, int offset, const QByteArray &bytes,
                  QString *error = nullptr);

    void markWritten(int number, bool ok, const QString &error = QString());
    void markVerified(int number, bool ok);
    void revertPage(int number);
    void revertAll();

    QList<int> dirtyPages() const;
    QList<int> readablePages() const;
    QByteArray originalBytes() const;
    QByteArray editedBytes() const;

private:
    Capability m_cap;
    QVector<MemoryPage> m_pages;
};

/** Parses "E1 10 3E 00", "E1103E00" or "E1,10,3E,00". */
bool parseHexBytes(const QString &text, QByteArray *bytes, QString *error);

/** Builds the 56-byte dump header + pages accepted by the client tools. */
QByteArray buildDump(const QByteArray &version, const QList<QByteArray> &pages);

/** Async batch-read state shared by the NTAG page and its tests. */
enum class ReadJobMode
{
    None,
    DumpChunks,
    Pages
};

struct ReadJobState
{
    bool busy = false;
    bool cancelled = false;
    ReadJobMode mode = ReadJobMode::None;
    int done = 0;
    int total = 0;
    int failures = 0;

    bool start(ReadJobMode newMode, int pageTotal);
    void success(int count = 1);
    void failure(int count = 1);
    void requestCancel();
    void finish();
};

/** Async write/erase/verify state shared by the NTAG page and its tests. */
enum class WriteJobMode
{
    None,
    WritePages,
    Erase,
    Verify
};

enum class WriteJobPhase
{
    Idle,
    Snapshot,
    Writing,
    ReadBack,
    Verifying
};

struct WriteJobState
{
    bool busy = false;
    bool cancelled = false;
    bool retryUsed = false;
    WriteJobMode mode = WriteJobMode::None;
    WriteJobPhase phase = WriteJobPhase::Idle;
    int total = 0;
    int written = 0;
    int failures = 0;
    int currentPage = -1;

    bool start(WriteJobMode newMode, int pageTotal);
    void beginPage(int page);
    void success();
    void failure();
    void requestCancel();
    void finish();
};

/**
 * Private temporary directory for `hf mfu dump` work files.
 *
 * The client writes both the requested `.bin` and a `.json` sidecar next to
 * it.  The workspace keeps both inside its own QTemporaryDir so cleanup can
 * never touch a user dump or another process' files.
 */
class DumpWorkspace
{
public:
    DumpWorkspace();

    bool isValid() const { return m_dir && m_dir->isValid(); }
    QString path() const;
    QString sidecarPath() const;
    QString simPath() const;

    /** Writes a simulator dump into this workspace (no user path involved). */
    bool writeSimDump(const QByteArray &dump, QString *error = nullptr);

    /** Removes the dump and every client sidecar inside this workspace only. */
    void clearDump();

private:
    QScopedPointer<QTemporaryDir> m_dir;
};

} // namespace Ntag

#endif // NTAGMEMORY_H
