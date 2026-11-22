#include "mfpm3log.h"

#include <QRegularExpression>

namespace mfgen3
{

namespace
{
const int kMaxLinesPerTask = 400;

/** Failures/completion must survive the line cap. */
bool isImportant(const QString &line)
{
    static const QRegularExpression important(
        QStringLiteral("fail|error|timeout|done|complete|success|abort|"
                       "cancel|saved|not found|no card"),
        QRegularExpression::CaseInsensitiveOption);
    return important.match(line).hasMatch();
}

/** Lines that only carry a percentage are pure progress noise. */
bool isProgressOnly(const QString &line)
{
    if (!line.contains(QLatin1Char('%')))
        return false;
    static const QRegularExpression meaningful(
        QStringLiteral("block|blk|sector|sec\\b|key|nonce|uid|card|found|fail|"
                       "error|done|complete|verified|read|write|backdoor|"
                       "candidate|saved|dump"),
        QRegularExpression::CaseInsensitiveOption);
    return !meaningful.match(line).hasMatch();
}

/** Table rules such as "[+] ----+----+----". */
bool isSeparator(const QString &line)
{
    QString body = line;
    static const QRegularExpression prefix(QStringLiteral("^\\s*\\[[+\\-=!?*]\\]\\s*"));
    body.remove(prefix);
    if (body.isEmpty())
        return true;
    static const QRegularExpression separator(QStringLiteral("^[-=+|\\s]+$"));
    return separator.match(body).hasMatch();
}

bool isNoise(const QString &line)
{
    if (line.isEmpty())
        return true;
    if (isSeparator(line))
        return true;
    static const QRegularExpression dots(QStringLiteral("^[.\\s]+$"));
    if (dots.match(line).hasMatch())
        return true;
    static const QRegularExpression prompt(
        QStringLiteral("^\\s*(press|hit)\\s+(any\\s+)?(key|enter)\\b"),
        QRegularExpression::CaseInsensitiveOption);
    return prompt.match(line).hasMatch();
}

/** Keep lines that carry information listed in the Stage 3.1 brief. */
bool hasSignal(const QString &line)
{
    if (line.startsWith(QLatin1Char('[')))
        return true;
    if (line.contains(QLatin1Char('|')))
        return true;
    static const QStringList keywords = {
        QStringLiteral("uid"), QStringLiteral("atqa"), QStringLiteral("sak"),
        QStringLiteral("card"), QStringLiteral("tag"), QStringLiteral("mifare"),
        QStringLiteral("fm11"), QStringLiteral("gen3"), QStringLiteral("iso"),
        QStringLiteral("block"), QStringLiteral("blk"), QStringLiteral("sector"),
        QStringLiteral("key"), QStringLiteral("nonce"), QStringLiteral("backdoor"),
        QStringLiteral("par"), QStringLiteral("found"), QStringLiteral("verified"),
        QStringLiteral("fail"), QStringLiteral("error"), QStringLiteral("timeout"),
        QStringLiteral("done"), QStringLiteral("complete"), QStringLiteral("success"),
        QStringLiteral("abort"), QStringLiteral("cancel"), QStringLiteral("saved"),
        QStringLiteral("read"), QStringLiteral("write"), QStringLiteral("load"),
        QStringLiteral("save"), QStringLiteral("dump"), QStringLiteral("attack"),
        QStringLiteral("candidate"), QStringLiteral("attempt"), QStringLiteral("search"),
        QStringLiteral("wait"), QStringLiteral("using"), QStringLiteral("start"),
        QStringLiteral("stop"), QStringLiteral("progress")
    };
    const QString lower = line.toLower();
    for (const QString &keyword : keywords)
    {
        if (lower.contains(keyword))
            return true;
    }
    static const QRegularExpression data(
        QStringLiteral("[0-9A-Fa-f]{8,}"));
    return data.match(line).hasMatch();
}
} // namespace

void Pm3LogFilter::beginTask(quint64 serial)
{
    m_serial = serial;
    m_active = true;
    m_pending.clear();
    m_seen.clear();
    m_emitted = 0;
    m_capNoted = false;
}

void Pm3LogFilter::endTask()
{
    m_active = false;
    m_pending.clear();
}

void Pm3LogFilter::suspend()
{
    m_active = false;
    m_pending.clear();
}

bool Pm3LogFilter::accepts(quint64 serial, bool cancelled) const
{
    return m_active && serial == m_serial && !cancelled;
}

bool Pm3LogFilter::shouldAutoScroll(int value, int maximum)
{
    // Two pixels of tolerance covers the rounding of a fully scrolled view.
    return value >= maximum - 2;
}

bool Pm3LogFilter::keepLine(const QString &line) const
{
    if (isNoise(line))
        return false;
    if (isProgressOnly(line))
        return false;
    return hasSignal(line);
}

QStringList Pm3LogFilter::takeLine(const QString &line)
{
    QStringList result;
    QString value = line;
    value.remove(QLatin1Char('\r'));
    value.remove(QLatin1Char('\n'));
    static const QRegularExpression controls(
        QStringLiteral("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F\\x7F]"));
    value.remove(controls);
    value = value.trimmed();
    if (value.isEmpty())
        return result;

    if (!keepLine(value))
        return result;
    if (m_emitted >= kMaxLinesPerTask && !isImportant(value))
    {
        if (!m_capNoted)
        {
            m_capNoted = true;
            result << QStringLiteral("(further PM3 output suppressed for this task)");
        }
        return result;
    }
    if (m_seen.contains(value))
        return result;

    m_seen.insert(value);
    ++m_emitted;
    result << value;
    return result;
}

QStringList Pm3LogFilter::feed(quint64 serial, const QString &chunk, bool cancelled)
{
    if (cancelled)
    {
        // Cancel freezes this task: discard the partial line and stop here so
        // a late chunk can never surface under the next task.
        suspend();
        return QStringList();
    }
    if (!accepts(serial, false) || chunk.isEmpty())
        return QStringList();

    QString text = chunk;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    m_pending += text;

    QStringList lines;
    int start = 0;
    while (true)
    {
        const int newline = m_pending.indexOf(QLatin1Char('\n'), start);
        if (newline < 0)
            break;
        lines << m_pending.mid(start, newline - start);
        start = newline + 1;
    }
    if (start > 0)
        m_pending.remove(0, start);
    if (m_pending.size() > 4096)
    {
        // A single unterminated line cannot be useful forever.
        m_pending = m_pending.right(512);
    }

    QStringList output;
    for (const QString &line : lines)
    {
        const QStringList kept = takeLine(line);
        for (const QString &value : kept)
        {
            output << value;
        }
    }
    return output;
}

QStringList Pm3LogFilter::flush(quint64 serial)
{
    if (!m_active || serial != m_serial)
        return QStringList();
    const QString pending = m_pending;
    m_pending.clear();
    if (pending.trimmed().isEmpty())
        return QStringList();
    return takeLine(pending);
}

} // namespace mfgen3
