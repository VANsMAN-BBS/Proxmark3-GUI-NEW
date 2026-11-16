#include "code_tables.h"

#include <QHash>
#include <QStringList>

namespace FelicaCodes {
namespace {

QString unknown(const char *kind, int code)
{
    const QString hex = QString::number(code, 16).toUpper()
                            .rightJustified(code > 0xFF ? 4 : 2, QLatin1Char('0'));
    return QStringLiteral("%1 (0x%2)").arg(QString::fromLatin1(kind), hex);
}

const QHash<int, QString> &equipmentTable()
{
    static const QHash<int, QString> table = {
        {0x00, QStringLiteral("未定義")},
        {0x03, QStringLiteral("のりこし精算機")},
        {0x04, QStringLiteral("携帯端末")},
        {0x05, QStringLiteral("バス等車載機")},
        {0x07, QStringLiteral("カード発売機")},
        {0x08, QStringLiteral("自動券売機")},
        {0x09, QStringLiteral("SMART ICOCA クイックチャージ機?")},
        {0x12, QStringLiteral("自動券売機(東京モノレール)")},
        {0x14, QStringLiteral("駅務機器(PASMO発行機?)")},
        {0x15, QStringLiteral("定期券発売機")},
        {0x16, QStringLiteral("自動改札機")},
        {0x17, QStringLiteral("簡易改札機")},
        {0x18, QStringLiteral("駅務機器(発行機?)")},
        {0x19, QStringLiteral("窓口処理機(みどりの窓口)")},
        {0x1A, QStringLiteral("窓口処理機(有人改札)")},
        {0x1B, QStringLiteral("モバイルFeliCa")},
        {0x1C, QStringLiteral("入場券券売機")},
        {0x1D, QStringLiteral("他社乗換自動改札機")},
        {0x1F, QStringLiteral("入金機")},
        {0x20, QStringLiteral("発行機?(モノレール)")},
        {0x22, QStringLiteral("簡易改札機(ことでん)")},
        {0x34, QStringLiteral("カード発売機(せたまる?)")},
        {0x35, QStringLiteral("バス等車載機(せたまる車内入金機?)")},
        {0x36, QStringLiteral("バス等車載機(車内簡易改札機)")},
        {0x46, QStringLiteral("ビューアルッテ端末")},
        {0xC7, QStringLiteral("物販端末(POS)")},
        {0xC8, QStringLiteral("物販端末(自動販売機)")},
        {0xC9, QStringLiteral("物販端末(0xC9, 規範外)")},
        {0xCA, QStringLiteral("物販端末(0xCA, 規範外)")},
    };
    return table;
}

const QHash<int, QString> &transactionTable()
{
    static const QHash<int, QString> table = {
        {0x00, QStringLiteral("未定義")},
        {0x01, QStringLiteral("自動改札機出場(運賃支払)")},
        {0x02, QStringLiteral("SFチャージ")},
        {0x03, QStringLiteral("きっぷ購入")},
        {0x04, QStringLiteral("磁気券精算")},
        {0x05, QStringLiteral("乗越精算")},
        {0x06, QStringLiteral("窓口出場")},
        {0x07, QStringLiteral("新規")},
        {0x08, QStringLiteral("控除")},
        {0x0D, QStringLiteral("バス等均一運賃")},
        {0x0F, QStringLiteral("バス等")},
        {0x11, QStringLiteral("再発行?")},
        {0x13, QStringLiteral("料金出場")},
        {0x14, QStringLiteral("オートチャージ")},
        {0x1F, QStringLiteral("バス等チャージ")},
        {0x23, QStringLiteral("バス・路面電車企画券購入")},
        {0x46, QStringLiteral("物販")},
        {0x48, QStringLiteral("ポイントチャージ")},
        {0x4A, QStringLiteral("物販取消")},
        {0x4B, QStringLiteral("入場・物販")},
    };
    return table;
}

const QHash<int, QString> &paymentTable()
{
    static const QHash<int, QString> table = {
        {0x00, QStringLiteral("現金/なし")},
        {0x02, QStringLiteral("VIEW")},
        {0x0B, QStringLiteral("PiTaPa")},
        {0x0D, QStringLiteral("オートチャージ対応PASMO")},
        {0x3F, QStringLiteral("モバイルSuica(VIEW決済以外)")},
    };
    return table;
}

const QHash<int, QString> &gateInstructionTable()
{
    static const QHash<int, QString> table = {
        {0x00, QStringLiteral("対象外/未定義")},
        {0x01, QStringLiteral("入場")},
        {0x02, QStringLiteral("入場/出場")},
        {0x03, QStringLiteral("定期入場/出場")},
        {0x04, QStringLiteral("入場/定期出場")},
        {0x05, QStringLiteral("（未公開。乗継/連絡扱いと推定）")},
        {0x0E, QStringLiteral("窓口出場")},
        {0x0F, QStringLiteral("入場/出場(バス等)")},
        {0x12, QStringLiteral("料金定期入場/料金出場")},
        {0x17, QStringLiteral("入場/出場(乗継割引)")},
        {0x21, QStringLiteral("入場/出場(バス等乗継割引)")},
    };
    return table;
}

const QHash<int, QString> &gateInOutTable()
{
    static const QHash<int, QString> table = {
        {0x00, QStringLiteral("精算出場")},
        {0x01, QStringLiteral("精算出場(プリペイドカード併用?)")},
        {0x20, QStringLiteral("出場")},
        {0x21, QStringLiteral("駅務機器出場")},
        {0x22, QStringLiteral("割引出場")},
        {0x24, QStringLiteral("割引出場?")},
        {0x40, QStringLiteral("定期出場")},
        {0x80, QStringLiteral("均一区間入場?")},
        {0xA0, QStringLiteral("入場")},
        {0xA2, QStringLiteral("割引入場?")},
        {0xC0, QStringLiteral("定期入場")},
    };
    return table;
}

const QHash<int, QString> &intermediateGateTable()
{
    static const QHash<int, QString> table = {
        {0x00, QStringLiteral("未定義")},
        {0x04, QStringLiteral("乗継割引?")},
        {0x08, QStringLiteral("電車バス乗継割引?")},
        {0x40, QStringLiteral("新幹線中間改札?")},
    };
    return table;
}

const QHash<int, QPair<QString, int> > &serviceTable()
{
    // code -> (access, declared blocks)
    static const QHash<int, QPair<QString, int> > table = {
        {0x004A, qMakePair(QStringLiteral("keyed"), 4)},
        {0x0816, qMakePair(QStringLiteral("keyed"), 1)},
        {0x08CA, qMakePair(QStringLiteral("keyed"), 1)},
        {0x100A, qMakePair(QStringLiteral("keyed"), 10)},
        {0x104A, qMakePair(QStringLiteral("keyed"), 3)},
        {0x008B, qMakePair(QStringLiteral("public"), 1)},
        {0x090F, qMakePair(QStringLiteral("public"), 20)},
        {0x108F, qMakePair(QStringLiteral("public"), 3)},
        {0x10CB, qMakePair(QStringLiteral("public"), 2)},
        {0x184B, qMakePair(QStringLiteral("public"), 0)},
    };
    return table;
}

} // namespace

QString equipmentLabel(int code)
{
    return equipmentTable().value(code, unknown("Unknown equipment", code));
}

QString transactionLabel(int code)
{
    return transactionTable().value(code & 0x7F, unknown("Unknown transaction", code & 0x7F));
}

QString paymentLabel(int code)
{
    return paymentTable().value(code, unknown("Unknown payment", code));
}

QString gateInstructionLabel(int code)
{
    return gateInstructionTable().value(code, unknown("Unknown gate instruction", code));
}

QString gateInOutLabel(int code)
{
    return gateInOutTable().value(code, unknown("Unknown gate type", code));
}

QString intermediateGateLabel(int code)
{
    return intermediateGateTable().value(code, unknown("Unknown intermediate gate", code));
}

QString regionLabel(int area)
{
    switch (area)
    {
    case 0: return QStringLiteral("首都圏(関東)");
    case 1: return QStringLiteral("中部");
    case 2: return QStringLiteral("関西");
    case 3: return QStringLiteral("その他");
    default: break;
    }
    return unknown("Unknown region", area);
}

QString serviceName(int code)
{
    switch (code)
    {
    case 0x004A: return QStringLiteral("発行情報");
    case 0x0816: return QStringLiteral("その他（用途未確定）");
    case 0x08CA: return QStringLiteral("最終チャージ情報");
    case 0x100A: return QStringLiteral("拡張情報");
    case 0x104A: return QStringLiteral("定期情報");
    case 0x008B: return QStringLiteral("属性情報");
    case 0x090F: return QStringLiteral("取引履歴");
    case 0x108F: return QStringLiteral("改札入出場情報");
    case 0x10CB: return QStringLiteral("SF改札入場情報");
    case 0x184B: return QStringLiteral("料金発券・改札情報");
    default: break;
    }
    return unknown("Unknown service", code);
}

QString serviceAccess(int code)
{
    return serviceTable().value(code).first;
}

int serviceDeclaredBlocks(int code)
{
    return serviceTable().value(code).second;
}

Transit::TransactionKind kindForTransaction(int transactionCode)
{
    static const QVector<int> goods = {0x46, 0x48, 0x4A, 0x4B};
    static const QVector<int> bus = {0x0D, 0x0F, 0x1F, 0x23};
    const int code = transactionCode & 0x7F;
    if (goods.contains(code))
        return Transit::TransactionKind::Goods;
    if (bus.contains(code))
        return Transit::TransactionKind::Bus;
    if (code == 0x00)
        return Transit::TransactionKind::Unknown;
    return Transit::TransactionKind::Rail;
}

} // namespace FelicaCodes
