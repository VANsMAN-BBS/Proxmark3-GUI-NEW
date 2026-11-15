#ifndef NTAGMODEL_H
#define NTAGMODEL_H

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QVector>

/**
 * Pure Type 2 Tag logic: GET_VERSION decoding, model identification, memory
 * map, dump parsing and NDEF decoding.
 *
 * No widgets, no client calls, no hardware: everything here is testable with
 * synthetic bytes.
 */
namespace Ntag {

enum class Model {
    Unknown,
    NTAG213,
    NTAG215,
    NTAG216
};

enum class Confidence {
    Exact,     // GET_VERSION storage size and CC agree
    Probable,  // version or CC alone, or client type string
    Unknown
};

enum class PageState {
    Unknown,      // never read
    Read,         // data present
    Failed,       // read attempted and failed
    Unavailable   // page outside the identified layout
};

struct Version
{
    bool valid = false;
    quint8 vendor = 0;
    quint8 productType = 0;
    quint8 productSubtype = 0;
    quint8 major = 0;
    quint8 minor = 0;
    quint8 storageSize = 0;
    quint8 protocol = 0;
    QByteArray raw;
};

struct Capability
{
    Model model = Model::Unknown;
    Confidence confidence = Confidence::Unknown;
    int pages = 0;          // total pages (0 = unknown)
    int userFirst = 4;
    int userLast = -1;
    int lockPage = -1;
    int cfg0 = -1;
    int cfg1 = -1;
    int pwd = -1;
    int pack = -1;
    QString basis;          // human readable identification basis
};

struct CC
{
    bool valid = false;
    quint8 magic = 0;
    quint8 version = 0;
    quint8 size = 0;
    int ndefCapacity = 0;
    bool readWrite = false;
    bool multiBlock = false;
    QByteArray raw;
};

struct Dump
{
    bool valid = false;
    QString error;
    QByteArray versionRaw;
    quint8 maxPage = 0;
    QList<QByteArray> pages; // 4 bytes each, index == page number
};

struct NdefRecord
{
    QString tnf;
    QString type;
    QString typeName;
    QString text;      // text/URI payload preview
    QString mime;
    QString uri;
    QByteArray payload;
    QByteArray raw;
};

struct NdefResult
{
    bool ok = false;
    QString error;
    QByteArray message;
    QList<NdefRecord> records;
};

/** Role of a page in the detected memory layout. */
enum class PageClass {
    Writable,       // user data page that may be written
    ReadOnly,       // read-only under the detected layout
    Manufacturer,   // page 0: UID0-2 / BCC0 / internal
    UID,            // chained UID bytes
    Lock,           // static or dynamic lock bytes
    Config,         // CC / CFG0 / CFG1
    Security,       // PWD / PACK
    NotApplicable,  // outside the layout / vendor / reserved
    Unknown         // model or layout not identified
};

/** Plan for clearing only user data pages. */
struct ErasePlan
{
    bool valid = false;
    QString error;
    QList<int> clearPages;    // writable user pages holding non-zero data
    QList<int> blankPages;    // writable user pages that are already zero
    int userFirst = 4;
    int userLast = -1;
    int totalPages = 0;
    bool lockBitsPresent = false;
    QByteArray staticLock;
    QByteArray dynamicLock;
};

enum class PageVerdict {
    Match,          // writable page matches the expected bytes
    Mismatch,       // writable page differs from the expected bytes
    Preserved,      // read-only/system page still readable (not a failure)
    Unchecked,      // page could not be read, so no conclusion
    NotApplicable   // page not part of this operation
};

enum class VerifyStatus {
    Passed,             // every writable target page matches
    PassedWithExceptions, // writable targets match, some pages were not checkable
    Failed,             // at least one writable target page differs
    Incomplete          // not enough pages were read to decide
};

struct PageCheck
{
    int page = -1;
    PageClass pageClass = PageClass::Unknown;
    QByteArray expected;
    QByteArray actual;
    PageVerdict verdict = PageVerdict::NotApplicable;
    QString note;
};

struct VerifyReport
{
    VerifyStatus status = VerifyStatus::Incomplete;
    QList<PageCheck> checks;
    int writableTargets = 0;
    int writablePassed = 0;
    int writableFailed = 0;
    int unchecked = 0;
};

QString modelName(Model model);
QString confidenceName(Confidence confidence);
QString pageStateName(PageState state);
QString pageClassName(PageClass pageClass);
QString verifyStatusName(VerifyStatus status);

Version parseVersion(const QByteArray &raw);
Capability identify(const Version &version, const CC &cc, const QString &clientType);
Capability capacityFor(Model model, Confidence confidence, const QString &basis);

CC parseCC(const QByteArray &raw4);

/** Parses a `hf mfu dump` .bin file (56-byte header, pages from 0x38). */
Dump parseDump(const QByteArray &file);

/** Region label for a page under the given layout. */
QString regionName(const Capability &capacity, int page);

/** Parses CC/TLV/NDEF from the user area (pages 4..). */
NdefResult parseNdef(const QByteArray &userArea);

/** Role of a page under the given layout (based on the identified model). */
PageClass classifyPage(const Capability &capacity, int page);

/**
 * Builds a clear plan from a full read.  Only writable user pages are
 * targeted; UID/CC/lock/config/PWD/PACK are never included.  Refuses when the
 * model or the layout is unknown, or when a user page was not read.
 */
ErasePlan planErase(const Capability &capacity, const QList<QByteArray> &pages);

/**
 * Compares only the pages that were actually targeted by a write.
 * Read-only/system pages can never turn the result into a failure.
 */
VerifyReport verifyWrite(const Capability &capacity,
                         const QMap<int, QByteArray> &expected,
                         const QList<QByteArray> &actual,
                         const QVector<PageState> &states);

/** URI prefix table entry (index 0x00..0x23), empty when out of range. */
QString uriPrefix(quint8 code);

} // namespace Ntag

#endif // NTAGMODEL_H
