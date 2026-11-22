#include "mifare.h"
#include "core/clientsession.h"
#include "module/mifare/mfcardinfoservice.h"
#include "module/mifare/mfcardtypesel.h"
#include "module/mifare/mfgen3crypto.h"
#include "module/mifare/mfgen3service.h"
#include "module/mifare/mfimage.h"
#include "module/mifare/mfnoncestore.h"
#include "module/mifare/mftablelayout.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScrollBar>
#include <QThread>
#include <QAtomicInt>

namespace {
QString normalizeHex(const QString &text, int byteCount)
{
    QString value = text;
    value.remove(QRegularExpression(QStringLiteral("\\s")));
    value = value.toUpper();
    if(value.size() != byteCount * 2)
        return QString();
    for(const QChar ch : value)
    {
        const bool isHex = (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
                           (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'));
        if(!isHex && ch != QLatin1Char('?'))
            return QString();
    }
    return value;
}

bool hexStringToBytes(const QString &text, QByteArray &out, bool allowUnknown)
{
    out.clear();
    for(int i = 0; i < text.size(); i += 2)
    {
        const QChar high = text.at(i);
        const QChar low = text.at(i + 1);
        if(high == QLatin1Char('?') || low == QLatin1Char('?'))
        {
            if(!allowUnknown)
                return false;
            out.append(char(0));
            continue;
        }
        int h = 0;
        int l = 0;
        if(high >= QLatin1Char('0') && high <= QLatin1Char('9'))
            h = high.unicode() - '0';
        else
            h = high.unicode() - 'A' + 10;
        if(low >= QLatin1Char('0') && low <= QLatin1Char('9'))
            l = low.unicode() - '0';
        else
            l = low.unicode() - 'A' + 10;
        out.append(char((h << 4) | l));
    }
    return true;
}
} // namespace

const Mifare::CardType Mifare::card_mini =
{
    0,
    5,
    20,
    {4, 4, 4, 4, 4},
    {0, 4, 8, 12, 16},
    "mini"
};
const Mifare::CardType Mifare::card_1k =
{
    1,
    16,
    64,
    {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4},
    {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60},
    "1k"
};
const Mifare::CardType Mifare::card_2k =
{
    2,
    32,
    128,
    {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4},
    {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124},
    "2k"
};
const Mifare::CardType Mifare::card_4k =
{
    4,
    40,
    256,
    {4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 16, 16, 16, 16, 16, 16, 16, 16},
    {0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124, 128, 144, 160, 176, 192, 208, 224, 240},
    "4k"
};

const Mifare::AccessType Mifare::dataCondition[8][4] =
{
    // {read, write, increment, decrement/transfer/restore}
    {ACC_KEY_AB, ACC_KEY_AB, ACC_KEY_AB, ACC_KEY_AB}, // {C3x, C2x, C1x} = 0
    {ACC_KEY_AB, ACC_KEY_B, ACC_NEVER, ACC_NEVER}, // {C3x, C2x, C1x} = 1
    {ACC_KEY_AB, ACC_NEVER, ACC_NEVER, ACC_NEVER}, // {C3x, C2x, C1x} = 2
    {ACC_KEY_AB, ACC_KEY_B, ACC_KEY_B, ACC_KEY_AB}, // {C3x, C2x, C1x} = 3
    {ACC_KEY_AB, ACC_NEVER, ACC_NEVER, ACC_KEY_AB}, // {C3x, C2x, C1x} = 4
    {ACC_KEY_B, ACC_NEVER, ACC_NEVER, ACC_NEVER}, // {C3x, C2x, C1x} = 5
    {ACC_KEY_B, ACC_KEY_B, ACC_NEVER, ACC_NEVER}, // {C3x, C2x, C1x} = 6
    {ACC_NEVER, ACC_NEVER, ACC_NEVER, ACC_NEVER}, // {C3x, C2x, C1x} = 7
};
const Mifare::AccessType Mifare::trailerReadCondition[8][3] =
{
    // {KEYA, Access bits, KEYB}
    {ACC_NEVER, ACC_KEY_A, ACC_KEY_A}, // {C33, C23, C13} = 0
    {ACC_NEVER, ACC_KEY_AB, ACC_NEVER}, // {C33, C23, C13} = 1
    {ACC_NEVER, ACC_KEY_A, ACC_KEY_A}, // {C33, C23, C13} = 2
    {ACC_NEVER, ACC_KEY_AB, ACC_NEVER}, // {C33, C23, C13} = 3
    {ACC_NEVER, ACC_KEY_A, ACC_KEY_A}, // {C33, C23, C13} = 4
    {ACC_NEVER, ACC_KEY_AB, ACC_NEVER}, // {C33, C23, C13} = 5
    {ACC_NEVER, ACC_KEY_AB, ACC_NEVER}, // {C33, C23, C13} = 6
    {ACC_NEVER, ACC_KEY_AB, ACC_NEVER}, // {C33, C23, C13} = 7
};
const Mifare::AccessType Mifare::trailerWriteCondition[8][3] =
{
    // {KEYA, Access bits, KEYB}
    {ACC_KEY_A, ACC_NEVER, ACC_KEY_A}, // {C33, C23, C13} = 0
    {ACC_KEY_B, ACC_NEVER, ACC_KEY_B}, // {C33, C23, C13} = 1
    {ACC_NEVER, ACC_NEVER, ACC_NEVER}, // {C33, C23, C13} = 2
    {ACC_NEVER, ACC_NEVER, ACC_NEVER}, // {C33, C23, C13} = 3
    {ACC_KEY_A, ACC_KEY_A, ACC_KEY_A}, // {C33, C23, C13} = 4
    {ACC_NEVER, ACC_KEY_B, ACC_NEVER}, // {C33, C23, C13} = 5
    {ACC_KEY_B, ACC_KEY_B, ACC_KEY_B}, // {C33, C23, C13} = 6
    {ACC_NEVER, ACC_NEVER, ACC_NEVER}, // {C33, C23, C13} = 7
};

Mifare::Mifare(Ui::MainWindow *ui, Util *addr, ClientSession *session, QWidget *parent): QObject(parent)
{
    this->parent = parent;
    util = addr;
    this->ui = ui;
    this->session = session;
    cardType = card_1k;
    // The button caption is the two-line backdoor label from the UI file,
    // translated by retranslateUi() before Mifare is constructed.  Remember
    // it so the busy state can restore the exact text without duplicating the
    // translation lookup in two modules.
    if(ui->MF_Quick_crackButton)
        gen3ButtonIdleText = ui->MF_Quick_crackButton->text();
    keyAList = new QStringList();
    keyBList = new QStringList();
    dataList = new QStringList();
    data_clearKey();  // fill with blank QString
    data_clearData(); // fill with blank QString
    dataPattern = new QRegularExpression("([0-9a-fA-F]{2} ){15}[0-9a-fA-F]{2}");
    keyPattern_res = new QRegularExpression("\\|\\s*\\d{3}\\s*\\|\\s*.+?\\s*\\|\\s*.+?\\s*\\|\\s*.+?\\s*\\|\\s*.+?\\s*\\|");
    keyPattern = new QRegularExpression("\\|\\s*\\d{3}\\s*\\|\\s*.+?\\s*\\|\\s*.+?\\s*\\|");

    // Stage 3: status area + result area defaults.
    if(ui->MF_result_progressBar)
        ui->MF_result_progressBar->setValue(0);
    if(session)
    {
        pm3Connected = session->isConnected();
        connect(session, &ClientSession::stateChanged, this, &Mifare::onPm3StateChanged);
    }
    updateStatusLabels();
}

Mifare::~Mifare()
{
    // A running one-click task must not outlive the page: stop issuing
    // commands and give the worker a bounded window to return.
    if(gen3Thread)
    {
        gen3Cancel.request();
        pm3LogFilter.suspend();
        if(!gen3Thread->wait(5000))
            gen3Thread->disconnect(this);
    }
}

void Mifare::setConfigMap(const QVariantMap& configMap){
    this->configMap = configMap;
}

QMap<QString, QString> Mifare::info(bool isRequiringOutput)
{
    Q_UNUSED(isRequiringOutput);
    QMap<QString, QString> map;
    QVariantMap config = configMap["info"].toMap();
    QString cmd = config["basic cmd"].toString();

    // for official client
    if(cmd.isEmpty())
        cmd = config["cmd"].toString();
    // The read-only info path never uses the old NACK-bug probe and never
    // leaves the MIFARE page.  `info()` keeps the captured-output behavior for
    // callers such as the UID parameter dialog; the single card-info workflow
    // lives in readCardInfo().
    cmd.remove(QRegularExpression("\\s+-nsv\\b"));
    if(cmd.isEmpty())
        cmd = QStringLiteral("hf 14a info");
    QString result = util->execCMDWithOutput(cmd, 500);
    QStringList lineList = result.split("\n");

    // v4.23346 prints e.g.:
    //   [+]  UID: 2F 7B 54 54   ( FNUID, fixed, non-unique ID )
    //   [+] ATQA: 00 04
    //   [+]  SAK: 20 [1]
    // Parsing the token after the label avoids picking letters out of the
    // annotation (e.g. the "F" in "FNUID").
    const QRegularExpression uidRe(QStringLiteral("UID:\\s*([0-9A-Fa-f ]+)"));
    const QRegularExpression atqaRe(QStringLiteral("ATQA:\\s*([0-9A-Fa-f ]+)"));
    const QRegularExpression sakRe(QStringLiteral("SAK:\\s*([0-9A-Fa-f ]+)"));

    for(const QString& line : lineList)
    {
        QRegularExpressionMatch match = uidRe.match(line);
        if(match.hasMatch())
        {
            map["UID"] = match.captured(1).remove(QRegularExpression("[^0-9a-fA-F]")).trimmed();
            continue;
        }
        match = atqaRe.match(line);
        if(match.hasMatch())
        {
            map["ATQA"] = match.captured(1).remove(QRegularExpression("[^0-9a-fA-F]")).trimmed();
            continue;
        }
        match = sakRe.match(line);
        if(match.hasMatch())
        {
            map["SAK"] = match.captured(1).remove(QRegularExpression("[^0-9a-fA-F]")).trimmed();
        }
    }
    return map;
}

void Mifare::chk()
{
    QRegularExpressionMatch reMatch;
    QString result;
    int offset = 0;
    QString data;
    QVariantMap config = configMap["check"].toMap();
    QString cmd = config["cmd"].toString();
    int keyAindex = config["key A index"].toInt();
    int keyBindex = config["key B index"].toInt();
    QRegularExpression keyPattern = QRegularExpression(config["key pattern"].toString(), QRegularExpression::MultilineOption);
    cmd.replace("<card type>", config["card type"].toMap()[cardType.typeText].toString());

    result = util->execCMDWithOutput(
                 cmd,
                 Util::ReturnTrigger(60000, {"No valid", keyPattern.pattern()}));
    for(int i = 0; i < cardType.sector_size; i++)
    {
        reMatch = keyPattern.match(result, offset);
        offset = reMatch.capturedStart();
        if(reMatch.hasMatch())
        {
            data = reMatch.captured().toUpper();
            offset += data.length();
            QStringList cells = data.remove(" ").split("|");
            if(!cells[keyAindex].contains(QRegularExpression("[^0-9a-fA-F]")))
            {
                keyAList->replace(i, cells[keyAindex]);
            }
            if(!cells[keyBindex].contains(QRegularExpression("[^0-9a-fA-F]")))
            {
                keyBList->replace(i, cells[keyBindex]);
            }
        }
    }

    data_syncWithKeyWidget();
}

void Mifare::nested(bool isStaticNested)
{
    QVariantMap config = configMap["nested"].toMap();
    QString cmd;
    if(isStaticNested)
        cmd = config["static cmd"].toString();
    else
        cmd = config["cmd"].toString();
    int keyAindex = config["key A index"].toInt();
    int keyBindex = config["key B index"].toInt();
    QRegularExpression keyPattern = QRegularExpression(config["key pattern"].toString(), QRegularExpression::MultilineOption);
    QRegularExpressionMatch reMatch;
    QString result;
    int offset = 0;
    QString data;

    cmd.replace("<card type>", config["card type"].toMap()[cardType.typeText].toString());
    if(cmd.contains(QRegularExpression("<.+>"))) // need at least one section key
    {
        QString knownKey, knownKeyType;
        int knownKeySector = -1;
        for(int i = 0; i < cardType.sector_size; i++)
        {
            if(data_isKeyValid(keyAList->at(i)))
            {
                knownKeyType = "A";
                knownKey = keyAList->at(i);
                knownKeySector = i;
                break;
            }
            else if(data_isKeyValid(keyBList->at(i)))
            {
                knownKeyType = "B";
                knownKey = keyBList->at(i);
                knownKeySector = i;
                break;
            }
        }
        if(knownKeySector != -1)
        {
            cmd.replace("<block>", QString::number(cardType.blks[knownKeySector]));
            cmd.replace("<key type>", config["key type"].toMap()[knownKeyType].toString());
            cmd.replace("<key>", knownKey);
        }
        else
        {
            QMessageBox::information(parent, tr("Info"), tr("Plz provide at least one known key"));
            return;
        }

    }
    result = util->execCMDWithOutput(
                 cmd,
                 Util::ReturnTrigger(60000, {"Quit", "Can't found", "Can't authenticate", keyPattern_res->pattern()}),
                 true);

    if(result.contains("static") && !isStaticNested)
    {
        nested(true);
        return;
    }

    for(int i = 0; i < cardType.sector_size; i++)
    {
        reMatch = keyPattern.match(result, offset);
        offset = reMatch.capturedStart();
        if(reMatch.hasMatch())
        {
            data = reMatch.captured().toUpper();
            offset += data.length();
            QStringList cells = data.remove(" ").split("|");
            if(!cells[keyAindex].contains(QRegularExpression("[^0-9a-fA-F]")))
            {
                keyAList->replace(i, cells[keyAindex]);
            }
            if(!cells[keyBindex].contains(QRegularExpression("[^0-9a-fA-F]")))
            {
                keyBList->replace(i, cells[keyBindex]);
            }
        }
    }
    data_syncWithKeyWidget();

}

void Mifare::hardnested()
{
    QVariantMap config = configMap["hardnested"].toMap();
    MF_Attack_hardnestedDialog dialog(cardType.block_size, config);
    connect(&dialog, &MF_Attack_hardnestedDialog::sendCMD, this, [this](const QString &cmd) { util->execDetached(cmd); });
    if(dialog.exec() == QDialog::Accepted)
        Util::gotoRawTab();
}

void Mifare::darkside()
{
    QVariantMap config = configMap["darkside"].toMap();
    util->execCMD(config["cmd"].toString());

    Util::gotoRawTab();
}

void Mifare::sniff()
{
    QVariantMap config = configMap["sniff"].toMap();
    sniffBufferLoaded = false;
    util->execDetached(config["cmd"].toString());

    Util::gotoRawTab();
}

void Mifare::sniff14a()
{
    QVariantMap config = configMap["sniff 14a"].toMap();
    sniffBufferLoaded = false;
    util->execDetached(config["cmd"].toString());

    Util::gotoRawTab();
}

void Mifare::list()
{
    QVariantMap config = configMap["list"].toMap();
    util->execCMD(config["cmd"].toString());

    Util::gotoRawTab();
}

QString Mifare::_readblk(int blockId, KeyType keyType, const QString& key, TargetType targetType, int waitTime)
{
    QString data;
    QString result;
    QRegularExpressionMatch currMatch;
    bool isTrailerBlock = (blockId < 128 && ((blockId + 1) % 4 == 0)) || ((blockId + 1) % 16 == 0);

    if(targetType == TARGET_MIFARE)
    {
        if(!data_isKeyValid(key))
        {
            return "";
        }
        QVariantMap config = configMap["normal read block"].toMap();
        QString cmd = config["cmd"].toString();
        QRegularExpression dataPattern = QRegularExpression(config["data pattern"].toString());
        cmd.replace("<block>", QString::number(blockId));
        cmd.replace("<key type>", config["key type"].toMap()[QString((char)keyType)].toString());
        cmd.replace("<key>", key);
        // use the given key type to read the target block
        result = util->execCMDWithOutput(cmd, waitTime);

        currMatch = dataPattern.match(result);
        if(currMatch.hasMatch())
        {
            data = currMatch.captured().toUpper();
            data.remove(" ");
            // when the target block is a key block and the given key type is KeyA, try to check whether the KeyB is valid(by Access Bits)
            // if the given key type is KeyB, it will never get the KeyA from the key block
            if(isTrailerBlock && keyType == KEY_A) // in this case, the Access Bits is always accessible
            {
                data.replace(0, 12, key);
                QList<quint8> ACBits = data_getACBits(data.mid(12, 8));
                if(ACBits.size() == 4 && (ACBits[3] == 2 || ACBits[3] == 3 || ACBits[3] == 5 || ACBits[3] == 6 || ACBits[3] == 7)) // in these cases, the KeyB cannot be read by KeyA
                {
                    data.replace(20, 12, "????????????");
                }
            }
            else if(isTrailerBlock && keyType == KEY_B)
            {
                data.replace(20, 12, key);;
                data.replace(0, 12, "????????????"); // fill the keyA part with ?
            }
        }
        else
            data = "";
    }
    else if(targetType == TARGET_UID)
    {
        QVariantMap config = configMap["Magic Card read block"].toMap();
        QString cmd = config["cmd"].toString();
        QRegularExpression dataPattern = QRegularExpression(config["data pattern"].toString());
        cmd.replace("<block>", QString::number(blockId));
        result = util->execCMDWithOutput(cmd, waitTime);
        currMatch = dataPattern.match(result);
        if(currMatch.hasMatch())
        {
            data = currMatch.captured().toUpper();
            data.remove(" ");
        }
        else
            data = "";
    }
    else if(targetType == TARGET_EMULATOR)
    {
        QVariantMap config = configMap["emulator read block"].toMap();
        QString cmd = config["cmd"].toString();
        QRegularExpression dataPattern = QRegularExpression(config["data pattern"].toString());
        cmd.replace("<block>", QString::number(blockId));
        result = util->execCMDWithOutput(cmd, 150);
        data = dataPattern.match(result).captured().toUpper();
        data.remove(" ");
    }

    return data;
}

QStringList Mifare::_readsec(int sectorId, KeyType keyType, const QString& key, TargetType targetType, int waitTime)
{
    QVariantMap config;
    QStringList data;
    QString result, tmp;
    QRegularExpressionMatch reMatch;
    int offset = -1; // for targetType == TARGET_EMULATOR

    for(int i = 0; i < cardType.blk[sectorId]; i++)
    {
        data.append("");
    }

    // try to read all blocks together
    if(targetType == TARGET_MIFARE)
    {
        if(!data_isKeyValid(key))
        {
            return data;
        }
        config = configMap["normal read sector"].toMap();
        QString cmd = config["cmd"].toString();
        cmd.replace("<sector>", QString::number(sectorId));
        cmd.replace("<key type>", config["key type"].toMap()[QString((char)keyType)].toString());
        cmd.replace("<key>", key);
        result = util->execCMDWithOutput(cmd, waitTime);
    }
    else if(targetType == TARGET_UID)
    {
        config = configMap["Magic Card read sector"].toMap();
        QString cmd = config["cmd"].toString();
        cmd.replace("<sector>", QString::number(sectorId));
        result = util->execCMDWithOutput(cmd, waitTime);
    }
    else if(targetType == TARGET_EMULATOR)
    {
        for(int i = 0; i < cardType.blk[sectorId]; i++)
            data[i] = _readblk(cardType.blks[sectorId] + i, keyType, key, targetType, waitTime);
        return data;
    }

    // for TARGET_MIFARE and TARGET_UID
    // if targetType == TARGET_EMULATOR, this function has returned
    QRegularExpression dataPattern = QRegularExpression(config["data pattern"].toString());
    reMatch = dataPattern.match(result);
    offset = reMatch.capturedStart();
    if(reMatch.hasMatch()) // read successful
    {
        for(int i = 0; i < cardType.blk[sectorId]; i++)
        {
            reMatch = dataPattern.match(result, offset);
            offset = reMatch.capturedStart();
            if(reMatch.hasMatch())
            {
                tmp = reMatch.captured().toUpper();
                offset += tmp.length();
                tmp.remove(" ");
                data[i] = tmp;
            }
        }
    }
    // when one of the block cannot be read, the rdsc will return nothing, so you need to read the rest of blocks manually
    // the following rdbl operation is not handled there, for better speed(rdsc_A->rdsc_B->rdbl0~3)
    else if(targetType == TARGET_UID) // treat as MIFARE
        data = _readsec(sectorId, keyType, key, TARGET_MIFARE, waitTime);


    //process trailer(like _readblk())
    QString trailer = data[cardType.blk[sectorId] - 1];
    if(trailer != "" && targetType == TARGET_MIFARE)
    {
        if(keyType == KEY_A) // in this case, the Access Bits is always accessible
        {
            trailer.replace(0, 12, key);
            QList<quint8> ACBits = data_getACBits(trailer.mid(12, 8));
            if(ACBits.size() == 4 && (ACBits[3] == 2 || ACBits[3] == 3 || ACBits[3] == 5 || ACBits[3] == 6 || ACBits[3] == 7)) // in these cases, the KeyB cannot be read by KeyA
            {
                trailer.replace(20, 12, "????????????");
            }
        }
        else if(keyType == KEY_B)
        {
            trailer.replace(20, 12, key);;
            trailer.replace(0, 12, "????????????"); // fill the keyA part with ?
        }
        data[cardType.blk[sectorId] - 1] = trailer;
    }

    return data;
}

void Mifare::readOne(TargetType targetType)
{
    int blockId = ui->MF_RW_blockBox->currentText().toInt();
    Mifare::KeyType keyType = (Mifare::KeyType)(ui->MF_RW_keyTypeBox->currentData().toInt());
    QString result = _readblk(blockId, keyType, ui->MF_RW_keyEdit->text().toUpper(), targetType);
    if(result != "")
    {
        ui->MF_RW_dataEdit->setText(result);
    }
    else
    {
        ui->MF_RW_dataEdit->setText(tr("Failed!"));
    }
}

void Mifare::readSelected(TargetType targetType)
{
    QString trailerA, trailerB;
    QList<bool> selectedSectors;
    QList<int> selectedBlocks;
    for(int i = 0; i < cardType.block_size; i++)
    {
        if(ui->MF_dataWidget->item(i, 1)->checkState() == Qt::Checked)
            selectedBlocks.append(i);
    }

    for(int i = 0; i < cardType.sector_size; i++)
    {
        selectedSectors.append(false);
    }
    for(int item : selectedBlocks)
    {
        selectedSectors[data_b2s(item)] = true;
    }

    for(int i = 0; i < cardType.sector_size; i++)
    {
        if(!selectedSectors[i])
            continue;

        QStringList data, dataA, dataB;
        for(int j = 0; j < cardType.blk[i]; j++)
        {
            // dataA is always filled with "" because of the _readsec()
            data.append("");
            dataB.append("");
        }

        dataA = _readsec(i, Mifare::KEY_A, keyAList->at(i), targetType);

        // in other situations, the key doesn't matters
        // so the dataA is the final result
        //
        // if the targetType is TARGET_MIFARE and the dataA has unknown part, try to read by keyB
        if(targetType == TARGET_MIFARE && (dataA.contains("") || dataA[cardType.blk[i] - 1].right(12) == "????????????"))
            dataB = _readsec(i, Mifare::KEY_B, keyBList->at(i), targetType);

        // process trailer block seperately
        if(dataA[cardType.blk[i] - 1] == "" && selectedBlocks.contains(getTrailerBlockId(i)))
            dataA[cardType.blk[i] - 1] = _readblk(getTrailerBlockId(i), Mifare::KEY_A, keyAList->at(i), targetType);
        if(dataB[cardType.blk[i] - 1] == "" && dataA[cardType.blk[i] - 1].right(12) == "????????????" && selectedBlocks.contains(getTrailerBlockId(i)))
            dataB[cardType.blk[i] - 1] = _readblk(getTrailerBlockId(i), Mifare::KEY_B, keyBList->at(i), targetType);


        for(int j = 0; j < cardType.blk[i]; j++)
        {
            if(dataA[j] != "")
                data[j] = dataA[j];
            else
                data[j] = dataB[j];

            if(data[j] == "" && selectedBlocks.contains(cardType.blks[i] + j)) // try rdbl seperately
            {
                data[j] = _readblk(cardType.blks[i] + j, Mifare::KEY_A, keyAList->at(i), targetType);
                if(data[j] == "")
                    data[j] = _readblk(cardType.blks[i] + j, Mifare::KEY_B, keyBList->at(i), targetType);
            }
        }

        // process trailer block seperately
        trailerA = dataA[cardType.blk[i] - 1];
        trailerB = dataB[cardType.blk[i] - 1];
        if(trailerA != "" && trailerB != "") // if KeyA and KeyB can both read the trailer, then concat them
        {
            QString ACbits = trailerA.mid(12, 8);
            QString key_A = trailerA.left(12); // KeyA cannot be read by KeyB
            QString key_B = trailerA.at(31) != '?' ? trailerA.right(12) : trailerB.right(12);
            data[cardType.blk[i] - 1] = key_A + ACbits + key_B;
        }

        for(int j = 0; j < cardType.blk[i]; j++)
        {
            if(selectedBlocks.contains(cardType.blks[i] + j))
            {
                dataList->replace(cardType.blks[i] + j, data[j]);
                data_syncWithDataWidget(false, cardType.blks[i] + j);
            }
        }

        if(selectedBlocks.contains(getTrailerBlockId(i)))
        {
            // data widget has been updated, so this is just a temporary varient.
            if(data[cardType.blk[i] - 1] == "")
                data[cardType.blk[i] - 1] = "????????????????????????????????";

            // doesn't replace the existing key.
            if(!data_isKeyValid(keyAList->at(i)))
                keyAList->replace(i, data[cardType.blk[i] - 1].left(12));
            if(!data_isKeyValid(keyBList->at(i)))
                keyBList->replace(i, data[cardType.blk[i] - 1].right(12));
            data_syncWithKeyWidget(false, i, KEY_A);
            data_syncWithKeyWidget(false, i, KEY_B);
        }
    }
}

bool Mifare::_writeblk(int blockId, KeyType keyType, const QString& key, const QString& data, TargetType targetType, int waitTime)
{
    QString result;
    QString input = data.toUpper();

    input.remove(" ");
    if(data_isDataValid(input) != DATA_NOSPACE)
        return false;

    if(targetType == TARGET_MIFARE)
    {
        if(!data_isKeyValid(key))
            return false;
        QVariantMap config = configMap["normal write block"].toMap();
        QString cmd = config["cmd"].toString();
        cmd.replace("<block>", QString::number(blockId));
        cmd.replace("<key type>", config["key type"].toMap()[QString((char)keyType)].toString());
        cmd.replace("<key>", key);
        cmd.replace("<data>", input);
        result = util->execCMDWithOutput(cmd, waitTime);
        if(result.isEmpty())
            return false;

        QVariantList failedFlag = config["failed flag"].toJsonArray().toVariantList();
        for(auto flag = failedFlag.begin(); flag != failedFlag.end(); flag++)
        {
            if(result.contains(flag->toString()))
                return false;
        }
        return true;
    }
    else if(targetType == TARGET_UID)
    {
        QVariantMap config = configMap["Magic Card write block"].toMap();
        QString cmd = config["cmd"].toString();
        cmd.replace("<block>", QString::number(blockId));
        cmd.replace("<data>", input);
        result = util->execCMDWithOutput(cmd, waitTime);
        if(result.isEmpty())
            return false;

        QVariantList failedFlag = config["failed flag"].toJsonArray().toVariantList();
        for(auto flag = failedFlag.begin(); flag != failedFlag.end(); flag++)
        {
            if(result.contains(flag->toString()))
                return false;
        }
        return true;
    }
    else if(targetType == TARGET_EMULATOR)
    {
        QVariantMap config = configMap["emulator write block"].toMap();
        QString cmd = config["cmd"].toString();
        cmd.replace("<block>", QString::number(blockId));
        cmd.replace("<data>", input);
        util->execCMD(cmd);
        util->delay(5);
        return true;
    }

    return false;
}

void Mifare::writeOne(TargetType targetType)
{
    int blockId = ui->MF_RW_blockBox->currentText().toInt();
    Mifare::KeyType keyType = (Mifare::KeyType)(ui->MF_RW_keyTypeBox->currentData().toInt());
    bool isSuccessful = _writeblk(blockId, keyType, ui->MF_RW_keyEdit->text().toUpper(), ui->MF_RW_dataEdit->text(), targetType);
    if(isSuccessful)
    {
        QMessageBox::information(parent, tr("Info"), tr("Succeed!"));
    }
    else
    {
        QMessageBox::information(parent, tr("Info"), tr("Failed!"));
    }
}

void Mifare::writeSelected(TargetType targetType)
{
    QList<int> failedBlocks;
    QList<int> selectedBlocks;
    bool yes2All = false, no2All = false;

    for(int i = 0; i < cardType.block_size; i++)
    {
        if(ui->MF_dataWidget->item(i, 1)->checkState() == Qt::Checked)
            selectedBlocks.append(i);
    }
    for(int item : selectedBlocks)
    {
        bool result = false;
        bool isTrailerBlock = (item < 128 && ((item + 1) % 4 == 0)) || ((item + 1) % 16 == 0);

        if(isTrailerBlock && !data_isACBitsValid(dataList->at(item).mid(12, 8))) // trailer block is invalid
        {
            if(!yes2All && !no2All)
            {
                QMessageBox::StandardButton choice = QMessageBox::information(parent, tr("Info"),
                                                     tr("The Access Bits is invalid!\nIt could make the whole sector blocked irreversibly!\nContinue to write?"),
                                                     QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No | QMessageBox::NoToAll);
                if(choice == QMessageBox::No)
                    continue;
                else if(choice == QMessageBox::YesToAll)
                    yes2All = true;
                else if(choice == QMessageBox::NoToAll)
                {
                    no2All = true;
                    continue;
                }
            }
            else if(no2All)
                continue;
        }

        if(targetType == TARGET_MIFARE)
        {
            result = _writeblk(item, KEY_A, keyAList->at(data_b2s(item)), dataList->at(item), TARGET_MIFARE);
            if(!result)
            {
                result = _writeblk(item, KEY_B, keyBList->at(data_b2s(item)), dataList->at(item), TARGET_MIFARE);
            }
            if(!result && keyAList->at(data_b2s(item)) != "FFFFFFFFFFFF")
            {
                result = _writeblk(item, KEY_A, "FFFFFFFFFFFF", dataList->at(item), TARGET_MIFARE);
            }
            if(!result && keyBList->at(data_b2s(item)) != "FFFFFFFFFFFF") // for access bits like "80 f7 87", the block can only be written with keyB
            {
                result = _writeblk(item, KEY_B, "FFFFFFFFFFFF", dataList->at(item), TARGET_MIFARE);
            }
        }
        else // key doesn't matter when writing to Chinese Magic Card and Emulator Memory
        {
            result = _writeblk(item, KEY_A, "FFFFFFFFFFFF", dataList->at(item), targetType);
        }
        if(!result)
        {
            failedBlocks.append(item);
        }
    }
    if(failedBlocks.size() == 0)
        QMessageBox::information(parent, tr("Info"), tr("Succeed!"));
    else
    {
        QString suffix = "";
        int counter = 0;
        for(int failedBlk : failedBlocks)
        {
            suffix += QString::number(failedBlk) + " ";
            counter++;
            counter %= 20;
            if(counter == 0)
                suffix += "\n";
        }
        QMessageBox::StandardButton res = QMessageBox::information(parent, tr("Info"), tr("Failed to write to these blocks:")
                                          + "\n"
                                          + suffix
                                          + "\n"
                                          + tr("Select them?"),
                                          QMessageBox::Yes | QMessageBox::No);
        if(res == QMessageBox::Yes)
        {
            for(int item : selectedBlocks)
            {
                ui->MF_dataWidget->item(item, 1)->setCheckState(Qt::Unchecked);
            }
            for(int failedBlk : failedBlocks)
            {
                ui->MF_dataWidget->item(failedBlk, 1)->setCheckState(Qt::Checked);
            }
        }

    }
}

void Mifare::dump()
{
    QVariantMap config = configMap["dump"].toMap();
    QString cmd = config["cmd"].toString();
    if(cmd.contains("<card type>"))
        cmd.replace("<card type>", config["card type"].toMap()[cardType.typeText].toString());
    util->execCMD(cmd);
    Util::gotoRawTab();
}

void Mifare::restore()
{
    QVariantMap config = configMap["restore"].toMap();
    QString cmd = config["cmd"].toString();
    if(cmd.contains("<card type>"))
        cmd.replace("<card type>", config["card type"].toMap()[cardType.typeText].toString());
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        parent, tr("Restore MIFARE Classic"),
        tr("This will write a dump file to the card and may overwrite the current data.\n"
           "The client selects the default dump file by the current card UID.\n\n"
           "Continue?"),
        QMessageBox::Yes | QMessageBox::No);
    if(choice != QMessageBox::Yes)
        return;
    util->execCMD(cmd);
    Util::gotoRawTab();
}

void Mifare::wipeC()
{
    QVariantMap config = configMap["Magic Card wipe"].toMap();
    QString cmd = config["cmd"].toString();
    if(cmd.contains("<card type>"))
        cmd.replace("<card type>", config["card type"].toMap()[cardType.typeText].toString());
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        parent, tr("Wipe Magic Card"),
        tr("This will erase the writable data of a Chinese Magic Card.\n\nContinue?"),
        QMessageBox::Yes | QMessageBox::No);
    if(choice != QMessageBox::Yes)
        return;
    util->execCMD(cmd);
    Util::gotoRawTab();
}

void Mifare::setParameterC()
{
    QVariantMap config = configMap["Magic Card set parameter"].toMap();
    QMap<QString, QString> result = info(true);
    if(result.isEmpty())
    {
        QMessageBox::information(parent, tr("Info"), tr("Failed to read card."));
        return;
    }
    else
    {
        MF_UID_parameterDialog dialog(result["UID"].toUpper(), result["ATQA"].toUpper(), result["SAK"].toUpper(), config);
        connect(&dialog, &MF_UID_parameterDialog::sendCMD, util, &Util::execCMD);
        if(dialog.exec() == QDialog::Accepted)
            Util::gotoRawTab();
    }
}

void Mifare::lockC()
{
    QVariantMap config = configMap["Magic Card lock"].toMap();
    QString cmd = config["cmd"].toString();
    QVariantList list = config["sequence"].toJsonArray().toVariantList();
    const QMessageBox::StandardButton choice = QMessageBox::warning(
        parent, tr("Lock UFUID Card"),
        tr("This sends the UFUID locking sequence and may be irreversible.\n\nContinue?"),
        QMessageBox::Yes | QMessageBox::No);
    if(choice != QMessageBox::Yes)
        return;
    for(auto item = list.begin(); item != list.end(); item++)
    {
        qDebug() << cmd + item->toString();
        util->execCMD(cmd + item->toString());
    }
}

void Mifare::wipeE()
{
    QVariantMap config = configMap["emulator wipe"].toMap();
    util->execCMD(config["cmd"].toString());
}

void Mifare::simulate()
{
    MF_Sim_simDialog dialog(cardType.type, cardType.typeText);
    connect(&dialog, &MF_Sim_simDialog::sendCMD, this, [this](const QString &cmd) { util->execDetached(cmd); });
    if(dialog.exec() == QDialog::Accepted)
        Util::gotoRawTab();
}

void Mifare::loadSniff(const QString& file)
{
    QVariantMap config = configMap["load sniff"].toMap();
    QString cmd = config["cmd"].toString();
    cmd.replace("<filename>", file);
    // v4.23346: trace load reports "Recorded Activity (TraceLen = ...)".
    // The old "loaded" trigger no longer exists. ClientSession's sentinel is
    // authoritative, but keep the new text for diagnostics/compatibility.
    if(util->execCMDWithOutput(cmd, Util::ReturnTrigger({"Recorded Activity", "TraceLen"})) != "")
    {
        sniffBufferLoaded = true;
        if(config.contains("show cmd"))
            util->execCMD(config["show cmd"].toString());
    }
    else
    {
        sniffBufferLoaded = false;
    }

    Util::gotoRawTab();
}

void Mifare::saveSniff(const QString& file)
{
    QVariantMap config = configMap["save sniff"].toMap();
    QString cmd = config["cmd"].toString();
    cmd.replace("<filename>", file);
    // v4.23346 defaults `trace save` to downloading from the device. If the
    // trace was just loaded (or came from a previous client-buffer operation),
    // save the client buffer explicitly.
    if(sniffBufferLoaded && !cmd.contains("-1") && !cmd.contains("--buffer"))
        cmd.replace(QRegularExpression(QStringLiteral("^trace\\s+save\\b")),
                    QStringLiteral("trace save -1"));
    util->execCMD(cmd);

    Util::gotoRawTab();
}

void Mifare::data_syncWithDataWidget(bool syncAll, int block)
{
    ui->MF_dataWidget->blockSignals(true);
    QString tmp;
    if(syncAll)
    {
        // The tables are resized by the shell, but the lists are owned here.
        // Clamp to both the model and the widget: a stale/short table must
        // never turn into a null `item(i, 2)` dereference.
        const int limit = qMin(int(cardType.block_size),
                               qMin(ui->MF_dataWidget->rowCount(), dataList->size()));
        for(int i = 0; i < limit; i++)
        {
            tmp = "";
            if(dataList->at(i) != "")
            {
                tmp += dataList->at(i).midRef(0, 2);
                for(int j = 1; j < 16; j++)
                {
                    tmp += " ";
                    tmp += dataList->at(i).midRef(j * 2, 2);
                }
            }
            mftable::setItemText(ui->MF_dataWidget, i, 2, tmp);
        }
    }
    else
    {
        tmp = "";
        if(block >= 0 && block < dataList->size() && dataList->at(block) != "")
        {
            tmp += dataList->at(block).midRef(0, 2);
            for(int j = 1; j < 16; j++)
            {
                tmp += " ";
                tmp += dataList->at(block).midRef(j * 2, 2);
            }
        }
        mftable::setItemText(ui->MF_dataWidget, block, 2, tmp);
    }
    ui->MF_dataWidget->blockSignals(false);
}

void Mifare::data_syncWithKeyWidget(bool syncAll, int sector, KeyType keyType)
{
    ui->MF_keyWidget->blockSignals(true);
    if(syncAll)
    {
        const int limit = qMin(int(cardType.sector_size),
                               qMin(ui->MF_keyWidget->rowCount(), keyAList->size()));
        for(int i = 0; i < limit; i++)
        {
            mftable::setItemText(ui->MF_keyWidget, i, 1, keyAList->at(i));
            if(i < keyBList->size())
                mftable::setItemText(ui->MF_keyWidget, i, 2, keyBList->at(i));
        }
    }
    else
    {
        if(keyType == KEY_A)
        {
            if(sector >= 0 && sector < keyAList->size())
                mftable::setItemText(ui->MF_keyWidget, sector, 1, keyAList->at(sector));
        }
        else if(sector >= 0 && sector < keyBList->size())
        {
            mftable::setItemText(ui->MF_keyWidget, sector, 2, keyBList->at(sector));
        }
    }
    ui->MF_keyWidget->blockSignals(false);
}

void Mifare::data_clearData(bool clearAll)
{
    if(clearAll)
    {
        dataList->clear();
    }

    int delta = cardType.block_size - dataList->length()  ;
    if(delta >= 0)
    {
        for(int i = 0; i < delta; i++)
            dataList->append("");
    }
    else if(delta < 0)
    {
        for(int i = 0; i < -delta; i++)

            dataList->removeLast();
    }
}

void Mifare::data_clearKey(bool clearAll)
{
    if(clearAll)
    {
        keyAList->clear();
        keyBList->clear();
    }

    int delta = cardType.sector_size - keyAList->length()  ;
    if(delta >= 0)
    {
        for(int i = 0; i < delta; i++)
        {
            keyAList->append("");
            keyBList->append("");
        }
    }
    else if(delta < 0)
    {
        for(int i = 0; i < -delta; i++)
        {
            keyAList->removeLast();
            keyBList->removeLast();
        }
    }
}

bool Mifare::data_isKeyValid(const QString& key)
{
    if(key.length() != 12)
        return false;
    for(int i = 0; i < 12; i++)
    {
        if(!((key[i] >= '0' && key[i] <= '9') || (key[i] >= 'A' && key[i] <= 'F')))
            return false;
    }
    return true;
}

Mifare::DataType Mifare::data_isDataValid(const QString& data) // "?" will not been processd there
{
    if(data.length() == 47)
    {
        for(int i = 0; i < 47; i++)
        {
            if(i % 3 != 0)
            {
                if(!((data[i] >= '0' && data[i] <= '9') || (data[i] >= 'A' && data[i] <= 'F') || data[i] == '?'))
                    return DATA_INVALID;
            }
            else
            {
                if(data[i] != ' ')
                    return DATA_INVALID;
            }
        }
        return DATA_WITHSPACE;
    }
    else if(data.length() == 32)
    {
        for(int i = 0; i < 32; i++)
        {
            if(!((data[i] >= '0' && data[i] <= '9') || (data[i] >= 'A' && data[i] <= 'F') || data[i] == '?'))
                return DATA_INVALID;
        }
        return DATA_NOSPACE;
    }
    else
        return DATA_INVALID;
}

Mifare::CardType Mifare::getCardType()
{
    return cardType;
}

void Mifare::setCardType(int type)
{
    if(type == 0 || type == 1 || type == 2 || type == 4)
    {
        if(type == 0)
            cardType = card_mini;
        else if(type == 1)
            cardType = card_1k;
        else if(type == 2)
            cardType = card_2k;
        else if(type == 4)
            cardType = card_4k;
        data_clearKey(false);
        data_clearData(false);
    }
}

bool Mifare::data_loadDataFile(const QString& filename)
{
    QFile file(filename, this);
    if(!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray buff = file.readAll();
    file.close();

    const QString suffix = QFileInfo(filename).suffix().toLower();
    const bool forceText = (suffix == QLatin1String("eml") || suffix == QLatin1String("txt"));
    const bool forceBinary = (suffix == QLatin1String("bin") || suffix == QLatin1String("dump"));

    bool isBinary = forceBinary;
    if(!forceText && !forceBinary)
    {
        const int probeLen = qMin(buff.size(), int(cardType.block_size * 16));
        for(int i = 0; i < probeLen; i++)
        {
            const char c = buff.at(i);
            const bool allowed = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
                                 (c >= 'a' && c <= 'f') || c == '\n' || c == '\r';
            if(!allowed)
            {
                isBinary = true;
                break;
            }
        }
    }

    QStringList loaded;
    if(isBinary)
    {
        if(buff.size() < cardType.block_size * 16)
            return false;
        for(int i = 0; i < cardType.block_size; i++)
        {
            const QString block = bin2text(buff, i, 16);
            if(normalizeHex(block, 16).isEmpty())
                return false;
            loaded.append(block.toUpper());
        }
    }
    else
    {
        const QString text = QString::fromUtf8(buff);
        QStringList lines = text.split(QLatin1Char('\n'));
        while(!lines.isEmpty() && lines.last().trimmed().isEmpty())
            lines.removeLast();
        if(lines.size() < cardType.block_size)
            return false;
        for(int i = 0; i < cardType.block_size; i++)
        {
            QString line = lines.at(i).trimmed();
            line.remove(QLatin1Char('\r'));
            const QString normalized = normalizeHex(line, 16);
            if(normalized.isEmpty())
                return false;
            loaded.append(normalized);
        }
    }

    for(int i = 0; i < cardType.block_size; i++)
        dataList->replace(i, loaded.at(i));
    data_syncWithDataWidget();
    return true;
}

bool Mifare::data_loadKeyFile(const QString& filename)
{
    QFile file(filename, this);
    if(!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray buff = file.readAll();
    file.close();

    QStringList loadedA;
    QStringList loadedB;
    if(buff.size() >= cardType.block_size * 16)
    {
        // Full MIFARE dump: keys live in each sector trailer.
        for(int i = 0; i < cardType.sector_size; i++)
        {
            const int blk = getTrailerBlockId(i);
            const QString tmp = bin2text(buff, blk, 16);
            const QString keyA = normalizeHex(tmp.left(12), 6);
            const QString keyB = normalizeHex(tmp.right(12), 6);
            if(keyA.isEmpty() || keyB.isEmpty())
                return false;
            loadedA.append(keyA);
            loadedB.append(keyB);
        }
    }
    else if(buff.size() >= cardType.sector_size * 12)
    {
        // Key file: 12 bytes (6 + 6) per sector.
        for(int i = 0; i < cardType.sector_size; i++)
        {
            const QString tmp = bin2text(buff, i, 12);
            const QString keyA = normalizeHex(tmp.left(12), 6);
            const QString keyB = normalizeHex(tmp.right(12), 6);
            if(keyA.isEmpty() || keyB.isEmpty())
                return false;
            loadedA.append(keyA);
            loadedB.append(keyB);
        }
    }
    else
    {
        return false;
    }

    for(int i = 0; i < cardType.sector_size; i++)
    {
        keyAList->replace(i, loadedA.at(i));
        keyBList->replace(i, loadedB.at(i));
    }
    data_syncWithKeyWidget();
    return true;
}

QString Mifare::bin2text(const QByteArray& buff, int i, int length)
{
    if(i < 0 || length <= 0 || (i + 1) * length > buff.size())
        return QString();

    QString ret = "";
    char LByte, RByte;
    char map[16] = {'0', '1', '2', '3', '4', '5', '6', '7',
                    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
                   };
    for(int j = 0; j < length; j++)
    {
        LByte = map[(unsigned char)buff[i * length + j] >> 4];
        RByte = map[(unsigned char)buff[i * length + j] & 0xF];
        ret += LByte;
        ret += RByte;
    }
    return ret;
}

bool Mifare::data_saveDataFile(const QString& filename, bool isBin)
{
    QStringList normalized;
    for(int i = 0; i < cardType.block_size; i++)
    {
        const QString block = normalizeHex(dataList->at(i), 16);
        if(block.isEmpty())
            return false;
        if(isBin && block.contains(QLatin1Char('?')))
            return false;
        normalized.append(block);
    }

    QFile file(filename, this);
    if(!file.open(QIODevice::WriteOnly))
        return false;

    QByteArray buff;
    if(isBin)
    {
        for(int i = 0; i < cardType.block_size; i++)
        {
            QByteArray bytes;
            if(!hexStringToBytes(normalized.at(i), bytes, false) || bytes.size() != 16)
            {
                file.close();
                return false;
            }
            buff += bytes;
        }
    }
    else
    {
        for(int i = 0; i < cardType.block_size; i++)
        {
            buff += normalized.at(i).toLatin1();
            buff += '\n';
        }
    }
    const bool ret = (file.write(buff) == buff.size());
    file.close();
    return ret;
}

bool Mifare::data_saveKeyFile(const QString& filename, bool isBin)
{
    if(!isBin)
        return false;

    QStringList keysA;
    QStringList keysB;
    for(int i = 0; i < cardType.sector_size; i++)
    {
        const QString keyA = normalizeHex(keyAList->at(i), 6);
        const QString keyB = normalizeHex(keyBList->at(i), 6);
        if(keyA.isEmpty() || keyB.isEmpty() ||
           keyA.contains(QLatin1Char('?')) || keyB.contains(QLatin1Char('?')))
            return false;
        keysA.append(keyA);
        keysB.append(keyB);
    }

    QFile file(filename, this);
    if(!file.open(QIODevice::WriteOnly))
        return false;

    QByteArray buff;
    for(int i = 0; i < cardType.sector_size; i++)
    {
        QByteArray bytes;
        if(!hexStringToBytes(keysA.at(i), bytes, false) || bytes.size() != 6)
        {
            file.close();
            return false;
        }
        buff += bytes;
        if(!hexStringToBytes(keysB.at(i), bytes, false) || bytes.size() != 6)
        {
            file.close();
            return false;
        }
        buff += bytes;
    }
    const bool ret = (file.write(buff) == buff.size());
    file.close();
    return ret;
}

void Mifare::data_key2Data()
{
    for(int i = 0; i < cardType.sector_size; i++)
    {
        QString tmp = "";
        if(data_isKeyValid(keyAList->at(i)))
            tmp += keyAList->at(i);
        else
            tmp += "????????????";

        if(dataList->at(getTrailerBlockId(i)) == "")
            tmp += "FF078069"; // default control bytes
        else
            tmp += dataList->at(getTrailerBlockId(i)).midRef(12, 8);

        if(data_isKeyValid(keyBList->at(i)))
            tmp += keyBList->at(i);
        else
            tmp += "????????????";

        dataList->replace(getTrailerBlockId(i), tmp);
        data_syncWithDataWidget();
    }
}

void Mifare::data_data2Key()
{
    for(int i = 0; i < cardType.sector_size; i++)
    {
        if(dataList->at(getTrailerBlockId(i)) == "")
        {
            keyAList->replace(i, "????????????");
            keyBList->replace(i, "????????????");
        }
        else
        {
            keyAList->replace(i, dataList->at(getTrailerBlockId(i)).left(12));
            keyBList->replace(i, dataList->at(getTrailerBlockId(i)).right(12));
        }
        data_syncWithKeyWidget();
    }
}

void Mifare::data_setData(int block, const QString& data)
{
    dataList->replace(block, data);
}

void Mifare::data_setKey(int sector, KeyType keyType, const QString& key)
{
    if(keyType == KEY_A)
        keyAList->replace(sector, key);
    else
        keyBList->replace(sector, key);
}

void Mifare::data_fillKeys()
{
    for(int i = 0; i < cardType.sector_size; i++)
    {
        if(!data_isKeyValid(keyAList->at(i)))
        {
            keyAList->replace(i, "FFFFFFFFFFFF");
        }
        if(!data_isKeyValid(keyBList->at(i)))
        {
            keyBList->replace(i, "FFFFFFFFFFFF");
        }
    }
    data_syncWithKeyWidget();
}

int Mifare::data_b2s(int block)
{
    if(block >= 0 && block < 128)
        return block / 4;
    else if(block < 256)
        return (block - 128) / 16 + 32;
    else
        return -1;
}

bool Mifare::data_isACBitsValid(const QString& text, QList<quint8>* returnHalfBytes)
{
    QString input = text;
    input.remove(" ");
    if(input.length() < 6)
    {
        return false;
    }
    input = input.left(6);
    quint32 val = input.toUInt(nullptr, 16);
    QList<quint8> halfBytes;
    for(int i = 0; i < 6; i++)
    {
        // 6  7  8
        // AB CD EF->
        // {0xA, 0xB, 0xC, 0xD, 0xE, 0xF}
        // {~C2x, ~C1x, C1x, ~C3x, C3, C2}
        halfBytes.append((val >> ((5 - i) * 4)) & 0xf);
    }
    // qDebug() << val;
    if((~halfBytes[0] & 0xf) == halfBytes[5] && (~halfBytes[1] & 0xf) == halfBytes[2] && (~halfBytes[3] & 0xf) == halfBytes[4])
    {
        if(returnHalfBytes != nullptr)
            *returnHalfBytes = halfBytes;
        return true;
    }
    else
        return false;
}

QList<quint8> Mifare::data_getACBits(const QString& text) //return empty QList if the text is invalid
{
    QList<quint8> halfBytes, result;
    if(data_isACBitsValid(text, &halfBytes))
    {
        // data in halfbits:
        // {~C2x, ~C1x, C1x, ~C3x, C3, C2}
        for(int i = 0; i < 4; i++)
        {
            result.append((((halfBytes[4] >> i) & 1) << 2) | (((halfBytes[5] >> i) & 1) << 1) | (((halfBytes[2] >> i) & 1) << 0));
            // {Cx0, Cx1, Cx2, Cx3} (Cx0={C30, C20, C10})
        }
    }
    return result;
}

QString Mifare::data_getUID()
{
    if(data_isDataValid(dataList->at(0)))
        return dataList->at(0).left(8);
    else
        return "";
}

quint16 Mifare::getTrailerBlockId(quint8 sectorId, qint8 cardTypeId)
{
    if(cardTypeId == 0)
        return (card_mini.blks[sectorId] + card_mini.blk[sectorId] - 1);
    else if(cardTypeId == 1)
        return (card_1k.blks[sectorId] + card_1k.blk[sectorId] - 1);
    else if(cardTypeId == 2)
        return (card_2k.blks[sectorId] + card_2k.blk[sectorId] - 1);
    else if(cardTypeId == 4)
        return (card_4k.blks[sectorId] + card_4k.blk[sectorId] - 1);
    else
        // other cardTypeId: use current cardtype(include default -1)
        return (cardType.blks[sectorId] + cardType.blk[sectorId] - 1);
}

QString Mifare::getTraceSavePath()
{
    QVariantMap config = configMap["save sniff"].toMap();
    QString pathCmd = config["path cmd"].toString();
    QString patternText = config["path pattern"].toString();
    QRegularExpression pattern = QRegularExpression(patternText, QRegularExpression::MultilineOption);
    if(pathCmd.isEmpty() || patternText.isEmpty())
        return QString();
    QString result = util->execCMDWithOutput(pathCmd, 500);
    QRegularExpressionMatch reMatch = pattern.match(result);
    if(!reMatch.hasMatch())
        return QString();
    return reMatch.captured(1).trimmed();
}

// =========================================================================
// Stage 3: one-click read / FM11RF08S (Gen3) crack
//
// The heavy work runs on a worker thread; every PM3 command is pumped back to
// the GUI thread (the only thread that may touch the client process), which is
// also why a Cancel click is processed while a long fchk/rdbl wait is running.
// =========================================================================

mfgen3::FwFlavor Mifare::currentFlavor() const
{
    // iceman/RRG clients understand --blk/-k/-a/-b; the official client keeps
    // the legacy "chk *1 ? d" / "rdbl N A K" argument style.
    return Util::getClientType() == Util::CLIENTTYPE_ICEMAN ? mfgen3::FwFlavor::New : mfgen3::FwFlavor::Old;
}

QString Mifare::gen3WorkDir() const
{
    if(session)
    {
        const QString dir = session->workingDirectory();
        if(!dir.isEmpty())
            return dir;
    }
    return QDir::currentPath();
}

QString Mifare::gen3ExecFromWorker(const QString &cmd, int timeoutMs)
{
    if(gen3Cancel.requested())
        return QString();
    QString output;
    QMetaObject::invokeMethod(this, [this, cmd, timeoutMs, &output]() {
        if(gen3Cancel.requested())
            return;
        const quint64 serial = gen3TaskSerial;
        // The command itself is always kept in the detail log; the terminal
        // shows the complete client session separately.
        appendPm3Log(QStringLiteral("> ") + cmd);
        output = util->execCMDWithOutputInterruptible(cmd, timeoutMs, gen3Cancel.atomic(), true,
            [this, serial](const QString &chunk) {
                const QStringList lines = pm3LogFilter.feed(
                    serial, chunk, gen3Cancel.requested());
                for(const QString &line : lines)
                    appendPm3Log(line);
            });
    }, Qt::BlockingQueuedConnection);
    return output;
}

void Mifare::gen3StepFromWorker(int step, int total, const QString &text, int percent)
{
    QMetaObject::invokeMethod(this, [this, step, total, text, percent]() {
        if(ui->MF_result_progressBar)
            ui->MF_result_progressBar->setValue(qBound(0, percent, 100));
        if(ui->MF_result_stepLabel)
            ui->MF_result_stepLabel->setText(QStringLiteral("%1/%2  %3").arg(step).arg(total).arg(text));
        Q_UNUSED(step);
    }, Qt::QueuedConnection);
}

void Mifare::gen3LogFromWorker(const QString &text)
{
    QMetaObject::invokeMethod(this, [this, text]() {
        appendResultLog(text);
    }, Qt::QueuedConnection);
}

void Mifare::appendResultLog(const QString &text)
{
    appendLogLine(QStringLiteral("[GUI] "), text);
}

void Mifare::appendPm3Log(const QString &text)
{
    appendLogLine(QStringLiteral("[PM3] "), text);
}

void Mifare::appendLogLine(const QString &prefix, const QString &text)
{
    if(!ui->MF_result_logView || text.isEmpty())
        return;
    QScrollBar *bar = ui->MF_result_logView->verticalScrollBar();
    // Appending must not yank the view away while the user is reading an
    // earlier part of the log.  Only follow the tail when it was already
    // pinned to the bottom.
    const bool follow = !bar || mfgen3::Pm3LogFilter::shouldAutoScroll(bar->value(), bar->maximum());
    ui->MF_result_logView->appendPlainText(prefix + text);
    if(follow && bar)
        bar->setValue(bar->maximum());
}

void Mifare::onPm3StateChanged(bool connected, const QString &info)
{
    Q_UNUSED(info);
    pm3Connected = connected;
    updateStatusLabels();
}

QString Mifare::identityConfidenceText(mfgen3::Confidence confidence) const
{
    switch(confidence)
    {
    case mfgen3::Confidence::Confirmed:
        return tr("Confirmed");
    case mfgen3::Confidence::Suspected:
        return tr("Suspected");
    default:
        return tr("Unknown");
    }
}

QString Mifare::identityAllowedFlowText(const mfgen3::CardIdentity &identity) const
{
    if(!identity.detected)
        return tr("Identify a card first");
    if(identity.magic)
        return tr("Identification only (read-only): no key read, Gen3 probe, unlock, UID/block write or erase");

    // The 4K / Mini layouts are not the 1K Gen3 pipeline, even when the
    // read-only evidence suggests a Fudan die.
    if(identity.family == QLatin1String("classic4k"))
        return tr("Public info only; use Advanced (Classic 4K; Gen3 flow is 1K-only)");
    if(identity.family == QLatin1String("classicmini"))
        return tr("Public info only; use Advanced (Mini layout is not the 1K Gen3 flow)");

    switch(identity.kind)
    {
    case mfgen3::CardKind::FM11RF08S:
    case mfgen3::CardKind::Classic1K:
        if(identity.gen3Candidate)
            return tr("Known-key read; if no key: FM11RF08S Gen3 probe (backdoor -> nonces -> verify -> read)");
        return tr("Known-key read; if no key: Advanced (dictionary / nested / hardnested)");
    case mfgen3::CardKind::ClassicMini:
        return tr("Public info only; use Advanced (Mini layout is not the 1K Gen3 flow)");
    case mfgen3::CardKind::Classic4K:
        return tr("Public info only; use Advanced (Classic 4K; Gen3 flow is 1K-only)");
    case mfgen3::CardKind::UltralightNtag:
        return tr("Public info only; use the existing NTAG page");
    case mfgen3::CardKind::DesFire:
    case mfgen3::CardKind::Iso14443a4Cpu:
        return tr("Public info only; use the existing DESFire / ISO14443-4 CPU branch");
    case mfgen3::CardKind::Gen1A:
    case mfgen3::CardKind::Gen2:
    case mfgen3::CardKind::CUID:
    case mfgen3::CardKind::FUID:
    case mfgen3::CardKind::UFUID:
        return tr("Identification only (read-only): no key read, Gen3 probe, unlock, UID/block write or erase");
    default:
        return tr("Identification only; no automatic read flow");
    }
}

QString Mifare::quickFlowText(mfgen3::QuickFlow flow, const mfgen3::CardIdentity &identity,
                              const QString &detail) const
{
    QString text;
    switch(flow)
    {
    case mfgen3::QuickFlow::IdentificationOnly:
        text = tr("Flow: identification only (magic card, read-only)");
        break;
    case mfgen3::QuickFlow::PublicInfoOnly:
        text = tr("Flow: public info only (%1)").arg(identity.typeText.isEmpty() ? tr("unknown card") : identity.typeText);
        break;
    case mfgen3::QuickFlow::ClassicKnownKey:
        text = tr("Flow: Classic 1K known-key read");
        break;
    case mfgen3::QuickFlow::Gen3Crack:
        text = tr("Flow: FM11RF08S Gen3 (backdoor -> nonce -> verify -> read)");
        break;
    case mfgen3::QuickFlow::EvidenceVerify:
        text = tr("Flow: imported nonce evidence verification");
        break;
    default:
        text = tr("Flow: -");
        break;
    }
    if(!detail.isEmpty())
        text += QStringLiteral("  [") + detail + QLatin1Char(']');
    return text;
}

void Mifare::applyCardIdentity(const mfgen3::CardIdentity &identity)
{
    lastIdentity = identity;
    // Results produced by the Gen3 pipeline carry the family but not the byte
    // capacity; derive it from the layout for the status area.
    if(lastIdentity.capacity == 0)
    {
        switch(lastIdentity.kind)
        {
        case mfgen3::CardKind::ClassicMini:
            lastIdentity.capacity = 320;
            break;
        case mfgen3::CardKind::Classic1K:
        case mfgen3::CardKind::FM11RF08S:
            lastIdentity.capacity = 1024;
            break;
        case mfgen3::CardKind::Classic4K:
            lastIdentity.capacity = 4096;
            break;
        default:
            break;
        }
    }
    updateStatusLabels();
}

void Mifare::updateStatusLabels()
{
    if(!ui)
        return;
    if(ui->MF_status_connLabel)
    {
        ui->MF_status_connLabel->setText(pm3Connected ? tr("Connected") : tr("Not Connected"));
        ui->MF_status_connLabel->setStyleSheet(pm3Connected ? QStringLiteral("color:#39FF14;")
                                                            : QStringLiteral("color:#C0392B;"));
    }
    if(ui->MF_status_cardLabel)
        ui->MF_status_cardLabel->setText(lastIdentity.detected ? tr("Present") : tr("None"));
    if(ui->MF_status_typeLabel)
    {
        ui->MF_status_typeLabel->setText(lastIdentity.detected ? lastIdentity.typeText : QStringLiteral("-"));
        ui->MF_status_typeLabel->setToolTip(lastIdentity.detected ? lastIdentity.typeText : QString());
    }
    if(ui->MF_status_uidLabel)
        ui->MF_status_uidLabel->setText(lastIdentity.uid.isEmpty() ? QStringLiteral("-") : lastIdentity.uid);
    if(ui->MF_status_atqaLabel)
        ui->MF_status_atqaLabel->setText(lastIdentity.atqa.isEmpty() ? QStringLiteral("-") : lastIdentity.atqa);
    if(ui->MF_status_sakLabel)
        ui->MF_status_sakLabel->setText(lastIdentity.sak.isEmpty() ? QStringLiteral("-") : lastIdentity.sak);
    if(ui->MF_status_sectorLabel)
        ui->MF_status_sectorLabel->setText(lastIdentity.sectors > 0
                                          ? QString::number(lastIdentity.sectors) : QStringLiteral("-"));
    if(ui->MF_status_capacityLabel)
        ui->MF_status_capacityLabel->setText(lastIdentity.detected
                                            ? identityCapacityText(lastIdentity)
                                            : QStringLiteral("-"));
}

void Mifare::refreshCardStatus()
{
    // One read-only workflow, one `hf 14a info`.  The old implementation ran
    // identifyCard() (another hf 14a info) right after the caller had already
    // sent one; both the card-info button and the status Refresh now use this
    // single path.
    readCardInfo();
}

void Mifare::readCardInfo()
{
    if(infoRunning || gen3Running)
        return;
    if(!session || !session->isConnected())
    {
        lastIdentity = mfgen3::CardIdentity();
        updateStatusLabels();
        appendResultLog(tr("PM3 is not connected"));
        return;
    }

    infoRunning = true;
    infoCancel.reset();
    setInfoBusy(true);

    // Never let a failed/new probe inherit the previous card's labels.
    lastIdentity = mfgen3::CardIdentity();
    updateStatusLabels();

    mfgen3::CardInfoHooks hooks;
    bool lastExecOk = false;
    hooks.isConnected = [this]() { return session && session->isConnected(); };
    hooks.exec = [this, &lastExecOk](const QString &cmd, int timeoutMs) {
        return util->execCMDWithOutputInterruptible(cmd, timeoutMs, infoCancel.atomic(), true,
                                                    Util::OutputHandler(), &lastExecOk);
    };
    hooks.lastExecSucceeded = [&lastExecOk]() { return lastExecOk; };
    hooks.canceled = [this]() { return infoCancel.requested(); };
    hooks.log = [this](const QString &text) { appendResultLog(text); };
    hooks.magicDetectionAvailable = Util::getClientType() == Util::CLIENTTYPE_ICEMAN;

    mfgen3::CardInfoService service(hooks);
    const mfgen3::CardIdentity identity = service.read();

    lastIdentity = identity;
    updateStatusLabels();

    if(identity.detected)
    {
        appendResultLog(tr("Card: %1  UID: %2  ATQA: %3  SAK: %4")
                        .arg(identity.typeText, identity.uid, identity.atqa, identity.sak));
        if(identity.sectors > 0)
            appendResultLog(tr("Sectors: %1  Capacity: %2")
                            .arg(identity.sectors).arg(identityCapacityText(identity)));
        appendResultLog(tr("Confidence: %1").arg(identityConfidenceText(identity.confidence)));
        // Evidence is kept in the log only; the status area no longer has a
        // dedicated evidence column.
        if(!identity.evidence.isEmpty())
            appendResultLog(tr("Evidence: %1").arg(identity.evidence.join(QStringLiteral("; "))));
        emit cardTypeDetected(mfcardtype::typeIdForIdentity(identity), identity.uid,
                              identity.confidence == mfgen3::Confidence::Confirmed);
    }
    else
    {
        appendResultLog(tr("No card detected"));
    }

    infoRunning = false;
    infoCancel.reset();
    setInfoBusy(false);
}

void Mifare::setInfoBusy(bool busy)
{
    infoRunning = busy;
    if(ui->MF_advancedGroupBox)
        ui->MF_advancedGroupBox->setEnabled(!busy);
    if(ui->MF_status_refreshButton)
        ui->MF_status_refreshButton->setEnabled(!busy);
    if(ui->MF_Quick_cancelButton)
        ui->MF_Quick_cancelButton->setEnabled(busy || gen3Running);
}

QString Mifare::identityCapacityText(const mfgen3::CardIdentity &identity) const
{
    if(identity.capacity > 0)
        return tr("%1 B").arg(identity.capacity);
    if(identity.kind == mfgen3::CardKind::UltralightNtag)
        return tr("NFC tag (capacity unknown)");
    return QStringLiteral("-");
}

void Mifare::setTaskUiBusy(bool busy)
{
    gen3Running = busy;
    ui->MF_Quick_crackButton->setEnabled(!busy);
    ui->MF_Quick_loadButton->setEnabled(!busy);
    ui->MF_Quick_saveButton->setEnabled(!busy);
    ui->MF_Quick_cancelButton->setEnabled(busy);
    if(ui->MF_advancedGroupBox)
        ui->MF_advancedGroupBox->setEnabled(!busy);
    if(ui->MF_dataWidget)
        ui->MF_dataWidget->setEnabled(true);   // keep the image readable while idle/busy
    ui->MF_Quick_crackButton->setText(busy && gen3Task == TASK_CRACK_GEN3
                                      ? tr("Running...")
                                      : gen3ButtonIdleText);
    if(!busy)
    {
        gen3Task = -1;
        if(ui->MF_result_progressBar && ui->MF_result_progressBar->value() >= 100)
            ui->MF_result_progressBar->setValue(100);
    }
    emit gen3TaskStateChanged(busy);
}

void Mifare::quickReadCard()
{
    startGen3Task(TASK_QUICK_READ);
}

void Mifare::crackGen3Card()
{
    startGen3Task(TASK_CRACK_GEN3);
}

void Mifare::importNonceEvidence(int format)
{
    importPath.clear();
    startGen3Task(format == 0 ? TASK_IMPORT_PM3 : TASK_IMPORT_PM3GUI);
}

void Mifare::cancelGen3Task()
{
    if(infoRunning)
    {
        infoCancel.request();
        appendResultLog(tr("Cancel requested; the card-info workflow will not start the next command."));
        return;
    }
    if(!gen3Running)
        return;
    gen3Cancel.request();
    // Freeze the task-scoped log first: late output of the interrupted
    // command must not be appended after the cancel was requested.
    pm3LogFilter.suspend();
    appendResultLog(tr("Cancel requested, waiting for the current command to return..."));
}

void Mifare::startGen3Task(int task)
{
    if(gen3Running)
        return;
    if(!session || !session->isConnected())
    {
        QMessageBox::warning(parent, tr("MIFARE"), tr("PM3 is not connected. Connect the device first."));
        return;
    }
    if(task == TASK_IMPORT_PM3 || task == TASK_IMPORT_PM3GUI)
    {
        importPath = QFileDialog::getOpenFileName(parent, tr("Select a nonce evidence JSON"),
                                                 gen3WorkDir(),
                                                 tr("JSON files (*.json);;All files (*)"));
        if(importPath.isEmpty())
            return;
        // The header, not the extension, decides whether the file is accepted.
        const QString detected = mfgen3::NonceStore::detectFormat(importPath);
        const QString expected = task == TASK_IMPORT_PM3 ? QStringLiteral("pm3") : QStringLiteral("pm3gui");
        if(detected != expected)
        {
            appendResultLog(tr("Import rejected: %1 looks like \"%2\", expected \"%3\"")
                            .arg(importPath, detected, expected));
            return;
        }
    }

    gen3Cancel.reset();
    gen3Task = task;
    ++gen3TaskSerial;
    pm3LogFilter.beginTask(gen3TaskSerial);
    setTaskUiBusy(true);
    if(ui->MF_result_progressBar)
        ui->MF_result_progressBar->setValue(0);
    appendResultLog(QStringLiteral("---"));
    appendResultLog(task == TASK_QUICK_READ ? tr("Task: identify card, then cheapest read path")
                     : task == TASK_CRACK_GEN3 ? tr("Task: Crack FM11RF08S")
                     : tr("Task: verify imported nonce evidence"));

    gen3Thread = QThread::create([this, task]() { runGen3Task(task); });
    connect(gen3Thread, &QThread::finished, this, [this]() {
        gen3Thread = nullptr;
    });
    gen3Thread->start();
}

void Mifare::runGen3Task(int task)
{
    mfgen3::TaskHooks hooks;
    hooks.isConnected = [this]() { return session && session->isConnected(); };
    hooks.exec = [this](const QString &cmd, int timeoutMs) { return gen3ExecFromWorker(cmd, timeoutMs); };
    hooks.cancel = [this]() { return gen3Cancel.requested(); };
    hooks.step = [this](int step, int total, const QString &text, int percent) {
        gen3StepFromWorker(step, total, text, percent);
    };
    hooks.log = [this](const QString &text) { gen3LogFromWorker(text); };
    hooks.workDir = [this]() { return gen3WorkDir(); };
    hooks.magicDetectionAvailable = Util::getClientType() == Util::CLIENTTYPE_ICEMAN;

    mfgen3::Gen3Service service(hooks);
    mfgen3::CrackResult result;

    switch(task)
    {
    case TASK_QUICK_READ:
        result = service.quickRead(currentFlavor());
        break;
    case TASK_CRACK_GEN3:
        result = service.crackGen3(currentFlavor());
        break;
    case TASK_IMPORT_PM3:
    case TASK_IMPORT_PM3GUI:
    {
        mfgen3::NonceSet set;
        QString error;
        bool loaded = false;
        if(task == TASK_IMPORT_PM3)
        {
            mfgen3::NonceStore::Pm3Evidence evidence;
            loaded = mfgen3::NonceStore::loadPm3File(importPath, &evidence, &error);
            if(loaded)
                set = evidence.set;
        }
        else
        {
            loaded = mfgen3::NonceStore::loadPm3GuiFile(importPath, &set, &error);
        }
        if(!loaded)
        {
            result.errorCode = mfgen3::err::kNoEvidence;
            result.error = buildImportError(error);
        }
        else
        {
            set.source = task == TASK_IMPORT_PM3 ? QStringLiteral("import-pm3")
                                                 : QStringLiteral("import-pm3gui");
            result = service.verifyEvidence(set, currentFlavor());
        }
        break;
    }
    default:
        result.errorCode = mfgen3::err::kUnsupported;
        result.error = tr("Unknown task");
        break;
    }

    QMetaObject::invokeMethod(this, [this, result]() { finishGen3Task(result); }, Qt::QueuedConnection);
}

void Mifare::finishGen3Task(const mfgen3::CrackResult &result)
{
    // No PM3 output may be appended after the task finished; the summary and
    // result lines below are GUI lines.
    pm3LogFilter.endTask();
    // The task is gone: clear the one-shot cancel request so Refresh / Card
    // Info work immediately after Cancel.  The worker has already returned.
    gen3Cancel.reset();
    lastResult = result;
    lastKeySource = result.keySource;
    if(result.hasNonces)
    {
        lastNonceSet = result.nonces;
        lastNonceValid = true;
    }
    setTaskUiBusy(false);
    showResultSummary(result);
    if(result.hasDump)
        applyCrackResult(result);
    if(result.identity.detected)
    {
        applyCardIdentity(result.identity);
    }
    else if(result.errorCode == mfgen3::err::kNotConnected || result.errorCode == mfgen3::err::kNoCard)
    {
        lastIdentity = mfgen3::CardIdentity();
        updateStatusLabels();
    }
    else if(!result.cardUid.isEmpty())
    {
        lastIdentity.uid = result.cardUid;
        if(!result.cardType.isEmpty())
            lastIdentity.typeText = result.cardType;
        lastIdentity.detected = true;
        updateStatusLabels();
    }
}

QString Mifare::buildImportError(const QString &detail) const
{
    // tr("Import failed: %1")
    return tr("Import failed: %1").arg(detail);
}

void Mifare::showResultSummary(const mfgen3::CrackResult &result)
{
    if(ui->MF_result_progressBar)
        ui->MF_result_progressBar->setValue(result.success() ? 100 : ui->MF_result_progressBar->value());
    if(ui->MF_result_statusLabel)
    {
        QString status;
        QString color;
        if(result.success() && !result.partial && !result.onlyData)
        {
            status = tr("Success");
            color = QStringLiteral("#39FF14");
        }
        else if(result.hasDump && (result.partial || result.onlyData))
        {
            status = tr("Partial success");
            color = QStringLiteral("#E0A800");
        }
        else
        {
            status = tr("Failed");
            color = QStringLiteral("#C0392B");
        }
        ui->MF_result_statusLabel->setText(status);
        ui->MF_result_statusLabel->setStyleSheet(QStringLiteral("color:%1;").arg(color));
    }
    if(ui->MF_result_keyLabel)
    {
        QStringList parts;
        if(!result.backdoorKey.isEmpty())
            parts << tr("Backdoor: %1").arg(result.backdoorKey);
        if(result.recoveredKeyCount() > 0)
            parts << tr("Keys: %1/32").arg(result.recoveredKeyCount());
        else if(result.onlyData)
            parts << tr("Keys: -");
        if(result.inferredKeyCount() > 0)
            parts << tr("Inferred: %1").arg(result.inferredKeyCount());
        if(!lastKeySource.isEmpty())
            parts << tr("Source: %1").arg(lastKeySource);
        ui->MF_result_keyLabel->setText(parts.isEmpty() ? QStringLiteral("-") : parts.join(QStringLiteral("  |  ")));
    }
    if(ui->MF_result_metaLabel)
    {
        QStringList parts;
        if(result.hasDump)
            parts << tr("%1 bytes").arg(result.dump.size());
        if(result.candidateTotal > 0)
            parts << tr("%1 candidates").arg(result.candidateTotal);
        if(result.elapsedMs > 0)
            parts << tr("%1 s").arg(QString::number(result.elapsedMs / 1000.0, 'f', 1));
        ui->MF_result_metaLabel->setText(parts.isEmpty() ? QStringLiteral("-") : parts.join(QStringLiteral("  |  ")));
    }
    if(!result.error.isEmpty())
        appendResultLog(tr("Error: %1").arg(result.error));
    if(!result.note.isEmpty())
        appendResultLog(result.note);
    if(result.success() || result.hasDump)
    {
        appendResultLog(tr("Done: type=%1 uid=%2 keys=%3 dump=%4 elapsed=%5 s")
                        .arg(result.cardType.isEmpty() ? QStringLiteral("-") : result.cardType,
                             result.cardUid.isEmpty() ? QStringLiteral("-") : result.cardUid)
                        .arg(result.recoveredKeyCount())
                        .arg(result.hasDump ? result.dump.size() : 0)
                        .arg(QString::number(result.elapsedMs / 1000.0, 'f', 1)));
    }
    if(ui->MF_result_stepLabel)
        ui->MF_result_stepLabel->setText(result.success() ? tr("Done") : tr("Stopped"));
}

bool Mifare::backupCurrentImage(const QString &uid)
{
    // Back up the complete image for the current layout (Mini 20 / 1K 64 /
    // 2K 128 / 4K 256 blocks).  A 1 KB buffer here would silently truncate a
    // 4K card and the file name would not reveal it.
    bool any = false;
    const int blocks = cardType.block_size;
    const QByteArray image = mfimage::imageFromRows(*dataList, blocks, &any);
    if(!any)
        return true;    // nothing to lose; applying the result is safe

    const QString dir = QDir(gen3WorkDir()).filePath(QStringLiteral("pm3gui/tmp"));
    QDir().mkpath(dir);
    const QString name = mfimage::backupFileName(uid, cardType.typeText, blocks,
                                                 QDateTime::currentDateTime());
    const QString path = QDir(dir).filePath(name);
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        appendResultLog(tr("Backup failed: cannot write %1").arg(path));
        return false;
    }
    const qint64 written = file.write(image);
    if(written != image.size() || !file.flush())
    {
        file.close();
        appendResultLog(tr("Backup failed: incomplete write to %1").arg(path));
        return false;
    }
    file.close();
    appendResultLog(tr("Previous image (%1 bytes, %2) backed up to %3")
                    .arg(image.size()).arg(cardType.typeText, path));
    return true;
}

void Mifare::applyCrackResult(const mfgen3::CrackResult &result)
{
    if(!result.hasDump || result.dump.size() != 1024)
        return;
    // Never clear the shown image unless the complete old image is on disk.
    if(!backupCurrentImage(result.cardUid))
        return;
    // Change the model and the table row counts together.  The shell blocks
    // the radio-group signal while applying, so no recursive layout change is
    // triggered; the signal is a direct (same-thread) call.
    emit cardLayoutChangeRequested(1);
    data_clearData();
    data_clearKey();
    for(int block = 0; block < 64; ++block)
    {
        const QString hex = QString::fromLatin1(result.dump.mid(block * 16, 16).toHex()).toUpper();
        data_setData(block, hex);
    }
    for(int sector = 0; sector < 16; ++sector)
    {
        if(!result.keys[sector][0].isEmpty())
            data_setKey(sector, KEY_A, result.keys[sector][0]);
        if(!result.keys[sector][1].isEmpty())
            data_setKey(sector, KEY_B, result.keys[sector][1]);
    }
    data_syncWithDataWidget();
    data_syncWithKeyWidget();
    if(!result.keys[0][0].isEmpty())
        ui->MF_RW_keyEdit->setText(result.keys[0][0]);
}

void Mifare::showImportMenu()
{
    QMenu menu(ui->MF_Quick_loadButton);
    QAction *legacy = menu.addAction(tr("Data / key file (original dialog)..."));
    menu.addSeparator();
    QAction *pm3 = menu.addAction(tr("PM3 nonces JSON (fm11rf08s_nonces) - import & verify"));
    menu.addAction(tr("PM3GUI nonces JSON (PM3GUI_FM11_NONCES_V1) - import & verify"));
    QAction *chosen = menu.exec(QCursor::pos());
    if(chosen == nullptr)
        return;
    if(chosen == legacy)
    {
        emit requestLegacyLoad();
        return;
    }
    importNonceEvidence(chosen == pm3 ? 0 : 1);
}

void Mifare::showExportMenu()
{
    QMenu menu(ui->MF_Quick_saveButton);
    QAction *legacy = menu.addAction(tr("Data / key file (original dialog)..."));
    menu.addSeparator();
    QAction *dump = menu.addAction(tr("Dump image (.bin / .mfd)"));
    QAction *keyBin = menu.addAction(tr("Keys (key.bin, 192 bytes)"));
    QAction *keyCsv = menu.addAction(tr("Keys (CSV)"));
    QAction *keyJson = menu.addAction(tr("Keys (JSON)"));
    menu.addSeparator();
    QAction *noncePm3 = menu.addAction(tr("Nonces (PM3 client JSON)"));
    QAction *nonceGui = menu.addAction(tr("Nonces (PM3GUI JSON)"));
    QAction *report = menu.addAction(tr("Report (Markdown)"));
    QAction *chosen = menu.exec(QCursor::pos());
    if(chosen == nullptr)
        return;
    if(chosen == legacy)
    {
        emit requestLegacySave();
        return;
    }

    const bool hasDump = lastResult.hasDump && lastResult.dump.size() == 1024;
    const bool hasKeys = lastResult.recoveredKeyCount() > 0;
    const bool hasNonces = lastNonceValid;
    if((chosen == dump && !hasDump) || ((chosen == keyBin || chosen == keyCsv || chosen == keyJson) && !hasKeys)
            || ((chosen == noncePm3 || chosen == nonceGui) && !hasNonces))
    {
        QMessageBox::information(parent, tr("MIFARE"),
                                 tr("Run \"Read Card\" or \"Crack FM11RF08S\" first, or import a nonce JSON."));
        return;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString uidPart = lastResult.cardUid.isEmpty() ? QStringLiteral("card") : lastResult.cardUid;
    const QString suggested = QDir(gen3WorkDir()).filePath(QStringLiteral("mf_%1_%2").arg(uidPart, stamp));
    QString filter;
    if(chosen == dump)
        filter = tr("Dump image (*.bin *.mfd)");
    else if(chosen == keyBin)
        filter = tr("Key file (*.bin)");
    else if(chosen == keyCsv)
        filter = tr("CSV (*.csv)");
    else
        filter = tr("JSON (*.json)");

    QString path = QFileDialog::getSaveFileName(parent, tr("Export"), suggested, filter);
    if(path.isEmpty())
        return;
    if(chosen == noncePm3 || chosen == nonceGui)
        path = uniqueOutputPath(path);

    QString error;
    bool ok = true;
    if(chosen == dump)
    {
        QFile file(path);
        ok = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        if(ok)
        {
            file.write(lastResult.dump);
            file.close();
        }
        else
        {
            error = tr("cannot write %1").arg(path);
        }
    }
    else if(chosen == keyBin)
    {
        QFile file(path);
        ok = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        if(ok)
        {
            file.write(mfgen3::NonceStore::buildKeyBin(lastResult));
            file.close();
        }
        else
        {
            error = tr("cannot write %1").arg(path);
        }
    }
    else if(chosen == keyCsv)
    {
        QFile file(path);
        ok = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        if(ok)
        {
            QByteArray text = "sector,keyA,keyB\n";
            for(int s = 0; s < 16; ++s)
            {
                text += QStringLiteral("%1,%2,%3\n").arg(s)
                        .arg(lastResult.keys[s][0]).arg(lastResult.keys[s][1]).toLatin1();
            }
            file.write(text);
            file.close();
        }
        else
        {
            error = tr("cannot write %1").arg(path);
        }
    }
    else if(chosen == keyJson)
    {
        QJsonObject root;
        root.insert(QStringLiteral("uid"), lastResult.cardUid);
        root.insert(QStringLiteral("cardType"), lastResult.cardType);
        root.insert(QStringLiteral("backdoorKey"), lastResult.backdoorKey);
        QJsonArray sectors;
        for(int s = 0; s < 16; ++s)
        {
            QJsonObject entry;
            entry.insert(QStringLiteral("sector"), s);
            entry.insert(QStringLiteral("keyA"), lastResult.keys[s][0]);
            entry.insert(QStringLiteral("keyB"), lastResult.keys[s][1]);
            sectors.append(entry);
        }
        root.insert(QStringLiteral("keys"), sectors);
        QFile file(path);
        ok = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        if(ok)
        {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            file.close();
        }
        else
        {
            error = tr("cannot write %1").arg(path);
        }
    }
    else if(chosen == noncePm3)
    {
        ok = mfgen3::NonceStore::savePm3File(path, lastNonceSet, lastResult.dump, false, &error);
    }
    else if(chosen == nonceGui)
    {
        ok = mfgen3::NonceStore::savePm3GuiFile(path, lastNonceSet, &error);
    }
    else if(chosen == report)
    {
        QString text;
        text += QStringLiteral("# MIFARE task report\n\n");
        text += QStringLiteral("- card type: %1\n").arg(lastResult.cardType);
        text += QStringLiteral("- uid: %1\n").arg(lastResult.cardUid);
        text += QStringLiteral("- backdoor key: %1\n").arg(lastResult.backdoorKey);
        text += QStringLiteral("- keys: %1/32\n").arg(lastResult.recoveredKeyCount());
        text += QStringLiteral("- dump bytes: %1\n").arg(lastResult.dump.size());
        text += QStringLiteral("- candidates: %1\n").arg(lastResult.candidateTotal);
        text += QStringLiteral("- elapsed: %1 s\n").arg(QString::number(lastResult.elapsedMs / 1000.0, 'f', 1));
        text += QStringLiteral("- note: %1\n").arg(lastResult.note);
        text += QStringLiteral("- error: %1\n").arg(lastResult.error);
        QFile file(path);
        ok = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        if(ok)
        {
            file.write(text.toUtf8());
            file.close();
        }
        else
        {
            error = tr("cannot write %1").arg(path);
        }
    }

    if(ok)
        appendResultLog(tr("Exported to %1").arg(path));
    else
        appendResultLog(tr("Export failed: %1").arg(error));
}

QString Mifare::uniqueOutputPath(const QString &path) const
{
    if(!QFileInfo::exists(path))
        return path;
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QFileInfo info(path);
    return info.absolutePath() + QLatin1Char('/') + info.completeBaseName()
           + QStringLiteral("_converted_") + stamp + QLatin1Char('.') + info.suffix();
}
