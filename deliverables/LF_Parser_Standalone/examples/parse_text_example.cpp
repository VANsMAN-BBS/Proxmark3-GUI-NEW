#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

#include "lf/lfparser.h"
#include "lf/lfparsetypes.h"
#include "lf/t55xxparser.h"

namespace {

QString readInput(const QStringList &arguments)
{
    if (arguments.size() > 1) {
        QFile file(arguments.at(1));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream(stderr) << "cannot open " << arguments.at(1) << "\n";
            return QString();
        }
        return QString::fromUtf8(file.readAll());
    }

    QFile input;
    if (!input.open(stdin, QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(input.readAll());
}

void printExtraFields(QTextStream &out, const LFParseResult &result)
{
    for (const LfExtraField &field : result.extraFields)
        out << "  " << field.name << " = " << field.value << "\n";
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    const QString text = readInput(app.arguments());
    if (text.trimmed().isEmpty()) {
        QTextStream(stderr) << "usage: lf_parse_example [client-output.txt] < output.txt\n";
        return 2;
    }

    const LFParseResult result = LFParserRegistry::instance().parse(text);

    QTextStream out(stdout);
    out << "protocol   : " << Lf::protocolName(result.protocol) << "\n";
    out << "status     : " << Lf::statusName(result.status) << "\n";
    out << "confidence : " << Lf::confidenceName(result.confidence) << "\n";
    out << "format     : " << result.format << "\n";
    if (result.bitLength.isPresent())
        out << "bit length : " << result.bitLength.value() << "\n";
    if (result.facilityCode.isPresent())
        out << "facility   : " << result.facilityCode.value() << "\n";
    if (result.cardNumber.isPresent())
        out << "card number: " << result.cardNumber.value() << "\n";
    if (!result.rawData.isEmpty())
        out << "raw        : " << result.rawData << "\n";
    if (!result.hexId.isEmpty())
        out << "hex id     : " << result.hexId << "\n";
    if (result.decimalId.isPresent())
        out << "decimal id : " << result.decimalId.value() << "\n";

    if (!result.extraFields.isEmpty()) {
        out << "extra      :\n";
        printExtraFields(out, result);
    }
    for (const QString &warning : result.warnings)
        out << "warning    : " << warning << "\n";

    // T55xx output is parsed by a separate, read-only API.
    if (text.contains(QStringLiteral("T55xx"), Qt::CaseInsensitive)
            || text.contains(QStringLiteral("T55x7"), Qt::CaseInsensitive)) {
        const LfT55xx::DetectInfo chip = LfT55xx::parseDetect(text);
        if (chip.detected) {
            out << "t55xx      : " << chip.chipType << "\n";
            if (chip.block0.isPresent())
                out << "block0     : " << chip.block0Hex << "\n";
        }
    }

    return result.status == Lf::Status::Identified ? 0 : 1;
}
