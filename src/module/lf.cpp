#include "lf.h"
#include <QJsonArray>
#include <QPushButton>
#include <QBoxLayout>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QTextStream>
#include <QDir>
#include "ui/waveformdialog.h"

const LF::LFConfig LF::defaultLFConfig;

LF::LF(Ui::MainWindow *ui, Util *addr, QWidget *parent): QObject(parent)
{
    this->parent = parent;
    util = addr;
    this->ui = ui;

    LFconfigPattern = new QRegularExpression("(\\d+)|Yes|No");
    currLFConfig = defaultLFConfig;

    QPushButton *waveButton = new QPushButton(tr("Show Waveform"), ui->LF_operationGroupBox);
    waveButton->setToolTip(tr("Export the Client GraphBuffer (data save) and plot the raw LF samples. "
                              "Requires a completed LF Read/Sniff on LF-capable firmware."));
    if(QBoxLayout *layout = qobject_cast<QBoxLayout *>(ui->LF_operationGroupBox->layout()))
        layout->addWidget(waveButton);
    connect(waveButton, &QPushButton::clicked, this, &LF::showWaveform);
}

void LF::read()
{
    QVariantMap config = configMap["read"].toMap();
    util->execCMD(config["cmd"].toString());
    Util::gotoRawTab();
    util->execCMD(config["show cmd"].toString());
}

void LF::sniff()
{
    QVariantMap config = configMap["sniff"].toMap();
    util->execCMD(config["cmd"].toString());
    Util::gotoRawTab();
    util->execCMD(config["show cmd"].toString());
}

void LF::search()
{
    QVariantMap config = configMap["search"].toMap();
    util->execCMD(config["cmd"].toString());
    Util::gotoRawTab();
}

void LF::tune()
{
    QVariantMap config = configMap["tune"].toMap();
    QString cmd = config["cmd"].toString();
    cmd.replace("<divisor>", QString::number(currLFConfig.divisor));
    util->execDetached(cmd);
    Util::gotoRawTab();
}

bool LF::getLFConfig_helper(const QVariantMap& map, QString& str, int* result)
{
    int len;
    QString flag = map["flag"].toString();
    QRegularExpressionMatch reMatch;
    if(!str.contains(flag))
        return false;
    len = str.length() - (str.indexOf(flag) + flag.length());
    str = str.right(len);
    if(map.contains("replace"))
    {
        QVariantMap table = map["replace"].toMap();
        for(auto it = table.begin(); it != table.end(); it++)
        {
            str.replace(it.key(), it.value().toString());
        }
    }
    reMatch = QRegularExpression(map["pattern"].toString()).match(str);
    if(!reMatch.hasMatch())
        return false;
    *result = reMatch.captured().toInt();
    qDebug() << *result;
    return true;

}

void LF::getLFConfig()
{

    QRegularExpressionMatch reMatch;
    QString result;
    QStringList resultList;
    int start, end, temp;
    QVariantMap config = configMap["get config"].toMap();
    QString cmd = config["cmd"].toString();
    result = util->execCMDWithOutput(cmd, 400);
    reMatch = QRegularExpression(config["field start"].toString(), QRegularExpression::MultilineOption).match(result);
    start = reMatch.hasMatch() ? reMatch.capturedEnd() : 0;
    reMatch = QRegularExpression(config["field end"].toString(), QRegularExpression::MultilineOption).match(result, start);
    end = reMatch.hasMatch() ? reMatch.capturedStart() : result.length();
    result = result.mid(start, end - start);
#if (QT_VERSION <= QT_VERSION_CHECK(5,14,0))
    resultList = result.split("\n", QString::SkipEmptyParts);
#else
    resultList = result.split("\n", Qt::SkipEmptyParts);
#endif
    qDebug() << "LF CONFIG GET\n" << resultList;
    for(auto it = resultList.begin(); it != resultList.end(); it++)
    {
        if(getLFConfig_helper(config["divisor"].toMap(), *it, &temp))
            currLFConfig.divisor = temp;
        else if(getLFConfig_helper(config["bits per sample"].toMap(), *it, &temp))
            currLFConfig.bitsPerSample = temp;
        else if(getLFConfig_helper(config["decimation"].toMap(), *it, &temp))
            currLFConfig.decimation = temp;
        else if(getLFConfig_helper(config["averaging"].toMap(), *it, &temp))
            currLFConfig.averaging = (bool)temp;
        else if(getLFConfig_helper(config["trigger threshold"].toMap(), *it, &temp))
            currLFConfig.triggerThreshold = temp;
        else if(getLFConfig_helper(config["samples to skip"].toMap(), *it, &temp))
            currLFConfig.samplesToSkip = temp;
    }
    syncWithUI();
}

void LF::setLFConfig(LF::LFConfig lfconfig)
{
    currLFConfig = lfconfig;
    QVariantMap config = configMap["set config"].toMap();
    QString cmd = config["cmd"].toString();
    cmd.replace("<divisor>", QString::number(currLFConfig.divisor));
    cmd.replace("<bits per sample>", QString::number(currLFConfig.bitsPerSample));
    cmd.replace("<decimation>", QString::number(currLFConfig.decimation));
    cmd.replace("<averaging>", currLFConfig.averaging ? "1" : "0");
    cmd.replace("<trigger threshold>", QString::number(currLFConfig.triggerThreshold));
    cmd.replace("<samples to skip>", QString::number(currLFConfig.samplesToSkip));
    util->execCMDWithOutput(cmd, 500);
    cmd = config["divisor cmd"].toString();
    cmd.replace("<divisor>", QString::number(currLFConfig.divisor));
    util->execCMDWithOutput(cmd, 500);
}

void LF::resetLFConfig()
{
    setLFConfig(defaultLFConfig);
    getLFConfig();
}

float LF::divisor2Freq(uint8_t divisor)
{
    return (12000.0 / (divisor + 1.0));
}

uint8_t LF::freq2Divisor(float freq)
{
    return ((uint16_t)(12000.0 / freq + 0.5) - 1); // uint16_t for (divisor + 1) = 256
}

void LF::syncWithUI()
{
    ui->LF_LFConf_freqDivisorBox->setValue(currLFConfig.divisor); // will trigger valueChanged()
    ui->LF_LFConf_bitsPerSampleBox->setValue(currLFConfig.bitsPerSample);
    ui->LF_LFConf_decimationBox->setValue(currLFConfig.decimation);
    ui->LF_LFConf_averagingBox->setChecked(currLFConfig.averaging);
    ui->LF_LFConf_thresholdBox->setValue(currLFConfig.triggerThreshold);
    ui->LF_LFConf_skipsBox->setValue(currLFConfig.samplesToSkip);
    emit LFfreqConfChanged(currLFConfig.divisor, false);
}

void LF::setConfigMap(const QVariantMap& configMap)
{
    this->configMap = configMap;
}

void LF::setClientWorkingDir(const QString &dir)
{
    clientWorkingDir = dir;
}

void LF::showWaveform()
{
    const QString baseName = QStringLiteral("pm3gui_lf_wave");
    const QString out = util->execCMDWithOutput(QStringLiteral("data save -f ") + baseName, 5000, true);
    if(out.isEmpty())
    {
        QMessageBox::information(parent, tr("LF Waveform"),
                                 tr("No graph data was exported.\n"
                                    "Run LF Read/Sniff first on LF-capable firmware, or load a graph buffer "
                                    "with `data load` in RawCommand."));
        return;
    }

    QString path;
    const QRegularExpressionMatch saved = QRegularExpression(
        QStringLiteral("Saved\\s+\\d+\\s+bytes\\s+to\\s+PM3\\s+file\\s+`([^`]+)`"),
        QRegularExpression::CaseInsensitiveOption).match(out);
    if(saved.hasMatch())
        path = saved.captured(1);

    if(path.isEmpty() || !QFile::exists(path))
    {
        const QStringList candidates = {
            clientWorkingDir + QLatin1Char('/') + baseName + QStringLiteral(".pm3"),
            QDir::currentPath() + QLatin1Char('/') + baseName + QStringLiteral(".pm3"),
            QDir::currentPath() + QLatin1Char('/') + baseName
        };
        for(const QString &candidate : candidates)
        {
            if(QFile::exists(candidate) && QFileInfo(candidate).size() > 0)
            {
                path = candidate;
                break;
            }
        }
    }

    if(path.isEmpty() || !QFile::exists(path))
    {
        QMessageBox::information(parent, tr("LF Waveform"),
                                 tr("The Client reported a graph export, but the .pm3 file could not be found.\n"
                                    "Check the Client working directory in Settings."));
        return;
    }

    QFile file(path);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::information(parent, tr("LF Waveform"), tr("Failed to open %1").arg(path));
        return;
    }
    QTextStream stream(&file);
    QVector<int> samples;
    while(!stream.atEnd())
    {
        const QString line = stream.readLine().trimmed();
        if(line.isEmpty())
            continue;
        bool ok = false;
        const int value = line.toInt(&ok);
        if(ok)
            samples.append(value);
        if(samples.size() > 1000000)
            break;
    }
    file.close();

    if(samples.size() < 2)
    {
        QMessageBox::information(parent, tr("LF Waveform"),
                                 tr("The graph buffer contains fewer than 2 samples; no waveform can be drawn.\n"
                                    "Run LF Read/Sniff first, or load a graph buffer with `data load`."));
        return;
    }

    WaveformDialog *dialog = new WaveformDialog(samples, QFileInfo(path).fileName(), parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

