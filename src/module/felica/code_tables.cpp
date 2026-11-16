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

QString equipmentLabel(int code, Transit::LabelLanguage language)
{
    if (language == Transit::LabelLanguage::Japanese)
        return equipmentTable().value(code, unknown("Unknown equipment", code));
    const bool zh = (language == Transit::LabelLanguage::Chinese);
    switch (code)
    {
    case 0x00: return zh ? QStringLiteral("未定义") : QStringLiteral("Undefined");
    case 0x05: return zh ? QStringLiteral("公交车载机") : QStringLiteral("Bus onboard terminal");
    case 0x07: return zh ? QStringLiteral("发卡机") : QStringLiteral("Card vending machine");
    case 0x08: return zh ? QStringLiteral("自动售票机") : QStringLiteral("Ticket vending machine");
    case 0x16: return zh ? QStringLiteral("自动检票机") : QStringLiteral("Automatic ticket gate");
    case 0x17: return zh ? QStringLiteral("简易检票机") : QStringLiteral("Simplified ticket gate");
    case 0x1B: return zh ? QStringLiteral("移动 FeliCa") : QStringLiteral("Mobile FeliCa");
    case 0x1D: return zh ? QStringLiteral("换乘自动检票机") : QStringLiteral("Transfer ticket gate");
    case 0x1F: return zh ? QStringLiteral("充值机") : QStringLiteral("Top-up machine");
    case 0xC7: return zh ? QStringLiteral("销售终端 (POS)") : QStringLiteral("Point-of-sale terminal");
    case 0xC8: return zh ? QStringLiteral("自动售货机") : QStringLiteral("Vending machine");
    case 0xC9: return zh ? QStringLiteral("非规范终端 (0xC9)") : QStringLiteral("Non-standard terminal (0xC9)");
    case 0xCA: return zh ? QStringLiteral("非规范终端 (0xCA)") : QStringLiteral("Non-standard terminal (0xCA)");
    default: break;
    }
    return zh ? QStringLiteral("未知终端 (0x%1)").arg(QString::number(code, 16).toUpper().rightJustified(2, QLatin1Char('0')))
              : QStringLiteral("Unknown terminal (0x%1)").arg(QString::number(code, 16).toUpper().rightJustified(2, QLatin1Char('0')));
}

QString transactionLabel(int code, Transit::LabelLanguage language)
{
    const int value = code & 0x7F;
    if (language == Transit::LabelLanguage::Japanese)
        return transactionTable().value(value, unknown("Unknown transaction", value));
    const bool zh = (language == Transit::LabelLanguage::Chinese);
    switch (value)
    {
    case 0x00: return zh ? QStringLiteral("未定义") : QStringLiteral("Undefined");
    case 0x01: return zh ? QStringLiteral("自动检票机出场（支付车费）") : QStringLiteral("Automatic gate exit (fare payment)");
    case 0x02: return zh ? QStringLiteral("SF 充值") : QStringLiteral("SF top-up");
    case 0x05: return zh ? QStringLiteral("超程补票") : QStringLiteral("Fare adjustment");
    case 0x0D: return zh ? QStringLiteral("公交均一票价") : QStringLiteral("Bus flat fare");
    case 0x0F: return zh ? QStringLiteral("公交") : QStringLiteral("Bus");
    case 0x13: return zh ? QStringLiteral("票价出场") : QStringLiteral("Fare exit");
    case 0x14: return zh ? QStringLiteral("自动充值") : QStringLiteral("Auto top-up");
    case 0x46: return zh ? QStringLiteral("购物") : QStringLiteral("Purchase");
    case 0x48: return zh ? QStringLiteral("积分充值") : QStringLiteral("Points top-up");
    case 0x4A: return zh ? QStringLiteral("购物取消") : QStringLiteral("Purchase cancellation");
    case 0x4B: return zh ? QStringLiteral("入场・购物") : QStringLiteral("Entry / purchase");
    default: break;
    }
    return zh ? QStringLiteral("未知交易类型 (0x%1)").arg(QString::number(value, 16).toUpper().rightJustified(2, QLatin1Char('0')))
              : QStringLiteral("Unknown transaction (0x%1)").arg(QString::number(value, 16).toUpper().rightJustified(2, QLatin1Char('0')));
}

QString paymentLabel(int code, Transit::LabelLanguage language)
{
    if (language == Transit::LabelLanguage::Japanese)
        return paymentTable().value(code, unknown("Unknown payment", code));
    const bool zh = (language == Transit::LabelLanguage::Chinese);
    switch (code)
    {
    case 0x00: return zh ? QStringLiteral("现金 / 无") : QStringLiteral("Cash / None");
    case 0x02: return QStringLiteral("VIEW");
    case 0x0B: return QStringLiteral("PiTaPa");
    case 0x0D: return zh ? QStringLiteral("PASMO 自动充值") : QStringLiteral("PASMO auto top-up");
    case 0x3F: return zh ? QStringLiteral("Mobile Suica（非 VIEW）") : QStringLiteral("Mobile Suica (non-VIEW)");
    default: break;
    }
    return zh ? QStringLiteral("未知支付方式 (0x%1)").arg(QString::number(code, 16).toUpper().rightJustified(2, QLatin1Char('0')))
              : QStringLiteral("Unknown payment (0x%1)").arg(QString::number(code, 16).toUpper().rightJustified(2, QLatin1Char('0')));
}

QString gateInstructionLabel(int code, Transit::LabelLanguage language)
{
    if (language == Transit::LabelLanguage::Japanese)
        return gateInstructionTable().value(code, unknown("Unknown gate instruction", code));
    const bool zh = (language == Transit::LabelLanguage::Chinese);
    switch (code)
    {
    case 0x00: return zh ? QStringLiteral("不适用 / 未定义") : QStringLiteral("Not applicable / undefined");
    case 0x01: return zh ? QStringLiteral("入场") : QStringLiteral("Entry");
    case 0x02: return zh ? QStringLiteral("入场 / 出场") : QStringLiteral("Entry / Exit");
    case 0x03: return zh ? QStringLiteral("定期入场 / 出场") : QStringLiteral("Commuter pass entry / exit");
    case 0x04: return zh ? QStringLiteral("入场 / 定期出场") : QStringLiteral("Entry / commuter pass exit");
    case 0x05: return zh ? QStringLiteral("未公开（推测为换乘 / 联络）") : QStringLiteral("Undisclosed (transfer assumed)");
    case 0x0E: return zh ? QStringLiteral("窗口出场") : QStringLiteral("Staffed gate exit");
    case 0x0F: return zh ? QStringLiteral("入场 / 出场（公交）") : QStringLiteral("Entry / Exit (bus)");
    default: break;
    }
    return zh ? QStringLiteral("未知改札处理 (0x%1)").arg(QString::number(code, 16).toUpper().rightJustified(2, QLatin1Char('0')))
              : QStringLiteral("Unknown gate instruction (0x%1)").arg(QString::number(code, 16).toUpper().rightJustified(2, QLatin1Char('0')));
}

QString gateInOutLabel(int code, Transit::LabelLanguage language)
{
    if (language == Transit::LabelLanguage::Japanese)
        return gateInOutTable().value(code, unknown("Unknown gate type", code));
    const bool zh = (language == Transit::LabelLanguage::Chinese);
    switch (code)
    {
    case 0x00: return zh ? QStringLiteral("精算出场") : QStringLiteral("Fare adjustment exit");
    case 0x20: return zh ? QStringLiteral("出场") : QStringLiteral("Exit");
    case 0x21: return zh ? QStringLiteral("站务设备出场") : QStringLiteral("Staffed equipment exit");
    case 0x40: return zh ? QStringLiteral("定期出场") : QStringLiteral("Commuter pass exit");
    case 0xA0: return zh ? QStringLiteral("入场") : QStringLiteral("Entry");
    case 0xC0: return zh ? QStringLiteral("定期入场") : QStringLiteral("Commuter pass entry");
    default: break;
    }
    return zh ? QStringLiteral("未知改札类型 (0x%1)").arg(QString::number(code, 16).toUpper().rightJustified(2, QLatin1Char('0')))
              : QStringLiteral("Unknown gate type (0x%1)").arg(QString::number(code, 16).toUpper().rightJustified(2, QLatin1Char('0')));
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
