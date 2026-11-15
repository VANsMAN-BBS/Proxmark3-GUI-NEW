#ifndef LFPARSETYPES_H
#define LFPARSETYPES_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

/**
 * LF parser result types.
 *
 * This header is intentionally free of widgets, processes and hardware access:
 * the module only turns Proxmark3 client *text* into structured data.
 *
 * Optional<T> is used instead of sentinel values so that a missing field can
 * never be confused with a legitimate 0.
 */
namespace Lf {

enum class Protocol {
    Unknown = 0,
    EM4100,
    HIDProx,
    AWID,
    Indala
};

enum class Status {
    Identified,   // protocol plus its core identity is present
    Partial,      // protocol recognised, core identity incomplete
    Unidentified, // nothing matched
    InvalidInput  // empty or non-text input
};

enum class Confidence {
    None = 0,
    Low,
    Medium,
    High
};

template <typename T>
class Optional
{
public:
    Optional() : m_present(false), m_value() {}
    explicit Optional(const T &value) : m_present(true), m_value(value) {}

    bool isPresent() const { return m_present; }
    bool isAbsent() const { return !m_present; }

    const T &value() const { return m_value; }
    T valueOr(const T &fallback) const { return m_present ? m_value : fallback; }

    void set(const T &value)
    {
        m_present = true;
        m_value = value;
    }

    void clear()
    {
        m_present = false;
        m_value = T();
    }

private:
    bool m_present;
    T m_value;
};

QString protocolName(Protocol protocol);
QString statusName(Status status);
QString confidenceName(Confidence confidence);

} // namespace Lf

struct LfExtraField
{
    QString name;
    QString value;

    LfExtraField() {}
    LfExtraField(const QString &fieldName, const QString &fieldValue)
        : name(fieldName), value(fieldValue) {}
};

/**
 * One parsed LF card.
 *
 * Fields are deliberately protocol neutral: a protocol that has no facility
 * code simply leaves facilityCode absent instead of inventing a 0.
 */
struct LFParseResult
{
    Lf::Protocol protocol = Lf::Protocol::Unknown;
    Lf::Status status = Lf::Status::Unidentified;
    Lf::Confidence confidence = Lf::Confidence::None;

    QString displayName;
    QString format;
    QStringList formatCandidates;

    Lf::Optional<int> bitLength;
    Lf::Optional<quint32> facilityCode;
    Lf::Optional<quint64> cardNumber;

    QString rawData; // uppercase hex, separators removed
    QString hexId;   // protocol specific hexadecimal identity
    Lf::Optional<quint64> decimalId;

    QList<LfExtraField> extraFields;
    QStringList warnings;
    QStringList unparsedFields;
    QString sourceText;

    int matchScore = 0;

    /** True when at least one protocol specific identity value is present. */
    bool hasIdentity() const;

    void addWarning(const QString &warning);
    void addExtra(const QString &name, const QString &value);
    void addUnparsed(const QString &field);

    /** Short single line description, useful for logs. */
    QString summary() const;
};

#endif // LFPARSETYPES_H
