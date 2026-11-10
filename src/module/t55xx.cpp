#include "t55xx.h"

#include <QMessageBox>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QStringList>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>

T55xx::T55xx(Ui::MainWindow *ui, Util *util, QWidget *parent)
    : QObject(parent), ui(ui), util(util), parent(parent)
{
    populateCombos();
    disableUnimplementedControls();

    connect(ui->lineEdit_3, &QLineEdit::textChanged, this, &T55xx::onBlock0HexChanged);
    connect(ui->lineEdit_4, &QLineEdit::textChanged, this, &T55xx::onBlock0BinChanged);
    connect(ui->pushButton, &QPushButton::clicked, this, &T55xx::onBlock0GetFromData);
    connect(ui->pushButton_2, &QPushButton::clicked, this, &T55xx::onBlock0SetToData);

    connect(ui->lineEdit_5, &QLineEdit::textChanged, this, &T55xx::onOptionHexChanged);
    connect(ui->lineEdit_6, &QLineEdit::textChanged, this, &T55xx::onOptionBinChanged);
    connect(ui->pushButton_3, &QPushButton::clicked, this, &T55xx::onOptionGetFromData);
    connect(ui->pushButton_4, &QPushButton::clicked, this, &T55xx::onOptionSetToData);

    connect(ui->T55_detectButton, &QPushButton::clicked, this, &T55xx::detect);
    connect(ui->T55_infoButton, &QPushButton::clicked, this, &T55xx::readInfo);
    connect(ui->T55_applyConfigButton, &QPushButton::clicked, this, &T55xx::applyConfig);

    connect(ui->T55_cloneEM410xReadButton, &QPushButton::clicked, this, &T55xx::onReadEM410x);
    connect(ui->T55_cloneEM410xCloneButton, &QPushButton::clicked, this, &T55xx::onCloneEM410x);

    // Start with a valid, editable zero block so the local editor is usable
    // even when the LF firmware is unavailable.
    updatingFields = true;
    ui->lineEdit_3->setText(QStringLiteral("00000000"));
    ui->lineEdit_4->setText(block0ToBinary(0));
    updatingFields = false;
}

void T55xx::setConfigMap(const QVariantMap &configMap)
{
    this->configMap = configMap;
}

void T55xx::populateCombos()
{
    struct ModItem { const char *name; quint32 value; };
    static const ModItem mods[] = {
        {"ASK", 0x08}, {"FSK1", 0x04}, {"FSK2", 0x05}, {"FSK1a", 0x06},
        {"FSK2a", 0x07}, {"PSK1", 0x01}, {"PSK2", 0x02}, {"PSK3", 0x03},
        {"NRZ", 0x00}, {"Biphase", 0x10}, {"Biphase inverted", 0x18}
    };

    struct RateItem { const char *name; quint32 value; };
    static const RateItem rates[] = {
        {"RF/8", 0}, {"RF/16", 1}, {"RF/32", 2}, {"RF/40", 3},
        {"RF/50", 4}, {"RF/64", 5}, {"RF/100", 6}, {"RF/128", 7}
    };

    const QList<QComboBox *> modulationBoxes = {ui->comboBox_2, ui->comboBox_12};
    for(QComboBox *box : modulationBoxes)
    {
        box->clear();
        for(const ModItem &item : mods)
            box->addItem(QString::fromLatin1(item.name), item.value);
    }

    const QList<QComboBox *> rateBoxes = {ui->comboBox, ui->comboBox_13};
    for(QComboBox *box : rateBoxes)
    {
        box->clear();
        for(const RateItem &item : rates)
            box->addItem(QString::fromLatin1(item.name), item.value);
    }

    ui->spinBox_2->setRange(0, 7);
    ui->spinBox_4->setRange(0, 255);
}

void T55xx::disableUnimplementedControls()
{
    const QString tip = tr("Not implemented in this version. The control is kept for UI compatibility and is marked Experimental.");

    // T55x7 block 0 fields whose bit mapping is not verified in this build.
    const QList<QWidget *> unsupported = {
        ui->checkBox,     // Locked
        ui->spinBox,      // Master Key
        ui->comboBox_3,   // PSK clock frequency
        ui->checkBox_9,   // Answer on request
        ui->checkBox_10,  // One time pad
        ui->checkBox_6    // Sequence start marker
    };
    for(QWidget *w : unsupported)
    {
        w->setEnabled(false);
        w->setToolTip(tip);
    }

    // Page 1 block 3 option fields: only raw hex/bin conversion is implemented.
    const QList<QWidget *> optionWidgets = ui->groupBox_3->findChildren<QWidget *>();
    for(QWidget *w : optionWidgets)
    {
        if(qobject_cast<QComboBox *>(w) || qobject_cast<QCheckBox *>(w) ||
           qobject_cast<QSpinBox *>(w) || qobject_cast<QRadioButton *>(w))
        {
            w->setEnabled(false);
            w->setToolTip(tip);
        }
    }

    ui->T55_cloneGroupBox->setToolTip(tr("Experimental. The current firmware has no LF support, so this is not hardware-verified."));
    ui->groupBox_4->setToolTip(tr("Experimental. Commands are verified against Iceman v4.23346 help, but the attached firmware lacks LF."));
}

QString T55xx::normalizedHex(const QString &text, int digits)
{
    QString value = text;
    value.remove(QRegularExpression(QStringLiteral("\\s")));
    value = value.toUpper();
    if(value.size() != digits)
        return QString();
    for(const QChar ch : value)
    {
        if(!((ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
             (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'))))
            return QString();
    }
    return value;
}

bool T55xx::parseHex32(const QString &text, quint32 *value)
{
    const QString hex = normalizedHex(text, 8);
    if(hex.isEmpty())
        return false;
    bool ok = false;
    const quint32 parsed = hex.toUInt(&ok, 16);
    if(ok && value)
        *value = parsed;
    return ok;
}

QString T55xx::block0ToBinary(quint32 block0)
{
    QString bits = QString::number(block0, 2).rightJustified(32, QLatin1Char('0'));
    QStringList groups;
    for(int i = 0; i < 32; i += 4)
        groups.append(bits.mid(i, 4));
    return groups.join(QLatin1Char(' '));
}

bool T55xx::binaryToBlock0(const QString &text, quint32 *value)
{
    QString bits = text;
    bits.remove(QRegularExpression(QStringLiteral("\\s")));
    if(bits.size() != 32)
        return false;
    for(const QChar ch : bits)
    {
        if(ch != QLatin1Char('0') && ch != QLatin1Char('1'))
            return false;
    }
    bool ok = false;
    const quint32 parsed = bits.toUInt(&ok, 2);
    if(ok && value)
        *value = parsed;
    return ok;
}

void T55xx::setModulationCombo(quint32 demod)
{
    QSignalBlocker blocker(ui->comboBox_2);
    const int index = ui->comboBox_2->findData(demod);
    if(index >= 0)
        ui->comboBox_2->setCurrentIndex(index);
}

quint32 T55xx::modulationFromCombo() const
{
    return ui->comboBox_2->currentData().toUInt();
}

void T55xx::setBitrateCombo(quint32 rateIndex)
{
    QSignalBlocker blocker(ui->comboBox);
    const int index = ui->comboBox->findData(rateIndex);
    if(index >= 0)
        ui->comboBox->setCurrentIndex(index);
}

quint32 T55xx::bitrateFromCombo() const
{
    return ui->comboBox->currentData().toUInt();
}

void T55xx::updateBlock0Display(quint32 block0)
{
    if(updatingFields)
        return;
    updatingFields = true;

    const bool extend = ((block0 >> 17) & 0x01) != 0;
    const quint32 dbr = (block0 >> 18) & (extend ? 0x3F : 0x07);

    setModulationCombo((block0 >> 12) & 0x1F);
    setBitrateCombo(dbr);
    ui->checkBox_2->setChecked(extend);
    ui->spinBox_2->setValue((block0 >> 5) & 0x07);
    ui->checkBox_8->setChecked(((block0 >> 4) & 0x01) != 0);
    ui->checkBox_7->setChecked(((block0 >> 3) & 0x01) != 0);
    ui->checkBox_5->setChecked(((block0 >> 2) & 0x01) != 0);
    ui->checkBox_4->setChecked(((block0 >> 1) & 0x01) != 0);
    ui->checkBox_3->setChecked((block0 & 0x01) != 0);

    QSignalBlocker binBlocker(ui->lineEdit_4);
    ui->lineEdit_4->setText(block0ToBinary(block0));

    updatingFields = false;
}

quint32 T55xx::block0FromUi() const
{
    quint32 block0 = 0;
    parseHex32(ui->lineEdit_3->text(), &block0);

    const quint32 modulationMask = 0x1F << 12;
    const bool extend = ui->checkBox_2->isChecked();
    const quint32 dbrMask = (extend ? 0x3F : 0x07) << 18;
    const quint32 maxBlockMask = 0x07 << 5;

    block0 &= ~(modulationMask | dbrMask | maxBlockMask | 0x1F);
    block0 |= (modulationFromCombo() & 0x1F) << 12;
    block0 |= (bitrateFromCombo() & (extend ? 0x3F : 0x07)) << 18;
    block0 |= (quint32(ui->spinBox_2->value()) & 0x07) << 5;
    if(extend)
        block0 |= (1u << 17);
    if(ui->checkBox_8->isChecked())
        block0 |= (1u << 4);
    if(ui->checkBox_7->isChecked())
        block0 |= (1u << 3);
    if(ui->checkBox_5->isChecked())
        block0 |= (1u << 2);
    if(ui->checkBox_4->isChecked())
        block0 |= (1u << 1);
    if(ui->checkBox_3->isChecked())
        block0 |= 1u;
    return block0;
}

void T55xx::onBlock0HexChanged()
{
    if(updatingFields)
        return;
    quint32 block0 = 0;
    if(!parseHex32(ui->lineEdit_3->text(), &block0))
        return;
    updateBlock0Display(block0);
}

void T55xx::onBlock0BinChanged()
{
    if(updatingFields)
        return;
    quint32 block0 = 0;
    if(!binaryToBlock0(ui->lineEdit_4->text(), &block0))
        return;
    updatingFields = true;
    ui->lineEdit_3->setText(QStringLiteral("%1").arg(block0, 8, 16, QLatin1Char('0')).toUpper());
    updatingFields = false;
    updateBlock0Display(block0);
}

void T55xx::onBlock0GetFromData()
{
    quint32 block0 = 0;
    if(!parseHex32(ui->lineEdit_3->text(), &block0))
    {
        QMessageBox::information(parent, tr("Info"), tr("Block 0 must be 8 hex digits."));
        return;
    }
    updateBlock0Display(block0);
}

void T55xx::onBlock0SetToData()
{
    const quint32 block0 = block0FromUi();
    updatingFields = true;
    ui->lineEdit_3->setText(QStringLiteral("%1").arg(block0, 8, 16, QLatin1Char('0')).toUpper());
    ui->lineEdit_4->setText(block0ToBinary(block0));
    updatingFields = false;
}

void T55xx::onOptionHexChanged()
{
    if(updatingFields)
        return;
    quint32 option = 0;
    if(!parseHex32(ui->lineEdit_5->text(), &option))
        return;
    updatingFields = true;
    ui->lineEdit_6->setText(block0ToBinary(option));
    updatingFields = false;
}

void T55xx::onOptionBinChanged()
{
    if(updatingFields)
        return;
    quint32 option = 0;
    if(!binaryToBlock0(ui->lineEdit_6->text(), &option))
        return;
    updatingFields = true;
    ui->lineEdit_5->setText(QStringLiteral("%1").arg(option, 8, 16, QLatin1Char('0')).toUpper());
    updatingFields = false;
}

void T55xx::onOptionGetFromData()
{
    quint32 option = 0;
    if(!parseHex32(ui->lineEdit_5->text(), &option))
    {
        QMessageBox::information(parent, tr("Info"), tr("Option block must be 8 hex digits."));
        return;
    }
    updatingFields = true;
    ui->lineEdit_6->setText(block0ToBinary(option));
    updatingFields = false;
}

void T55xx::onOptionSetToData()
{
    quint32 option = 0;
    if(!binaryToBlock0(ui->lineEdit_6->text(), &option))
    {
        QMessageBox::information(parent, tr("Info"), tr("Option block must be 32 binary digits."));
        return;
    }
    updatingFields = true;
    ui->lineEdit_5->setText(QStringLiteral("%1").arg(option, 8, 16, QLatin1Char('0')).toUpper());
    updatingFields = false;
}

void T55xx::detect()
{
    const QString output = util->execCMDWithOutput(QStringLiteral("lf t55xx detect"), 30000, true);
    Util::gotoRawTab();
    if(output.isEmpty())
    {
        QMessageBox::information(parent, tr("T55xx"),
                                 tr("No response from `lf t55xx detect`. The current firmware may not include LF support."));
        return;
    }
    if(output.contains(QStringLiteral("not available"), Qt::CaseInsensitive))
    {
        QMessageBox::information(parent, tr("T55xx"),
                                 tr("`lf t55xx detect` is not available on the connected firmware.\n"
                                    "This is expected on a 256K build without LF; the page remains Experimental."));
        return;
    }

    const QString upper = output.toUpper();
    struct ModToken { const char *token; quint32 value; };
    static const ModToken tokens[] = {
        {"FSK1A", 0x06}, {"FSK2A", 0x07}, {"FSK1", 0x04}, {"FSK2", 0x05},
        {"PSK1", 0x01}, {"PSK2", 0x02}, {"PSK3", 0x03}, {"ASK", 0x08},
        {"BIPHASE", 0x10}, {"NRZ", 0x00}
    };
    for(const ModToken &token : tokens)
    {
        if(upper.contains(QString::fromLatin1(token.token)))
        {
            const int index = ui->comboBox_12->findData(token.value);
            if(index >= 0)
                ui->comboBox_12->setCurrentIndex(index);
            break;
        }
    }

    QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(?:BIT\\s*RATE|RATE)[^0-9]*(\\d+)"),
                                                       QRegularExpression::CaseInsensitiveOption).match(output);
    if(match.hasMatch())
    {
        static const int rateValues[] = {8, 16, 32, 40, 50, 64, 100, 128};
        const int rate = match.captured(1).toInt();
        for(int i = 0; i < 8; i++)
        {
            if(rateValues[i] == rate)
            {
                const int index = ui->comboBox_13->findData(i);
                if(index >= 0)
                    ui->comboBox_13->setCurrentIndex(index);
                break;
            }
        }
    }

    match = QRegularExpression(QStringLiteral("OFFSET[^0-9]*(\\d+)"),
                               QRegularExpression::CaseInsensitiveOption).match(output);
    if(match.hasMatch())
        ui->spinBox_4->setValue(match.captured(1).toInt());

    match = QRegularExpression(QStringLiteral("INVERTED[^A-Z0-9]*(YES|NO|TRUE|FALSE|1|0)"),
                               QRegularExpression::CaseInsensitiveOption).match(output);
    if(match.hasMatch())
    {
        const QString value = match.captured(1).toUpper();
        ui->checkBox_12->setChecked(value == QLatin1String("YES") || value == QLatin1String("TRUE") || value == QLatin1String("1"));
    }
}

void T55xx::readInfo()
{
    const QString output = util->execCMDWithOutput(QStringLiteral("lf t55xx info"), 30000, true);
    Util::gotoRawTab();
    if(output.isEmpty())
    {
        QMessageBox::information(parent, tr("T55xx"),
                                 tr("No response from `lf t55xx info`. The current firmware may not include LF support."));
        return;
    }
    if(output.contains(QStringLiteral("not available"), Qt::CaseInsensitive))
    {
        QMessageBox::information(parent, tr("T55xx"),
                                 tr("`lf t55xx info` is not available on the connected firmware.\n"
                                    "The page remains Experimental."));
        return;
    }

    QRegularExpressionMatch match = QRegularExpression(
        QStringLiteral("BLOCK\\s*0[^0-9A-Fa-f]*([0-9A-Fa-f]{8})"),
        QRegularExpression::CaseInsensitiveOption).match(output);
    if(!match.hasMatch())
        match = QRegularExpression(QStringLiteral("\\b([0-9A-Fa-f]{8})\\b")).match(output);
    if(!match.hasMatch())
    {
        QMessageBox::information(parent, tr("T55xx"),
                                 tr("Could not parse block 0 from `lf t55xx info`. See the RawCommand output."));
        return;
    }

    bool ok = false;
    const quint32 block0 = match.captured(1).toUInt(&ok, 16);
    if(!ok)
        return;
    updatingFields = true;
    ui->lineEdit_3->setText(match.captured(1).toUpper());
    ui->lineEdit_4->setText(block0ToBinary(block0));
    updatingFields = false;
    updateBlock0Display(block0);
}

void T55xx::applyConfig()
{
    QStringList args;
    const quint32 demod = ui->comboBox_12->currentData().toUInt();
    switch(demod)
    {
    case 0x08: args << QStringLiteral("--ASK"); break;
    case 0x04: args << QStringLiteral("--FSK1"); break;
    case 0x05: args << QStringLiteral("--FSK2"); break;
    case 0x06: args << QStringLiteral("--FSK1A"); break;
    case 0x07: args << QStringLiteral("--FSK2A"); break;
    case 0x01: args << QStringLiteral("--PSK1"); break;
    case 0x02: args << QStringLiteral("--PSK2"); break;
    case 0x03: args << QStringLiteral("--PSK3"); break;
    case 0x10: args << QStringLiteral("--BI"); break;
    case 0x18: args << QStringLiteral("--BIA"); break;
    case 0x00: args << QStringLiteral("--NRZ"); break;
    default: break;
    }

    const int rate = ui->comboBox_13->currentData().toInt();
    if(rate >= 0)
        args << QStringLiteral("--rate") << QString::number(rate);
    args << QStringLiteral("-o") << QString::number(ui->spinBox_4->value());
    if(ui->checkBox_12->isChecked())
        args << QStringLiteral("-i");
    if(ui->checkBox_13->isChecked())
        args << QStringLiteral("--st");
    if(ui->radioButton_2->isChecked())
        args << QStringLiteral("--q5");

    const QString cmd = QStringLiteral("lf t55xx config ") + args.join(QLatin1Char(' '));
    util->execCMD(cmd);
    Util::gotoRawTab();
}

void T55xx::onReadEM410x()
{
    const QString output = util->execCMDWithOutput(QStringLiteral("lf em 410x reader"), 6000, true);
    const QRegularExpressionMatch match = QRegularExpression(
        QStringLiteral("EM\\s*410x\\s*ID\\s*[:=]?\\s*([0-9A-Fa-f]{10})"),
        QRegularExpression::CaseInsensitiveOption).match(output);
    if(match.hasMatch())
        ui->T55_cloneEM410xIDEdit->setText(match.captured(1).toUpper());
}

void T55xx::onCloneEM410x()
{
    const QString id = normalizedHex(ui->T55_cloneEM410xIDEdit->text(), 10);
    if(id.isEmpty())
    {
        QMessageBox::information(parent, tr("T55xx"), tr("EM410x ID must be 10 hex digits."));
        return;
    }

    const QMessageBox::StandardButton choice = QMessageBox::warning(
        parent, tr("Clone to T55xx"),
        tr("This writes the EM410x ID to a T55xx tag. The operation is Experimental and not verified on this firmware.\n\nContinue?"),
        QMessageBox::Yes | QMessageBox::No);
    if(choice != QMessageBox::Yes)
        return;

    QString cmd = QStringLiteral("lf em 410x clone --id ") + id;
    if(ui->T55_cloneT5555Button->isChecked())
        cmd += QStringLiteral(" --q5");
    util->execCMD(cmd);
    Util::gotoRawTab();
}
