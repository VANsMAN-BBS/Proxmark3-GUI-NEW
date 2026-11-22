#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "core/logger.h"
#include "ui/commandcatalogdelegate.h"
#include "ui/commandcatalogmodel.h"
#include "ui/rawworkspace.h"
#include "ui/settingslayout.h"
#include "ui/terminalhighlighter.h"
#include "ui/terminalpalette.h"
#include "module/mifare/mftablelayout.h"

#include <QApplication>
#include <QAbstractButton>
#include <QFile>
#include <QJsonDocument>
#include <QDirIterator>
#include <QLayout>
#include <QMouseEvent>
#include <QProcess>
#include <QSignalBlocker>
#include <QStyle>
#include <QTextStream>
#include <QVBoxLayout>

namespace
{
QString themeStyleSheet(const QString &themeName)
{
    QString path;
    if (themeName == QLatin1String("qdss_dark")) {
        path = QStringLiteral(":/qdarkstyle/dark/darkstyle.qss");
    } else if (themeName == QLatin1String("qdss_light")) {
        path = QStringLiteral(":/qdarkstyle/light/lightstyle.qss");
    }
    if (path.isEmpty()) {
        return QString();
    }

    QFile file(path);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        return QString();
    }
    QTextStream stream(&file);
    return stream.readAll();
}
}

MainWindow::MainWindow(QWidget *parent):
    QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    // The right-pane table split is declared in mainwindow.ui
    // (MF_tablesLayout stretch="3,2,0"): the data table stays dominant, the key
    // table and its compact toolbar stay below it.
    // Stage 3.3 horizontal workspace: the left pane keeps device status, the
    // live PM3 terminal and the task/progress area; the right pane holds the
    // card/key tables and the flat card-operation workspace. MifareSplitter
    // owns the stable 35 / 65 allocation (see ui/mifaresplitter.h).
    // Layout-only refinement of the Settings page (no defaults, INI keys or
    // signals are touched).
    refineSettingsLayout(ui->settingsTab);
    terminalHighlighter = new TerminalHighlighter(ui->Raw_outputEdit->document());
    // The MIFARE terminal shows the very same ClientSession/Util output stream
    // as the Raw page; it never executes a command on its own.  Keep its
    // highlighter as well so live light/dark theme changes stay in sync.
    mfTerminalHighlighter = new TerminalHighlighter(ui->MF_terminalView->document());
    catalogModel = nullptr;
    catalogCategoryGroup = nullptr;
    rawWorkspace = nullptr;
    hintPressActive = false;
    hintPressBlock = -1;
    hintPressOffset = -1;
    dockAllWindows = new QAction(tr("Dock all windows"), this);
    myInfo = new QAction("wh201906", this);
    currVersion = new QAction(tr("Ver: ") + QApplication::applicationVersion().section('.', 0, -2), this); // ignore the 4th version number
    connect(dockAllWindows, &QAction::triggered, [ = ]()
    {
        for(int i = 0; i < dockList.size(); i++)
            dockList[i]->setFloating(false);
    });
    connect(myInfo, &QAction::triggered, [ = ]()
    {
        QDesktopServices::openUrl(QUrl("https://github.com/wh201906"));
    });

    settings = new QSettings("GUIsettings.ini", QSettings::IniFormat);
    settings->setIniCodec("UTF-8");
    Logger::info(QStringLiteral("UI"), QStringLiteral("MainWindow constructing"));

    pm3 = new ClientSession(this);
    connect(QApplication::instance(), &QApplication::aboutToQuit, pm3, &ClientSession::stopClient);
    pm3state = false;
    clientWorkingDir = new QDir;

    util = new Util(this);
    util->setSession(pm3);
    Util::setUI(ui);
    mifare = new Mifare(ui, util, pm3, this);
    lf = new LF(ui, util, this);

    // The existing LF page keeps every widget and command unchanged; it simply
    // becomes the first sub-tab of a container that also hosts the text parser.
    // The outer tab index and label are preserved so rawTabIndex stays valid.
    lfParserTab = new LfParserTab(this);
    const int lfTabIndex = ui->funcTab->indexOf(ui->lfTab);
    if(lfTabIndex >= 0)
    {
        const QString lfTitle = ui->funcTab->tabText(lfTabIndex);
        QTabWidget *lfTabs = new QTabWidget(this);
        lfTabs->setObjectName(QStringLiteral("LF_subTabs"));
        ui->funcTab->removeTab(lfTabIndex);
        lfTabs->addTab(ui->lfTab, tr("LF Functions"));
        lfTabs->addTab(lfParserTab, tr("LF Parser"));
        // T55XX is an LF feature; keep the existing page and widgets intact,
        // but host it under the LF entry instead of a top-level tab.
        const int t55xxTabIndex = ui->funcTab->indexOf(ui->t55xxTab);
        if(t55xxTabIndex >= 0)
        {
            ui->funcTab->removeTab(t55xxTabIndex);
            lfTabs->addTab(ui->t55xxTab, tr("T55XX"));
        }
        ui->funcTab->insertTab(lfTabIndex, lfTabs, lfTitle);
        connect(lfParserTab, &LfParserTab::clientTextRequested, this, [this]()
        {
            lfParserTab->setClientText(ui->Raw_outputEdit->toPlainText());
        });
    }
    t55xx = new T55xx(ui, util, this);
    ntag = new NtagTab(util, pm3, this);
    // Amiibo is an NTAG215 / Type 2 feature, so it lives with the NTAG page.
    // This build has no JTAG implementation, so no JTAG tab is shown.
    QTabWidget *ntagTabs = new QTabWidget(this);
    ntagSubTabs = ntagTabs;
    ntagSimActive = false;
    ntagTabs->setObjectName(QStringLiteral("NTAG_subTabs"));
    ntagTabs->addTab(ntag, tr("NTAG"));
    amiiboPage = new AmiiboPage(util, pm3, this);
    ntagTabs->addTab(amiiboPage, tr("Amiibo"));
    ui->funcTab->insertTab(2, ntagTabs, tr("NTAG/UL"));

    connect(ntag, &NtagTab::stopSimulationRequested, this, [this]()
    {
        if(ntagSimActive)
            setNtagSimActive(false);
        pm3->restartClient();
    });
    connect(ntag, &NtagTab::simulationStateChanged, this, &MainWindow::setNtagSimActive);

    connect(amiiboPage, &AmiiboPage::stopRequested, this, [this]()
    {
        pm3->restartClient();
    });

    emrtdPage = new EMRTDPage(this);
    emrtdReconnect = false;
    ui->funcTab->insertTab(3, emrtdPage, tr("eMRTD"));
    connect(emrtdPage, &EMRTDPage::aboutToRead, this, [this]()
    {
        // Only one process may own the serial port: release ours first.
        emrtdReconnect = pm3->isConnected();
        if(emrtdReconnect)
            pm3->stopClient();
    });
    connect(emrtdPage, &EMRTDPage::readFinished, this, [this]()
    {
        if(emrtdReconnect)
        {
            emrtdReconnect = false;
            pm3->restartClient();
        }
    });

    // Read-only FeliCa transit cards (Suica/PASMO). The page never writes to
    // the card and uses the same ClientSession queue as every other page.
    felicaPage = new FelicaTab(util, pm3, this);
    ui->funcTab->insertTab(4, felicaPage, tr("FeliCa"));
    connect(pm3, &ClientSession::stateChanged, felicaPage,
            [this](bool connected, const QString &)
            {
                if (felicaPage)
                    felicaPage->setClientConnected(connected);
            });

    // Read-only T-money / Climate Card page (KS X 6924 over ISO14443-A +
    // ISO-DEP) still shares the same command queue and never issues MIFARE
    // Classic, write, authentication, or clone commands. It now lives under
    // "More Features" together with the EMV reader.
    hidSeosPage = new HidSeosPage(util, pm3, this);
    connect(pm3, &ClientSession::stateChanged, hidSeosPage,
            [this](bool connected, const QString &)
            {
                if (hidSeosPage)
                    hidSeosPage->setClientConnected(connected);
            });

    moreFeaturesTabs = new QTabWidget(this);
    moreFeaturesTabs->setObjectName(QStringLiteral("More_features_subTabs"));
    moreFeaturesTabs->addTab(hidSeosPage, tr("Tmoney"));
    emvPage = new EmvPage(this);
    moreFeaturesTabs->addTab(emvPage, tr("EMV Contactless"));
    // ISO 14443A trace/capture parser: text/file analysis only, no serial
    // port and no second Proxmark3 process.
    iso14443aPage = new Iso14443aPage(this);
    registerIso14443aPage(moreFeaturesTabs, iso14443aPage);
    iso14443aPage->setClientWorkingDirectory(clientWorkingDir->absolutePath());
    iso14443aPage->setTraceDirectoryProvider([this]() -> QStringList
    {
        QStringList dirs;
        if(clientWorkingDir)
            dirs << clientWorkingDir->absolutePath();
        // `prefs show` is a read-only client command; skip it while another
        // owner (EMV/NTAG simulation) has the serial port.
        if(mifare && pm3 && pm3->isConnected() && !pm3->isBusy() && !pm3->isDetached() && !emvReadActive && !ntagSimActive)
        {
            const QString traceDir = mifare->getTraceSavePath();
            if(!traceDir.isEmpty())
                dirs << traceDir;
        }
        return dirs;
    });
    connect(iso14443aPage, &Iso14443aPage::terminalTextRequested, this, [this]()
    {
        if(iso14443aPage)
            iso14443aPage->setTerminalText(ui->Raw_outputEdit->toPlainText());
    });
    ui->funcTab->insertTab(5, moreFeaturesTabs, tr("More Features"));

    emvReconnect = false;
    emvReadActive = false;
    connect(emvPage, &EmvPage::aboutToRead, this, [this]()
    {
        // The EMV reader starts its own proxmark3.exe. Release our client first
        // (stopClient() waits for the process to exit) and keep every other
        // page/button disabled until readFinished().
        emvReconnect = pm3->isConnected();
        setEmvReadActive(true);
        if(emvReconnect)
            pm3->stopClient();
    });
    connect(emvPage, &EmvPage::readFinished, this, [this]()
    {
        setEmvReadActive(false);
        if(emvReconnect)
        {
            emvReconnect = false;
            pm3->restartClient();
        }
    });
    emvPage->setDeviceInfo(ui->PM3_portBox->currentData().toString().isEmpty()
                           ? ui->PM3_portBox->currentText()
                           : ui->PM3_portBox->currentData().toString(),
                           ui->PM3_pathBox->currentText());
    connect(ui->PM3_portBox, &QComboBox::currentTextChanged, this, [this](const QString &text)
    {
        if(emvPage)
            emvPage->setDeviceInfo(ui->PM3_portBox->currentData().toString().isEmpty()
                                   ? text
                                   : ui->PM3_portBox->currentData().toString(),
                                   ui->PM3_pathBox->currentText());
    });
    connect(ui->PM3_pathBox, &QComboBox::currentTextChanged, this, [this](const QString &text)
    {
        if(emvPage)
            emvPage->setDeviceInfo(ui->PM3_portBox->currentData().toString().isEmpty()
                                   ? ui->PM3_portBox->currentText()
                                   : ui->PM3_portBox->currentData().toString(),
                                   text);
    });
    connect(ui->PM3_portBox, &QComboBox::currentTextChanged, this, [this](const QString &text)
    {
        if(emrtdPage)
            emrtdPage->setDeviceInfo(text, ui->PM3_pathBox->currentText());
        if(amiiboPage)
            amiiboPage->setDeviceInfo(text, ui->PM3_pathBox->currentText(),
                                      clientWorkingDir->absolutePath());
    });
    connect(ui->PM3_pathBox, &QComboBox::currentTextChanged, this, [this](const QString &text)
    {
        if(emrtdPage)
            emrtdPage->setDeviceInfo(ui->PM3_portBox->currentText(), text);
        if(amiiboPage)
            amiiboPage->setDeviceInfo(ui->PM3_portBox->currentText(), text,
                                      clientWorkingDir->absolutePath());
    });
    emrtdPage->setDeviceInfo(ui->PM3_portBox->currentText(), ui->PM3_pathBox->currentText());

    connect(lf, &LF::LFfreqConfChanged, this, &MainWindow::onLFfreqConfChanged);

    keyEventFilter = new MyEventFilter(QEvent::KeyPress);
    resizeEventFilter = new MyEventFilter(QEvent::Resize);

    portSearchTimer = new QTimer(this);
    portSearchTimer->setInterval(2000);
    connect(portSearchTimer, &QTimer::timeout, this, &MainWindow::on_portSearchTimer_timeout);
    portSearchTimer->start();

    contextMenu = new QMenu();
    contextMenu->addAction(dockAllWindows);
    contextMenu->addSeparator();
    contextMenu->addAction(myInfo);
    currVersion->setEnabled(false);
    contextMenu->addAction(currVersion);

}

MainWindow::~MainWindow()
{
    if(rawWorkspace && settings)
    {
        rawWorkspace->saveWorkspaceState(*settings);
        settings->sync();
    }
    if(emvPage)
        emvPage->shutdown();
    if(iso14443aPage)
        iso14443aPage->shutdown();
    if(pm3)
    {
        pm3->stopClient();
        delete pm3;
        pm3 = nullptr;
    }
    delete ui;
}

void MainWindow::loadConfig()
{
    QString filename = ui->Set_Client_configFileBox->currentData().toString();
    if(filename == "(ext)")
        filename = ui->Set_Client_configPathEdit->text();
    qDebug() << "config file:" << filename;
    QFile configList(filename);
    if(!configList.open(QFile::ReadOnly | QFile::Text))
    {
        Logger::error(QStringLiteral("Config"), QStringLiteral("Failed to open config: %1").arg(filename));
        QMessageBox::information(this, tr("Info"), tr("Failed to load config file"));
        return;
    }

    QByteArray configData = configList.readAll();
    QJsonDocument configJson(QJsonDocument::fromJson(configData));
    mifare->setConfigMap(configJson.object()["mifare classic"].toObject().toVariantMap());
    lf->setConfigMap(configJson.object()["lf"].toObject().toVariantMap());
    t55xx->setConfigMap(configJson.object()["t55xx"].toObject().toVariantMap());
}

void MainWindow::initUI() // will be called by main.app
{
    ui->retranslateUi(this);
    uiInit();
    signalInit();
    setState(false);
    dockInit();
    Logger::info(QStringLiteral("UI"), QStringLiteral("UI initialized"));
}

// ******************** basic functions ********************

void MainWindow::on_portSearchTimer_timeout()
{
    QStringList newPortList; // for actural port name
    QStringList newPortNameList; // for display name
    const QString hint = " *";

    foreach(const QSerialPortInfo &info, QSerialPortInfo::availablePorts())
    {
//        qDebug() << info.isNull() << info.portName() << info.description() << info.serialNumber() << info.manufacturer();
        if(!info.isNull())
        {
            QString idString = (info.description() + info.serialNumber() + info.manufacturer()).toLower();
            QString portName = info.portName();

            newPortList << portName;
            if(info.hasVendorIdentifier() && info.hasProductIdentifier())
            {
                quint16 vid = info.vendorIdentifier();
                quint16 pid = info.productIdentifier();
                if(vid == 0x9AC4 && pid == 0x4B8F)
                    portName += hint;
                else if(vid == 0x2D2D && pid == 0x504D)
                    portName += hint;
            }
            else if(idString.contains("proxmark") || idString.contains("iceman"))
                portName += hint;
            newPortNameList << portName;
        }
    }
    if(newPortList != portList) // update PM3_portBox when available ports changed
    {
        portList = newPortList;
        ui->PM3_portBox->clear();
        int selectId = -1;
        for(int i = 0; i < portList.size(); i++)
        {
            ui->PM3_portBox->addItem(newPortNameList[i], newPortList[i]);
            if(selectId == -1 && newPortNameList[i].endsWith(hint))
                selectId = i;
        }
        if(selectId != -1)
            ui->PM3_portBox->setCurrentIndex(selectId);
    }
}

void MainWindow::on_PM3_connectButton_clicked()
{
    if(emvReadActive)
        return; // serial port is owned by the EMV reader
    qDebug() << "Main:" << QThread::currentThread();

    const QComboBox* portBox = ui->PM3_portBox;
    QString port;
    if(portBox->currentText() == portBox->itemText(portBox->currentIndex()))
        // in the list
        port = portBox->currentData().toString();
    else
        // not in the list
        port = portBox->currentText();
    qDebug() << "port:" << port;
    QString startArgs = ui->Set_Client_startArgsEdit->text();
    QString clientPath = ui->PM3_pathBox->currentText();
    Logger::info(QStringLiteral("UI"),
                 QStringLiteral("Connect requested: client=%1 port=%2 args=%3")
                 .arg(clientPath, port, startArgs));
    QFileInfo clientFile(clientPath);
    bool clientExist = false;

    QStringList extList = {""};
#ifdef Q_OS_WIN
    if(clientFile.suffix().isEmpty())
    {
        QString pathExt = QProcessEnvironment::systemEnvironment().value("pathext");
        extList += pathExt.split(";", Qt::SkipEmptyParts);
        if(extList.size() == 1)
            extList += ".exe";
    }
#endif
    for(const QString& ext : extList)
    {
        QFileInfo executable(clientFile.filePath() + ext);
        if(executable.isFile())
        {
            clientExist = true;
            break;
        }
    }

    if(!clientExist)
    {
        QMessageBox::information(this, tr("Info"), tr("The client path is invalid"), QMessageBox::Ok);
        return;
    }

    // on RRG repo, if no port is specified, the client will search the available port
    if(port == "" && startArgs.contains("<port>")) // has <port>, no port
    {
        QMessageBox::information(this, tr("Info"), tr("Plz choose a port first"), QMessageBox::Ok);
        return;
    }

    if(!startArgs.contains("<port>")) // no <port>
        port = ""; // a symbol

    QStringList args = startArgs.replace("<port>", port).split(' ');
    addClientPath(clientPath);

    QProcess envSetProcess;
    QString envScriptPath = ui->Set_Client_envScriptEdit->text();
    if(envScriptPath.contains("<client dir>"))
        envScriptPath.replace("<client dir>", clientFile.absoluteDir().absolutePath());

    QFileInfo envScript(envScriptPath);
    if(envScript.exists())
    {
        qDebug() << envScript.absoluteFilePath();
        // use the shell session to keep the environment then read it
#ifdef Q_OS_WIN
        // cmd /c "<path>">>nul && set
        envSetProcess.start("cmd", {}, QProcess::Unbuffered | QProcess::ReadWrite | QProcess::Text);
        envSetProcess.write(QString("\"" + envScript.absoluteFilePath() + "\">>nul\n").toLatin1());
        envSetProcess.waitForReadyRead(10000);
        envSetProcess.readAll();
        envSetProcess.write("set\n");
#else
        // need implementation(or test if space works)
        // sh -c '. "<path>">>/dev/null && env'
        envSetProcess.start("sh -c \' . \"" + envScript.absoluteFilePath() + "\">>/dev/null && env");
#endif
        envSetProcess.waitForReadyRead(10000);
        QString envSetResult = QString(envSetProcess.readAll());
#if (QT_VERSION <= QT_VERSION_CHECK(5,14,0))
        clientEnv = envSetResult.split("\n", QString::SkipEmptyParts);
#else
        clientEnv = envSetResult.split("\n", Qt::SkipEmptyParts);
#endif
        if(clientEnv.size() > 2) // the first element is "set" and the last element is the current path
        {
            clientEnv.removeFirst();
            clientEnv.removeLast();
            pm3->setEnvironment(clientEnv);
        }
//      qDebug() << "Get Env List" << clientEnv;
    }
    else
        clientEnv.clear();

    clientWorkingDir->setPath(QApplication::applicationDirPath());
    qDebug() << clientWorkingDir->absolutePath();
    clientWorkingDir->mkpath(ui->Set_Client_workingDirEdit->text());
    qDebug() << clientWorkingDir->absolutePath();
    clientWorkingDir->cd(ui->Set_Client_workingDirEdit->text());
    qDebug() << clientWorkingDir->absolutePath();
    pm3->setWorkingDirectory(clientWorkingDir->absolutePath());
    lf->setClientWorkingDir(clientWorkingDir->absolutePath());
    if(iso14443aPage)
        iso14443aPage->setClientWorkingDirectory(clientWorkingDir->absolutePath());

    loadConfig();
    emit connectPM3(clientPath, args);

    envSetProcess.kill();
}

void MainWindow::onConnecting()
{
    pm3state = false;
    setState(false);
    setStatusBar(connectStatusBar, tr("Connecting..."));
    setStatusBar(programStatusBar, tr("Running"));
    portSearchTimer->stop();
}

void MainWindow::onClientError(const QString& message)
{
    qDebug() << "Client error:" << message;
    if(message.isEmpty())
        return;
    QMessageBox::information(this, tr("Info"), message);
}

void MainWindow::onVersionUpdated(const ClientInfo& info)
{
    clientInfo = info;
    if(!info.connected)
        return;
    QString text = info.firmwareVersion;
    if(info.hasModel())
        text += QStringLiteral(" / ") + info.model;
    if(!info.hardware.isEmpty())
        text += QStringLiteral(" / ") + info.hardware;
    setStatusBar(PM3VersionBar, text);
}

void MainWindow::onClientBusyChanged(bool busy)
{
    if(!pm3state)
        return;
    if(busy && !pm3->isDetached())
        setStatusBar(programStatusBar, tr("Running"));
    else if(!busy)
        setStatusBar(programStatusBar, tr("Idle"));
    Logger::debug(QStringLiteral("UI"), QStringLiteral("client busy=%1").arg(busy));
}

void MainWindow::onClientDetachedChanged(bool detached)
{
    Logger::info(QStringLiteral("UI"), QStringLiteral("detached state=%1").arg(detached));
    if(detached)
    {
        setStatusBar(programStatusBar, tr("Long command running"));
        setState(false);
    }
    else if(pm3state)
    {
        setStatusBar(programStatusBar, tr("Idle"));
        setState(true);
    }
}

void MainWindow::onCommandStateChanged(const QString& state, const QString& detail)
{
    Q_UNUSED(detail);
    if(state == QLatin1String("running") || state == QLatin1String("detached"))
        setStatusBar(programStatusBar, tr("Running"));
    else if(state == QLatin1String("completed"))
        setStatusBar(programStatusBar, tr("Idle"));
    else if(state == QLatin1String("timeout"))
        setStatusBar(programStatusBar, tr("Timeout"));
    else if(state == QLatin1String("failed"))
        setStatusBar(programStatusBar, tr("Error"));
}

void MainWindow::onPM3HWConnectFailed()
{
    QMessageBox::information(this, tr("Info"), tr("Failed to connect to the hardware"));
}

void MainWindow::onPM3StateChanged(bool st, const QString& info)
{
    Logger::info(QStringLiteral("UI"),
                 QStringLiteral("PM3 state=%1 info=%2").arg(st).arg(info));
    if(emrtdPage)
        emrtdPage->setDeviceInfo(ui->PM3_portBox->currentText(),
                                 ui->PM3_pathBox->currentText());
    if(amiiboPage)
    {
        amiiboPage->setDeviceInfo(ui->PM3_portBox->currentText(),
                                  ui->PM3_pathBox->currentText(),
                                  clientWorkingDir->absolutePath());
        amiiboPage->setClientConnected(st);
    }
    if(emvPage)
        emvPage->setDeviceInfo(ui->PM3_portBox->currentData().toString().isEmpty()
                               ? ui->PM3_portBox->currentText()
                               : ui->PM3_portBox->currentData().toString(),
                               ui->PM3_pathBox->currentText());
    pm3state = st;
    setState(st);
    if(st == true)
    {
        portSearchTimer->stop();
        if(clientInfo.connected)
            onVersionUpdated(clientInfo);
        else
            setStatusBar(PM3VersionBar, info);
        setStatusBar(connectStatusBar, tr("Connected"));
    }
    else
    {
        portSearchTimer->start();
        setStatusBar(PM3VersionBar, "");
        setStatusBar(connectStatusBar, tr("Not Connected"));
    }
}

void MainWindow::on_PM3_disconnectButton_clicked()
{
    if(emvReadActive)
        return;
    Logger::info(QStringLiteral("UI"), QStringLiteral("Disconnect requested"));
    pm3->stopClient();
}

void MainWindow::refreshOutput(const QString& output)
{
//    qDebug() << "MainWindow::refresh:" << output;
    const auto appendOutput = [](QPlainTextEdit *view, const QString &text)
    {
        view->moveCursor(QTextCursor::End);
        view->insertPlainText(text);
        view->moveCursor(QTextCursor::End);
    };
    appendOutput(ui->Raw_outputEdit, output);
    // Read-only mirror of the existing output stream. The MIFARE terminal does
    // not send or repeat commands; it only displays what ClientSession/Util
    // already produced.  Keep the user's scroll position when they are reading
    // an earlier part of the terminal.
    QScrollBar *mfBar = ui->MF_terminalView->verticalScrollBar();
    const bool mfFollow = !mfBar
            || mfgen3::Pm3LogFilter::shouldAutoScroll(mfBar->value(), mfBar->maximum());
    const int mfScroll = mfBar ? mfBar->value() : 0;
    appendOutput(ui->MF_terminalView, output);
    if(mfBar)
        mfBar->setValue(mfFollow ? mfBar->maximum() : mfScroll);
}

void MainWindow::on_stopButton_clicked()
{
    if(emvReadActive)
        return;
    if(!pm3state)
        on_PM3_disconnectButton_clicked();
    else
        pm3->restartClient();
}
// *********************************************************

// ******************** raw command ********************

void MainWindow::on_Raw_CMDEdit_textChanged(const QString &arg1)
{
    stashedCMDEditText = arg1;
}

void MainWindow::on_Raw_sendCMDButton_clicked()
{
    if(emvReadActive)
        return; // serial port is owned by the EMV reader
    submitRawCommand(ui->Raw_CMDEdit->text(), true);
}

void MainWindow::submitRawCommand(const QString& command, bool updateInput)
{
    if(emvReadActive || ntagSimActive)
        return;
    const QString cmd = command.trimmed();
    if(cmd.isEmpty())
        return;

    // Same path as the Send button: Util -> ClientSession::sendRawCommand ->
    // FIFO queue + completion sentinel + timeout.  Hint and catalog clicks
    // must not bypass this serialisation.
    util->execCMD(cmd);

    if(updateInput)
        refreshCMD(cmd);
    else
        recordCommandHistory(cmd);
}

void MainWindow::recordCommandHistory(const QString& cmd)
{
    if(cmd.isEmpty())
        return;
    if(ui->Raw_CMDHistoryWidget->count() == 0
       || ui->Raw_CMDHistoryWidget->item(ui->Raw_CMDHistoryWidget->count() - 1)->text() != cmd)
        ui->Raw_CMDHistoryWidget->addItem(cmd);
}

void MainWindow::setupCommandCatalog()
{
    catalogModel = new CommandCatalogModel(this);
    if(!catalogModel->loadFromResource())
    {
        Logger::warning(QStringLiteral("UI"), QStringLiteral("Command catalog resource could not be loaded"));
        ui->Raw_catalogCountLabel->setText(tr("Command catalog unavailable"));
        ui->Raw_catalogListView->setEnabled(false);
        return;
    }

    ui->Raw_catalogListView->setModel(catalogModel);
    ui->Raw_catalogListView->setItemDelegate(new CommandCatalogDelegate(this));
    ui->Raw_catalogListView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    connect(ui->Raw_catalogSearchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        catalogModel->setSearchText(text);
        updateCommandCatalogCount();
    });

    catalogCategoryGroup = new QButtonGroup(this);
    catalogCategoryGroup->setExclusive(true);
    const QList<QToolButton*> categoryButtons = {
        ui->Raw_catalogAllButton, ui->Raw_catalogHFButton, ui->Raw_catalogLFButton,
        ui->Raw_catalogHF14AButton, ui->Raw_catalogHFMIFAREButton, ui->Raw_catalogHF15Button,
        ui->Raw_catalogLFEMButton, ui->Raw_catalogOtherButton
    };
    for(int i = 0; i < categoryButtons.size(); i++)
    {
        catalogCategoryGroup->addButton(categoryButtons[i], i);
        categoryButtons[i]->setCheckable(true);
    }
    ui->Raw_catalogAllButton->setChecked(true);
    connect(catalogCategoryGroup, &QButtonGroup::idClicked, this, [this](int id) {
        const QStringList categories = CommandCatalogModel::categoryFilters();
        if(id >= 0 && id < categories.size())
            catalogModel->setCategoryFilter(categories[id]);
        updateCommandCatalogCount();
    });

    // A single click runs the command exactly like the Send button.  There is
    // no confirmation dialog and the command is not copied into the line edit.
    connect(ui->Raw_catalogListView, &QListView::clicked, this, [this](const QModelIndex &index) {
        const QString command = index.data(CommandCatalogModel::CommandRole).toString();
        submitRawCommand(command, false);
    });

    updateCommandCatalogCount();
}

void MainWindow::updateCommandCatalogCount()
{
    if(!catalogModel || !catalogModel->isLoaded())
        return;
    ui->Raw_catalogCountLabel->setText(tr("%1 commands").arg(catalogModel->visibleCount()));
}

QString MainWindow::hintCommandAt(const QPoint& viewportPos) const
{
    const QTextCursor cursor = ui->Raw_outputEdit->cursorForPosition(viewportPos);
    const QTextBlock block = cursor.block();
    const int offset = cursor.positionInBlock();
    return TerminalClassifier::hintAt(block.text(), offset);
}

void MainWindow::on_Raw_clearOutputButton_clicked()
{
    ui->Raw_outputEdit->clear();
}

void MainWindow::on_Raw_CMDHistoryBox_stateChanged(int arg1)
{
    const bool visible = arg1 == Qt::Checked;
    if(rawWorkspace)
        rawWorkspace->setHistoryVisible(visible);
    ui->Raw_CMDHistoryBox->setText(visible ? tr("History:") : QString());
}

void MainWindow::on_Raw_clearHistoryButton_clicked()
{
    ui->Raw_CMDHistoryWidget->clear();
}

void MainWindow::on_Raw_CMDHistoryWidget_itemDoubleClicked(QListWidgetItem *item)
{
    if(!item)
        return;
    // Refill only.  Chosen history entries are never executed automatically.
    ui->Raw_CMDEdit->setText(item->text());
    ui->Raw_CMDEdit->setFocus();
}

void MainWindow::sendMSG() // send command when pressing Enter
{
    if(ui->Raw_CMDEdit->hasFocus())
        on_Raw_sendCMDButton_clicked();
}


void MainWindow::refreshCMD(const QString& cmd)
{
    ui->Raw_CMDEdit->blockSignals(true);
    ui->Raw_CMDEdit->setText(cmd);
    if(cmd != "" && (ui->Raw_CMDHistoryWidget->count() == 0 || ui->Raw_CMDHistoryWidget->item(ui->Raw_CMDHistoryWidget->count() - 1)->text() != cmd))
        ui->Raw_CMDHistoryWidget->addItem(cmd);
    stashedCMDEditText = cmd;
    stashedIndex = -1;
    ui->Raw_CMDEdit->blockSignals(false);
}

void MainWindow::on_Raw_keyPressed(QObject* obj_addr, QEvent& event)
{
    if(event.type() == QEvent::KeyPress)
    {
        QKeyEvent& keyEvent = static_cast<QKeyEvent&>(event);
        if(obj_addr == ui->Raw_CMDEdit)
        {
            if(keyEvent.key() == Qt::Key_Up)
            {
                if(stashedIndex > 0)
                    stashedIndex--;
                else if(stashedIndex == -1)
                    stashedIndex = ui->Raw_CMDHistoryWidget->count() - 1;
            }
            else if(keyEvent.key() == Qt::Key_Down)
            {
                if(stashedIndex < ui->Raw_CMDHistoryWidget->count() - 1 && stashedIndex != -1)
                    stashedIndex++;
                else if(stashedIndex == ui->Raw_CMDHistoryWidget->count() - 1)
                    stashedIndex = -1;
            }
            if(keyEvent.key() == Qt::Key_Up || keyEvent.key() == Qt::Key_Down)
            {
                ui->Raw_CMDEdit->blockSignals(true);
                if(stashedIndex == -1)
                    ui->Raw_CMDEdit->setText(stashedCMDEditText);
                else
                    ui->Raw_CMDEdit->setText(ui->Raw_CMDHistoryWidget->item(stashedIndex)->text());
                ui->Raw_CMDEdit->blockSignals(false);
            }
        }
        else if(obj_addr == ui->Raw_outputEdit)
        {
            if(keyEvent.key() == Qt::Key_Up || keyEvent.key() == Qt::Key_Down)
                ui->Raw_CMDEdit->setFocus();
        }
    }
}
// *****************************************************

// ******************** mifare ********************
void MainWindow::on_MF_keyWidget_resized(QObject* obj_addr, QEvent& event)
{
    if(obj_addr == ui->MF_keyWidget && event.type() == QEvent::Resize)
    {
        QTableWidget* widget = (QTableWidget*)obj_addr;
        int keyItemWidth = widget->width();
        keyItemWidth -= widget->verticalScrollBar()->width();
        keyItemWidth -= 2 * widget->frameWidth();
        keyItemWidth -= widget->horizontalHeader()->sectionSize(0);
        widget->horizontalHeader()->resizeSection(1, keyItemWidth / 2);
        widget->horizontalHeader()->resizeSection(2, keyItemWidth / 2);
    }
}

void MainWindow::MF_onTypeBoxActivated(int index)
{
    if(!mifare)
        return;
    const int newType = cardTypeForComboIndex(index);
    const int currentType = mifare->getCardType().type;
    if(newType == currentType)
        return;

    // Growing the layout can discard rows; keep the original confirmation rule
    // (only when the new type is larger than the current one).
    int result = QMessageBox::Yes;
    if(newType > currentType)
    {
        result = QMessageBox::question(this, tr("Info"),
                                       tr("Some of the data and key will be cleared.") + "\n"
                                       + tr("Continue?"),
                                       QMessageBox::Yes | QMessageBox::No);
    }
    if(result != QMessageBox::Yes)
    {
        const QSignalBlocker blocker(ui->MF_Type_box);
        ui->MF_Type_box->setCurrentIndex(comboIndexForCardType(currentType));
        return;
    }

    mfcardtype::selectManually(&cardTypeState, newType);
    mifare->setCardType(newType);
    MF_widgetReset();
    mifare->data_syncWithDataWidget();
    mifare->data_syncWithKeyWidget();
}

void MainWindow::MF_applyCardType(int id)
{
    // One atomic transition: the model (Mifare::cardType + lists) and the two
    // table row counts change together.  The combo signal is blocked so the
    // normal user path (confirmation + reset) is not re-entered.
    if(!mifare)
        return;
    {
        const QSignalBlocker blocker(ui->MF_Type_box);
        const int index = comboIndexForCardType(id);
        if(index >= 0)
            ui->MF_Type_box->setCurrentIndex(index);
        mifare->setCardType(id);
        MF_widgetReset();
        mifare->data_syncWithDataWidget();
        mifare->data_syncWithKeyWidget();
        cardTypeState.type = id;
        cardTypeState.userLocked = false;
    }
}

int MainWindow::comboIndexForCardType(int id) const
{
    if(!ui || !ui->MF_Type_box)
        return -1;
    return ui->MF_Type_box->findData(id);
}

int MainWindow::cardTypeForComboIndex(int index) const
{
    if(!ui || !ui->MF_Type_box || index < 0)
        return 1;
    const QVariant data = ui->MF_Type_box->itemData(index);
    return data.isValid() ? data.toInt() : 1;
}

void MainWindow::MF_onCardTypeDetected(int typeId, const QString &uid, bool confirmed)
{
    if(!mifare)
        return;
    const bool changed = mfcardtype::applyDetected(&cardTypeState, typeId, uid, confirmed);
    if(changed)
        MF_applyCardType(cardTypeState.type);
}

void MainWindow::on_MF_selectAllBox_stateChanged(int arg1)
{
    ui->MF_dataWidget->blockSignals(true);
    ui->MF_selectAllBox->blockSignals(true);
    ui->MF_selectTrailerBox->blockSignals(true);
    if(arg1 == Qt::PartiallyChecked)
    {
        ui->MF_selectAllBox->setTristate(false);
        ui->MF_selectAllBox->setCheckState(Qt::Checked);
    }
    for(int i = 0; i < mifare->cardType.block_size; i++)
    {
        ui->MF_dataWidget->item(i, 1)->setCheckState(ui->MF_selectAllBox->checkState());
    }
    for(int i = 0; i < mifare->cardType.sector_size; i++)
    {
        ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(ui->MF_selectAllBox->checkState());
    }
    ui->MF_selectTrailerBox->setCheckState(ui->MF_selectAllBox->checkState());
    ui->MF_dataWidget->blockSignals(false);
    ui->MF_selectAllBox->blockSignals(false);
    ui->MF_selectTrailerBox->blockSignals(false);
}


void MainWindow::on_MF_selectTrailerBox_stateChanged(int arg1)
{
    int selectedSubBlocks = 0;

    ui->MF_dataWidget->blockSignals(true);
    ui->MF_selectAllBox->blockSignals(true);
    ui->MF_selectTrailerBox->blockSignals(true);
    if(arg1 == Qt::PartiallyChecked)
    {
        ui->MF_selectTrailerBox->setTristate(false);
        ui->MF_selectTrailerBox->setCheckState(Qt::Checked);
    }
    for(int i = 0; i < mifare->cardType.sector_size; i++)
    {
        ui->MF_dataWidget->item(mifare->cardType.blks[i] + mifare->cardType.blk[i] - 1, 1)->setCheckState(ui->MF_selectTrailerBox->checkState());
        selectedSubBlocks = 0;
        for(int j = 0; j < mifare->cardType.blk[i]; j++)
        {
            if(ui->MF_dataWidget->item(j + mifare->cardType.blks[i], 1)->checkState() == Qt::Checked)
                selectedSubBlocks++;
        }
        if(selectedSubBlocks == 0)
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(Qt::Unchecked);
        }
        else if(selectedSubBlocks == mifare->cardType.blk[i])
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(Qt::PartiallyChecked);
        }
    }

    ui->MF_dataWidget->blockSignals(false);
    ui->MF_selectAllBox->blockSignals(false);
    ui->MF_selectTrailerBox->blockSignals(false);
}


void MainWindow::on_MF_data2KeyButton_clicked()
{
    mifare->data_data2Key();
}

void MainWindow::on_MF_key2DataButton_clicked()
{
    mifare->data_key2Data();
}

void MainWindow::on_MF_fillKeysButton_clicked()
{
    mifare->data_fillKeys();
}

void MainWindow::on_MF_trailerDecoderButton_clicked()
{
    decDialog = new MF_trailerDecoderDialog(this);
    decDialog->show();
}

void MainWindow::on_MF_dataWidget_itemChanged(QTableWidgetItem *item)
{
    ui->MF_dataWidget->blockSignals(true);
    ui->MF_selectAllBox->blockSignals(true);
    ui->MF_selectTrailerBox->blockSignals(true);
    if(item->column() == 0)
    {
        int selectedSectors = 0;
        for(int i = 0; i < mifare->cardType.blk[Mifare::data_b2s(item->row())]; i++)
        {
            ui->MF_dataWidget->item(i + item->row(), 1)->setCheckState(item->checkState());
            qDebug() << i << mifare->cardType.blk[item->row()] << i + item->row() << ui->MF_dataWidget->item(i + item->row(), 1)->text();
        }
        for(int i = 0; i < mifare->cardType.sector_size; i++)
        {
            if(ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->checkState() == Qt::Checked)
            {
                selectedSectors++;
            }
        }
        if(selectedSectors == 0)
        {
            ui->MF_selectAllBox->setCheckState(Qt::Unchecked);
            ui->MF_selectTrailerBox->setCheckState(Qt::Unchecked);
        }
        else if(selectedSectors == mifare->cardType.sector_size)
        {
            ui->MF_selectAllBox->setCheckState(Qt::Checked);
            ui->MF_selectTrailerBox->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_selectAllBox->setCheckState(Qt::PartiallyChecked);
            ui->MF_selectTrailerBox->setCheckState(Qt::PartiallyChecked);
        }
    }
    else if(item->column() == 1)
    {
        int selectedSubBlocks = 0;
        int selectedBlocks = 0;
        int selectedTrailers = 0;

        for(int i = 0; i < mifare->cardType.block_size; i++)
        {
            if(ui->MF_dataWidget->item(i, 1)->checkState() == Qt::Checked)
                selectedBlocks++;
        }
        for(int i = 0; i < mifare->cardType.blk[Mifare::data_b2s(item->row())]; i++)
        {
            if(ui->MF_dataWidget->item(i + mifare->cardType.blks[Mifare::data_b2s(item->row())], 1)->checkState() == Qt::Checked)
                selectedSubBlocks++;
        }
        for(int i = 0; i < mifare->cardType.sector_size; i++)
        {
            int targetBlock = mifare->cardType.blks[i] + mifare->cardType.blk[i] - 1;
            if(ui->MF_dataWidget->item(targetBlock, 1)->checkState() == Qt::Checked)
                selectedTrailers++;
        }
        if(selectedBlocks == 0)
        {
            ui->MF_selectAllBox->setCheckState(Qt::Unchecked);
        }
        else if(selectedBlocks == mifare->cardType.block_size)
        {
            ui->MF_selectAllBox->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_selectAllBox->setCheckState(Qt::PartiallyChecked);
        }
        if(selectedSubBlocks == 0)
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[Mifare::data_b2s(item->row())], 0)->setCheckState(Qt::Unchecked);
        }
        else if(selectedSubBlocks == mifare->cardType.blk[Mifare::data_b2s(item->row())])
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[Mifare::data_b2s(item->row())], 0)->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_dataWidget->item(mifare->cardType.blks[Mifare::data_b2s(item->row())], 0)->setCheckState(Qt::PartiallyChecked);
        }
        if(selectedTrailers == 0)
        {
            ui->MF_selectTrailerBox->setCheckState(Qt::Unchecked);
        }
        else if(selectedTrailers == mifare->cardType.sector_size)
        {
            ui->MF_selectTrailerBox->setCheckState(Qt::Checked);
        }
        else
        {
            ui->MF_selectTrailerBox->setCheckState(Qt::PartiallyChecked);
        }
    }
    else if(item->column() == 2)
    {
        QString data = item->text().remove(" ").toUpper();
        if(data == "" || mifare->data_isDataValid(data) == Mifare::DATA_NOSPACE)
        {
            mifare->data_setData(item->row(), data);
        }
        else
        {
            QMessageBox::information(this, tr("Info"), tr("Data must consists of 32 Hex symbols(Whitespace is allowed)"));
        }
        mifare->data_syncWithDataWidget(false, item->row());
    }
    ui->MF_dataWidget->blockSignals(false);
    ui->MF_selectAllBox->blockSignals(false);
    ui->MF_selectTrailerBox->blockSignals(false);
}

void MainWindow::on_MF_keyWidget_itemChanged(QTableWidgetItem *item)
{
    if(item->column() == 1)
    {
        QString key = item->text().remove(" ").toUpper();
        if(key == "" || mifare->data_isKeyValid(key))
        {
            mifare->data_setKey(item->row(), Mifare::KEY_A, key);
        }
        else
        {
            QMessageBox::information(this, tr("Info"), tr("Key must consists of 12 Hex symbols(Whitespace is allowed)"));
        }
        mifare->data_syncWithKeyWidget(false, item->row(), Mifare::KEY_A);
    }
    else if(item->column() == 2)
    {
        QString key = item->text().remove(" ").toUpper();
        if(key == "" || mifare->data_isKeyValid(key))
        {
            mifare->data_setKey(item->row(), Mifare::KEY_B, key);
        }
        else
        {
            QMessageBox::information(this, tr("Info"), tr("Key must consists of 12 Hex symbols(Whitespace is allowed)"));
        }
        mifare->data_syncWithKeyWidget(false, item->row(), Mifare::KEY_B);
    }
}

void MainWindow::on_MF_File_loadButton_clicked()
{
    QString title = "";
    QString filename = "";
    if(ui->MF_File_dataButton->isChecked())
    {
        title = tr("Plz select the data file:");
        filename = QFileDialog::getOpenFileName(this, title, "./", tr("Binary Data Files(*.bin *.dump)") + ";;" + tr("Text Data Files(*.txt *.eml)") + ";;" + tr("All Files(*.*)"));
        qDebug() << filename;
        if(filename != "")
        {
            if(!mifare->data_loadDataFile(filename))
            {
                Logger::error(QStringLiteral("FileIO"), QStringLiteral("Failed to open: %1").arg(filename));
                QMessageBox::information(this, tr("Info"), tr("Failed to open") + "\n" + filename);
            }
        }
    }
    else if(ui->MF_File_keyButton->isChecked())
    {
        title = tr("Plz select the key file:");
        filename = QFileDialog::getOpenFileName(this, title, "./", tr("Binary Key Files(*.bin *.dump)") + ";;" + tr("All Files(*.*)"));
        qDebug() << filename;
        if(filename != "")
        {
            if(!mifare->data_loadKeyFile(filename))
            {
                Logger::error(QStringLiteral("FileIO"), QStringLiteral("Failed to open: %1").arg(filename));
                QMessageBox::information(this, tr("Info"), tr("Failed to open") + "\n" + filename);
            }
        }
    }

}

void MainWindow::on_MF_File_saveButton_clicked()
{

    QString title = "";
    QString filename = "";
    QString selectedType = "";
    QString defaultName = mifare->data_getUID();
    if(defaultName != "")
        defaultName += "_";
    defaultName += QDateTime::currentDateTime().toString("yyyy-MM-dd-hh-mm-ss");

    if(ui->MF_File_dataButton->isChecked())
    {
        title = tr("Plz select the location to save data file:");
        filename = QFileDialog::getSaveFileName(this, title, "./data_" + defaultName, tr("Binary Data Files(*.bin *.dump)") + ";;" + tr("Text Data Files(*.txt *.eml)"), &selectedType);
        qDebug() << filename;
        if(filename != "")
        {
            if(!mifare->data_saveDataFile(filename, selectedType == tr("Binary Data Files(*.bin *.dump)")))
            {
                Logger::error(QStringLiteral("FileIO"), QStringLiteral("Failed to save: %1").arg(filename));
                QMessageBox::information(this, tr("Info"), tr("Failed to save to") + "\n" + filename);
            }
        }
    }
    else if(ui->MF_File_keyButton->isChecked())
    {
        title = tr("Plz select the location to save key file:");
        filename = QFileDialog::getSaveFileName(this, title, "./key_" + defaultName, tr("Binary Key Files(*.bin *.dump)"), &selectedType);
        qDebug() << filename;
        if(filename != "")
        {
            if(!mifare->data_saveKeyFile(filename, selectedType == tr("Binary Key Files(*.bin *.dump)")))
            {
                Logger::error(QStringLiteral("FileIO"), QStringLiteral("Failed to save: %1").arg(filename));
                QMessageBox::information(this, tr("Info"), tr("Failed to save to") + "\n" + filename);
            }
        }
    }
    qDebug() << filename << selectedType;
}

void MainWindow::on_MF_File_clearButton_clicked()
{
    if(ui->MF_File_keyButton->isChecked())
    {
        mifare->data_clearKey();
        mifare->data_syncWithKeyWidget();
    }
    else if(ui->MF_File_dataButton->isChecked())
    {
        mifare->data_clearData();
        mifare->data_syncWithDataWidget();
    }
}

void MainWindow::on_MF_Attack_infoButton_clicked()
{
    // Unique read-card-info entry: one hf 14a info, then a conditional
    // hf mf info, both on the shared ClientSession.
    mifare->readCardInfo();
}

// ---------------------------------------------------------------------------
// Stage 3.3: MIFARE card operations (no one-click read)
// ---------------------------------------------------------------------------

void MainWindow::on_MF_Quick_crackButton_clicked()
{
    mifare->crackGen3Card();
}

void MainWindow::on_MF_Quick_loadButton_clicked()
{
    mifare->showImportMenu();
}

void MainWindow::on_MF_Quick_saveButton_clicked()
{
    mifare->showExportMenu();
}

void MainWindow::on_MF_Quick_cancelButton_clicked()
{
    mifare->cancelGen3Task();
}

void MainWindow::on_MF_status_refreshButton_clicked()
{
    mifare->refreshCardStatus();
}

void MainWindow::on_MF_result_clearButton_clicked()
{
    ui->MF_result_logView->clear();
}

void MainWindow::on_MF_result_logToggleButton_toggled(bool checked)
{
    ui->MF_result_logView->setVisible(checked);
}
void MainWindow::on_MF_Attack_chkButton_clicked()
{
    setState(false);
    mifare->chk();
    setState(true);
}

void MainWindow::on_MF_Attack_nestedButton_clicked()
{
    setState(false);
    mifare->nested();
    setState(true);
}

void MainWindow::on_MF_Attack_hardnestedButton_clicked()
{
    mifare->hardnested();
}

void MainWindow::on_MF_RW_readSelectedButton_clicked()
{
    setState(false);
    mifare->readSelected(Mifare::TARGET_MIFARE);
    setState(true);
}

void MainWindow::on_MF_RW_readBlockButton_clicked()
{
    setState(false);
    mifare->readOne(Mifare::TARGET_MIFARE);
    setState(true);
}

void MainWindow::on_MF_RW_writeBlockButton_clicked()
{
    setState(false);
    mifare->writeOne();
    setState(true);
}

void MainWindow::on_MF_RW_writeSelectedButton_clicked()
{
    setState(false);
    mifare->writeSelected(Mifare::TARGET_MIFARE);
    setState(true);
}

void MainWindow::on_MF_RW_dumpButton_clicked()
{
    mifare->dump();
}

void MainWindow::on_MF_RW_restoreButton_clicked()
{
    mifare->restore();
}

void MainWindow::on_MF_UID_readSelectedButton_clicked()
{
    setState(false);
    mifare->readSelected(Mifare::TARGET_UID);
    setState(true);
}

void MainWindow::on_MF_UID_readBlockButton_clicked()
{
    setState(false);
    mifare->readOne(Mifare::TARGET_UID);
    setState(true);
}

void MainWindow::on_MF_UID_writeSelectedButton_clicked()
{
    setState(false);
    mifare->writeSelected(Mifare::TARGET_UID);
    setState(true);
}

void MainWindow::on_MF_UID_writeBlockButton_clicked()
{
    setState(false);
    mifare->writeOne(Mifare::TARGET_UID);
    setState(true);
}

void MainWindow::on_MF_UID_wipeButton_clicked()
{
    mifare->wipeC();
}

void MainWindow::on_MF_UID_aboutUIDButton_clicked()
{
    QString msg;
    msg += tr("    Normally, the Block 0 of a typical Mifare card, which contains the UID, is locked during the manufacture. Users cannot write anything to Block 0 or set a new UID to a normal Mifare card.") + "\n";
    msg += tr("    Chinese Magic Cards(aka UID Cards) are some special cards whose Block 0 are writeable. And you can change UID by writing to it.") + "\n";
    msg += "\n";
    msg += tr("There are two versions of Chinese Magic Cards, the Gen1 and the Gen2.") + "\n";
    msg += tr("    Gen1:") + "\n" + tr("    also called UID card in China. It responses to some backdoor commands so you can access any blocks without password. The Proxmark3 has a bunch of related commands(csetblk, cgetblk, ...) to deal with this type of card, and my GUI also support these commands.") + "\n";
    msg += tr("    Gen2:") + "\n" + tr("    doesn't response to the backdoor commands, which means that a reader cannot detect whether it is a Chinese Magic Card or not by sending backdoor commands.") + "\n";
    msg += "\n";
    msg += tr("There are some types of Chinese Magic Card Gen2.") + "\n";
    msg += tr("    CUID Card:") + "\n" + tr("    the Block 0 is writeable, you can write to this block repeatedly by normal wrbl command.") + "\n";
    msg += tr("    (hf mf wrbl 0 A FFFFFFFFFFFF <the data you want to write>)") + "\n";
    msg += tr("    FUID Card:") + "\n" + tr("    you can only write to Block 0 once. After that, it seems like a typical Mifare card(Block 0 cannot be written to).") + "\n";
    msg += tr("    (some readers might try changing the Block 0, which could detect the CUID Card. In that case, you should use FUID card.)") + "\n";
    msg += tr("    UFUID Card:") + "\n" + tr("    It behaves like a CUID card(or UID card? I'm not sure) before you send some special command to lock it. Once it is locked, you cannot change its Block 0(just like a typical Mifare card).") + "\n";
    msg += "\n";
    msg += tr("    Seemingly, these Chinese Magic Cards are more easily to be compromised by Nested Attack(it takes little time to get an unknown key).") + "\n";
    QMessageBox::information(this, tr("About UID Card"), msg);
}

void MainWindow::on_MF_UID_setParaButton_clicked()
{
    setState(false);
    mifare->setParameterC();
    setState(true);
}

void MainWindow::on_MF_UID_lockButton_clicked()
{
    mifare->lockC();
}

void MainWindow::on_MF_Sim_readSelectedButton_clicked()
{
    setState(false);
    mifare->readSelected(Mifare::TARGET_EMULATOR);
    setState(true);
}

void MainWindow::on_MF_Sim_writeSelectedButton_clicked()
{
    setState(false);
    mifare->writeSelected(Mifare::TARGET_EMULATOR);
    setState(true);
}

void MainWindow::on_MF_Sim_clearButton_clicked()
{
    mifare->wipeE();
}

void MainWindow::on_MF_Sim_simButton_clicked()
{
    mifare->simulate();
}

void MainWindow::on_MF_Sniff_loadButton_clicked() // use a tmp file to support complicated path
{
    QString title = "";
    QString filename = "";
    QString defaultExtension;
    QDir clientTracePath;

    if(Util::getClientType() == Util::CLIENTTYPE_OFFICIAL)
        defaultExtension = ".trc";
    else if(Util::getClientType() == Util::CLIENTTYPE_ICEMAN)
        defaultExtension = ".trace";

    QString userTraceSavePath = mifare->getTraceSavePath();
    if(userTraceSavePath.isEmpty())
        clientTracePath = *clientWorkingDir;
    else
        clientTracePath = QDir(userTraceSavePath); // For v4.16717 and later

    title = tr("Plz select the trace file:");
    filename = QFileDialog::getOpenFileName(this, title, clientTracePath.absolutePath(), tr("Trace Files") + "(*" + defaultExtension + ")" + ";;" + tr("All Files(*.*)"));
    qDebug() << filename;
    if(filename != "")
    {
        QString tmpFile = "tmp" + QString::number(QDateTime::currentDateTimeUtc().toSecsSinceEpoch()) + defaultExtension;
        if(QFile::copy(filename, clientTracePath.absolutePath() + "/" + tmpFile))
        {
            mifare->loadSniff(tmpFile);
            util->delay(3000);
            QFile::remove(clientTracePath.absolutePath() + "/" + tmpFile);
        }
        else
        {
            Logger::error(QStringLiteral("FileIO"), QStringLiteral("Failed to open: %1").arg(filename));
                QMessageBox::information(this, tr("Info"), tr("Failed to open") + "\n" + filename);
        }
    }
}

void MainWindow::on_MF_Sniff_saveButton_clicked()
{
    QString title = "";
    QString filename = "";
    QString defaultExtension;
    QDir clientTracePath;

    if(Util::getClientType() == Util::CLIENTTYPE_OFFICIAL)
        defaultExtension = ".trc";
    else if(Util::getClientType() == Util::CLIENTTYPE_ICEMAN)
        defaultExtension = ".trace";

    QString userTraceSavePath = mifare->getTraceSavePath();
    if(userTraceSavePath.isEmpty())
        clientTracePath = *clientWorkingDir;
    else
        clientTracePath = QDir(userTraceSavePath); // For v4.16717 and later

    title = tr("Plz select the location to save trace file:");
    filename = QFileDialog::getSaveFileName(this, title, clientTracePath.absolutePath(), tr("Trace Files") + "(*" + defaultExtension + ")");
    qDebug() << filename;
    if(filename != "")
    {
        QString tmpFile = "tmp" + QString::number(QDateTime::currentDateTimeUtc().toSecsSinceEpoch()) + defaultExtension;
        mifare->saveSniff(tmpFile);
        for(int i = 0; i < 100; i++)
        {
            util->delay(100);
            if(QFile::exists(clientTracePath.absolutePath() + "/" + tmpFile))
                break;
        }
        // filename is not empty -> the user has chosen to overwrite the existing file
        if(QFile::exists(filename))
            QFile::remove(filename);
        if(!QFile::copy(clientTracePath.absolutePath() + "/" + tmpFile, filename))
        {
            Logger::error(QStringLiteral("FileIO"), QStringLiteral("Failed to save: %1").arg(filename));
                QMessageBox::information(this, tr("Info"), tr("Failed to save to") + "\n" + filename);
        }
        QFile::remove(clientTracePath.absolutePath() + "/" + tmpFile);
    }

}

void MainWindow::on_MF_Sniff_sniffButton_clicked()
{
    setState(false);
    mifare->sniff();
    setState(true);
}

void MainWindow::on_MF_14aSniff_snoopButton_clicked()
{
    setState(false);
    mifare->sniff14a();
    setState(true);
}

void MainWindow::on_MF_Sniff_listButton_clicked()
{
    mifare->list();
}

void MainWindow::MF_widgetReset()
{
    int secs = mifare->cardType.sector_size;
    int blks = mifare->cardType.block_size;
    QBrush trailerItemForeColor = QBrush(QColor(0, 160, 255));
    ui->MF_RW_blockBox->clear();
    // Silence itemChanged before the grid is rebuilt. resizeGrid() creates the
    // cells with QTableWidget::setItem(), and on_MF_dataWidget_itemChanged()
    // dereferences the sibling cells of the row it is called for. Creating the
    // first cell therefore re-enters that slot while the rest of the row does
    // not exist yet (null item() -> access violation during startup).
    ui->MF_dataWidget->blockSignals(true);
    ui->MF_keyWidget->blockSignals(true);
    ui->MF_selectAllBox->blockSignals(true);
    ui->MF_selectTrailerBox->blockSignals(true);

    // Create every cell together with the new row count; the Gen3 result path
    // can jump straight from a MINI (20/5) to a 1K (64/16) layout.
    mftable::resizeGrid(ui->MF_keyWidget, secs, 3);
    mftable::resizeGrid(ui->MF_dataWidget, blks, 3);

    for(int i = 0; i < blks; i++)
    {
        setTableItem(ui->MF_dataWidget, i, 0, "");
        setTableItem(ui->MF_dataWidget, i, 1, QString::number(i));
        ui->MF_dataWidget->item(i, 1)->setCheckState(Qt::Checked);
        setTableItem(ui->MF_dataWidget, i, 2, "");
        ui->MF_RW_blockBox->addItem(QString::number(i));
    }

    for(int i = 0; i < secs; i++)
    {
        setTableItem(ui->MF_keyWidget, i, 0, QString::number(i));
        setTableItem(ui->MF_keyWidget, i, 1, "");
        setTableItem(ui->MF_keyWidget, i, 2, "");
        setTableItem(ui->MF_dataWidget, mifare->cardType.blks[i], 0, QString::number(i));
        ui->MF_dataWidget->item(mifare->cardType.blks[i] + mifare->cardType.blk[i] - 1, 2)->setForeground(trailerItemForeColor);
        ui->MF_dataWidget->item(mifare->cardType.blks[i], 0)->setCheckState(Qt::Checked);
    }
    ui->MF_dataWidget->item(0, 2)->setForeground(QBrush(QColor(255, 160, 0)));
    ui->MF_selectAllBox->setCheckState(Qt::Checked);
    ui->MF_selectTrailerBox->setCheckState(Qt::Checked);

    ui->MF_dataWidget->blockSignals(false);
    ui->MF_keyWidget->blockSignals(false);
    ui->MF_selectAllBox->blockSignals(false);
    ui->MF_selectTrailerBox->blockSignals(false);
}
// ************************************************


// ******************** other ********************

void MainWindow::setupRawWorkspace()
{
    if(rawWorkspace)
        return;

    rawWorkspace = new RawWorkspace(ui->rawTab);
    rawWorkspace->adoptWidgets(ui->Raw_outputEdit,
                               ui->Raw_CMDHistoryBox,
                               ui->Raw_CMDHistoryWidget,
                               ui->Raw_clearHistoryButton,
                               ui->Raw_CMDEdit,
                               ui->Raw_sendCMDButton,
                               ui->Raw_clearOutputButton,
                               ui->Raw_catalogGroupBox);

    QLayout* oldLayout = ui->rawTab->layout();
    if(oldLayout)
    {
        while(QLayoutItem* item = oldLayout->takeAt(0))
            delete item; // widgets have already been reparented into the dock panels
        oldLayout->addWidget(rawWorkspace);
    }
    else
    {
        QVBoxLayout* layout = new QVBoxLayout(ui->rawTab);
        layout->setContentsMargins(5, 5, 5, 5);
        layout->addWidget(rawWorkspace);
    }

    // State is intentionally stored in the existing GUIsettings.ini; other
    // top-level docks are not touched.
    rawWorkspace->restoreWorkspaceState(*settings);
}

void MainWindow::uiInit()
{
    setupRawWorkspace();
    connect(ui->Raw_CMDEdit, &QLineEdit::returnPressed, this, &MainWindow::sendMSG);
    connect(rawWorkspace, &RawWorkspace::historyEntryChosen, this, [this](const QString& cmd)
    {
        // Refill only; never execute a history entry automatically.
        ui->Raw_CMDEdit->setText(cmd);
        ui->Raw_CMDEdit->setFocus();
    });
    ui->Raw_CMDEdit->installEventFilter(keyEventFilter);
    connect(keyEventFilter, &MyEventFilter::eventHappened, this, &MainWindow::on_Raw_keyPressed);
    ui->MF_keyWidget->installEventFilter(resizeEventFilter);
    connect(resizeEventFilter, &MyEventFilter::eventHappened, this, &MainWindow::on_MF_keyWidget_resized);
    ui->Raw_outputEdit->installEventFilter(keyEventFilter);
    ui->Raw_outputEdit->viewport()->installEventFilter(this);
    ui->Raw_outputEdit->viewport()->setMouseTracking(true);
    setupCommandCatalog();

    connectStatusBar = new QLabel(this);
    programStatusBar = new QLabel(this);
    PM3VersionBar = new QLabel(this);
    stopButton = new QPushButton(this);
    setStatusBar(connectStatusBar, tr("Not Connected"));
    setStatusBar(programStatusBar, tr("Idle"));
    setStatusBar(PM3VersionBar, "");
    stopButton->setText(tr("Stop"));
    ui->statusbar->addPermanentWidget(PM3VersionBar, 1);
    ui->statusbar->addPermanentWidget(connectStatusBar, 1);
    ui->statusbar->addPermanentWidget(programStatusBar, 1);
    ui->statusbar->addPermanentWidget(stopButton);

    ui->MF_dataWidget->setColumnWidth(0, 55);
    ui->MF_dataWidget->setColumnWidth(1, 55);

    ui->MF_keyWidget->setColumnWidth(0, 45);

    MF_widgetReset();
    // Card types live in the compact combo box on the key-table operation row.
    // The userData values are the existing internal ids (0/1/2/4).
    ui->MF_Type_box->clear();
    ui->MF_Type_box->addItem(tr("MINI"), 0);
    ui->MF_Type_box->addItem(tr("1K"), 1);
    ui->MF_Type_box->addItem(tr("2K"), 2);
    ui->MF_Type_box->addItem(tr("4K"), 4);
    cardTypeState.type = 1;   // 1K, the historical default
    ui->MF_Type_box->setCurrentIndex(comboIndexForCardType(cardTypeState.type));
    connect(ui->MF_Type_box, QOverload<int>::of(&QComboBox::activated),
            this, &MainWindow::MF_onTypeBoxActivated);

    ui->MF_keyWidget->installEventFilter(this);
    ui->MF_dataWidget->installEventFilter(this);

    ui->Set_UI_Theme_nameBox->addItem(tr("(None)"), "(none)");
    ui->Set_UI_Theme_nameBox->addItem(tr("Dark"), "qdss_dark");
    ui->Set_UI_Theme_nameBox->addItem(tr("Light"), "qdss_light");

    settings->beginGroup("UI_grpbox_preference");

    QStringList boxNames = settings->allKeys();
    QGroupBox * boxptr;
    foreach(QString name, boxNames)
    {
        boxptr = this->findChild<QGroupBox*>(name);
        // The MIFARE function areas are always visible now: their visibility
        // check boxes are gone, so stale preference keys are dropped instead of
        // collapsing the group boxes.
        if(boxptr == nullptr || !boxptr->isCheckable())
        {
            settings->remove(name);
            continue;
        }
        if(settings->value(name, true).toBool())
        {
            boxptr->setMaximumHeight(16777215);
            boxptr->setChecked(true);
        }
        else
        {
            boxptr->setMaximumHeight(20);
            boxptr->setChecked(false);
        }
    }
    settings->endGroup();

    loadClientPathList();

    ui->Set_Client_GUIWorkingDirLabel->setText(QDir::currentPath());

    settings->beginGroup("Client_Args");
    ui->Set_Client_startArgsEdit->setText(settings->value("args", "<port> -f").toString());
    settings->endGroup();

    settings->beginGroup("Client_forceButtonsEnabled");
    keepButtonsEnabled = settings->value("state", false).toBool();
    settings->endGroup();
    ui->Set_Client_forceEnabledBox->setChecked(keepButtonsEnabled);

    // the disconnect detection doesn't work well on Linux/macOS
    // So it should be disabled on these platforms
    // https://github.com/wh201906/Proxmark3GUI/issues/22
    // #22, #26, #40, #41
    settings->beginGroup("Client_keepClientActive");
#ifdef Q_OS_WIN
    keepClientActive = settings->value("state", false).toBool();
#else
    keepClientActive = settings->value("state", true).toBool();
#endif
    settings->endGroup();
    ui->Set_Client_keepClientActiveBox->setChecked(keepClientActive);

    QDir configFiles(":/config/");
    configFiles.setSorting(QDir::Name);
    const QFileInfoList configFileList = configFiles.entryInfoList();
    ui->Set_Client_configFileBox->blockSignals(true);
    for(const auto& file : configFileList)
    {
        ui->Set_Client_configFileBox->addItem(file.fileName(), file.filePath());
    }

    // Use the last one as the default one
    ui->Set_Client_configFileBox->setCurrentIndex(ui->Set_Client_configFileBox->count() - 1);
    ui->Set_Client_configFileBox->addItem(tr("External file"), "(ext)");

    int configId = -1;
    settings->beginGroup("Client_Env");
    ui->Set_Client_envScriptEdit->setText(settings->value("scriptPath").toString());
    ui->Set_Client_workingDirEdit->setText(settings->value("workingDir", "../data").toString());
    configId = ui->Set_Client_configFileBox->findData(settings->value("configFile"));
    ui->Set_Client_configPathEdit->setText(settings->value("extConfigFilePath", "config.json").toString());
    settings->endGroup();
    if(configId != -1)
        ui->Set_Client_configFileBox->setCurrentIndex(configId);
    ui->Set_Client_configFileBox->blockSignals(false);
    on_Set_Client_configFileBox_currentIndexChanged(ui->Set_Client_configFileBox->currentIndex());

    // setValue() will trigger valueChanged()
    // setValue(settings->value()) will create a nested group
    // call endGroup() before apply the value
    settings->beginGroup("UI");
    int opacity = settings->value("Opacity", 100).toInt();
    int themeId = ui->Set_UI_Theme_nameBox->findData(settings->value("Theme_Name", "(none)").toString());
    settings->endGroup();
    ui->Set_UI_Opacity_Box->setValue(opacity);
    ui->Set_UI_Theme_nameBox->setCurrentIndex((themeId == -1) ? 0 : themeId);

    settings->beginGroup("UI");
    // QApplication::font() might return wrong result
    // If fonts are not specified in config file, don't touch them.
    QString tmpFontName;
    int tmpFontSize;
    bool fontValid = false, dataFontValid = false, CMDFontValid = false;
    tmpFontName = settings->value("Font_Name", "").toString();
    tmpFontSize = settings->value("Font_Size", -1).toInt();
    if(!tmpFontName.isEmpty() && tmpFontSize != -1 && tmpFontName == QFont(tmpFontName).family())
    {
        ui->Set_UI_Font_nameBox->setCurrentFont(QFont(tmpFontName));
        ui->Set_UI_Font_sizeBox->setValue(tmpFontSize);
        fontValid = true;
    }
    // The default font should be the same as MF_dataWidget's and MF_keyWidget's.
    tmpFontName = settings->value("DataFont_Name", "Consolas").toString();
    tmpFontSize = settings->value("DataFont_Size", 12).toInt();
    if(!tmpFontName.isEmpty() && tmpFontSize != -1 && tmpFontName == QFont(tmpFontName).family())
    {
        ui->Set_UI_DataFont_nameBox->setCurrentFont(QFont(tmpFontName));
        ui->Set_UI_DataFont_sizeBox->setValue(tmpFontSize);
        dataFontValid = true;
    }
    tmpFontName = settings->value("CMDFont_Name", "").toString();
    tmpFontSize = settings->value("CMDFont_Size", -1).toInt();
    if(!tmpFontName.isEmpty() && tmpFontSize != -1 && tmpFontName == QFont(tmpFontName).family())
    {
        ui->Set_UI_CMDFont_nameBox->setCurrentFont(QFont(tmpFontName));
        ui->Set_UI_CMDFont_sizeBox->setValue(tmpFontSize);
        CMDFontValid = true;
    }
    settings->endGroup();

    if(fontValid)
        on_Set_UI_Font_setButton_clicked();
    if(dataFontValid)
        on_Set_UI_DataFont_setButton_clicked();
    if(CMDFontValid)
        on_Set_UI_CMDFont_setButton_clicked();

    ui->MF_RW_keyTypeBox->addItem("A", Mifare::KEY_A);
    ui->MF_RW_keyTypeBox->addItem("B", Mifare::KEY_B);

    on_Raw_CMDHistoryBox_stateChanged(Qt::Unchecked);

}

void MainWindow::signalInit()
{
    connect(pm3, &ClientSession::newOutput, util, &Util::processOutput);
    connect(pm3, &ClientSession::changeClientType, this, [this](bool isIceman) {
        Util::setClientType(isIceman ? Util::CLIENTTYPE_ICEMAN : Util::CLIENTTYPE_OFFICIAL);
    });
    connect(util, &Util::refreshOutput, this, &MainWindow::refreshOutput);

    connect(this, &MainWindow::connectPM3, pm3, &ClientSession::startClient);
    connect(this, &MainWindow::reconnectPM3, pm3, &ClientSession::restartClient);
    connect(this, &MainWindow::killPM3, pm3, &ClientSession::stopClient);
    connect(pm3, &ClientSession::connecting, this, &MainWindow::onConnecting);
    connect(pm3, &ClientSession::stateChanged, this, &MainWindow::onPM3StateChanged);
    connect(pm3, &ClientSession::stateChanged, this, [this](bool st, const QString&) {
        util->setRunningState(st);
    });
    connect(pm3, &ClientSession::clientError, this, &MainWindow::onClientError);
    connect(pm3, &ClientSession::versionUpdated, this, &MainWindow::onVersionUpdated);
    connect(pm3, &ClientSession::busyChanged, this, &MainWindow::onClientBusyChanged);
    connect(pm3, &ClientSession::detachedChanged, this, &MainWindow::onClientDetachedChanged);
    connect(pm3, &ClientSession::commandStateChanged, this, &MainWindow::onCommandStateChanged);

    connect(util, &Util::write, pm3, &ClientSession::sendRawCommand);
    connect(util, &Util::writeDetached, this, [this](const QString &cmd) {
        pm3->executeDetached(cmd);
    });

    // The MIFARE function areas no longer carry visibility check boxes: their
    // `clicked` connections are gone so collapsing them is impossible, and the
    // stale preference keys are dropped while the settings are read.
    connect(mifare, &Mifare::requestLegacyLoad, this, &MainWindow::on_MF_File_loadButton_clicked);
    connect(mifare, &Mifare::requestLegacySave, this, &MainWindow::on_MF_File_saveButton_clicked);
    connect(mifare, &Mifare::cardLayoutChangeRequested, this, &MainWindow::MF_applyCardType);
    connect(mifare, &Mifare::cardTypeDetected, this, &MainWindow::MF_onCardTypeDetected);
    connect(mifare, &Mifare::gen3TaskStateChanged, this, [this](bool running) {
        setState(!running);
        setStatusBar(programStatusBar, running ? tr("MIFARE task running") : tr("Idle"));
    });

    connect(stopButton, &QPushButton::clicked, this, &MainWindow::on_stopButton_clicked);

    connect(ui->Set_UI_Opacity_slider, &QSlider::valueChanged, ui->Set_UI_Opacity_Box, &QSpinBox::setValue);
}

void MainWindow::setStatusBar(QLabel * target, const QString& text)
{
    if(target == PM3VersionBar)
        target->setText(tr("HW Version:") + text);
    else if(target == connectStatusBar)
        target->setText(tr("PM3:") + text);
    else if(target == programStatusBar)
        target->setText(tr("State:") + text);
}

void MainWindow::setTableItem(QTableWidget * widget, int row, int column, const QString& text)
{
    if(widget->item(row, column) == nullptr)
        widget->setItem(row, column, new QTableWidgetItem());
    widget->item(row, column)->setText(text);
}

bool MainWindow::eventFilter(QObject * watched, QEvent * event) // drag support
{
    if(watched == ui->Raw_outputEdit->viewport())
    {
        if(event->type() == QEvent::MouseMove)
        {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            const bool overHint = !hintCommandAt(mouseEvent->pos()).isEmpty();
            ui->Raw_outputEdit->viewport()->setCursor(overHint ? Qt::PointingHandCursor
                                                               : Qt::IBeamCursor);
            if(overHint)
                return true; // QPlainTextEdit would otherwise reset the cursor to IBeam
        }
        else if(event->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if(mouseEvent->button() == Qt::LeftButton && !hintCommandAt(mouseEvent->pos()).isEmpty())
            {
                const QTextCursor cursor = ui->Raw_outputEdit->cursorForPosition(mouseEvent->pos());
                hintPressActive = true;
                hintPressBlock = cursor.block().blockNumber();
                hintPressOffset = cursor.positionInBlock();
                return true;
            }
        }
        else if(event->type() == QEvent::MouseButtonRelease)
        {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if(hintPressActive && mouseEvent->button() == Qt::LeftButton)
            {
                hintPressActive = false;
                const QTextCursor cursor = ui->Raw_outputEdit->cursorForPosition(mouseEvent->pos());
                const bool sameSpan = cursor.block().blockNumber() == hintPressBlock
                                      && qAbs(cursor.positionInBlock() - hintPressOffset) <= 1;
                hintPressBlock = -1;
                hintPressOffset = -1;
                if(sameSpan)
                {
                    const QString command = hintCommandAt(mouseEvent->pos());
                    if(!command.isEmpty())
                        submitRawCommand(command, false);
                }
                return true;
            }
        }
        else if(event->type() == QEvent::Leave)
        {
            hintPressActive = false;
            hintPressBlock = -1;
            hintPressOffset = -1;
            ui->Raw_outputEdit->viewport()->unsetCursor();
        }
    }

    if(event->type() == QEvent::DragEnter)
    {
        QDragEnterEvent* dragEvent = static_cast<QDragEnterEvent*>(event);
        dragEvent->acceptProposedAction();
        return true;
    }
    else if(event->type() == QEvent::Drop)
    {
        QDropEvent* dropEvent = static_cast<QDropEvent*>(event);
        if(watched == ui->MF_keyWidget)
        {
            const QMimeData* mime = dropEvent->mimeData();
            if(mime->hasUrls())
            {
                QList<QUrl> urls = mime->urls();
                if(urls.length() == 1)
                {
                    mifare->data_loadKeyFile(urls[0].toLocalFile());
                    return true;
                }
            }
        }
        else if(watched == ui->MF_dataWidget)
        {
            const QMimeData* mime = dropEvent->mimeData();
            if(mime->hasUrls())
            {
                QList<QUrl> urls = mime->urls();
                if(urls.length() == 1)
                {
                    mifare->data_loadDataFile(urls[0].toLocalFile());
                    return true;
                }
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::setEmvReadActive(bool active)
{
    emvReadActive = active;

    // One process, one serial port. While the EMV reader owns the device only
    // the "More Features" container stays reachable, and inside it the Tmoney
    // tab (shared ClientSession) is disabled. The EMV page itself stays
    // enabled so Cancel remains clickable.
    for(int i = 0; i < ui->funcTab->count(); i++)
    {
        QWidget *page = ui->funcTab->widget(i);
        ui->funcTab->setTabEnabled(i, !active || page == moreFeaturesTabs);
    }
    // After dockInit() the pages live in QDockWidgets and funcTab is hidden, so
    // the real guard is on the docks: only the More Features dock stays usable.
    for(QDockWidget *dock : dockList)
    {
        if(!dock)
            continue;
        const bool isMoreFeatures = (dock->widget() == moreFeaturesTabs);
        dock->setEnabled(!active || isMoreFeatures);
    }
    if(moreFeaturesTabs && hidSeosPage)
    {
        const int tmIndex = moreFeaturesTabs->indexOf(hidSeosPage);
        if(tmIndex >= 0)
            moreFeaturesTabs->setTabEnabled(tmIndex, !active);
    }

    if(active)
    {
        ui->PM3_connectButton->setEnabled(false);
        ui->PM3_disconnectButton->setEnabled(false);
        ui->Raw_sendCMDButton->setEnabled(false);
        if(stopButton)
            stopButton->setEnabled(false);
    }
    else
    {
        ui->PM3_connectButton->setEnabled(true);
        ui->PM3_disconnectButton->setEnabled(true);
        if(stopButton)
            stopButton->setEnabled(true);
        setState(pm3state);
    }
}

void MainWindow::setNtagSimActive(bool active)
{
    if(active && emvReadActive)
        return;
    ntagSimActive = active;

    // The simulator owns the client while it runs. Only the NTAG/UL container
    // (which holds the Stop Simulation button) stays reachable; every other
    // page and the raw console are disabled so no command can be queued into
    // the detached `hf mfu sim` session.
    for(int i = 0; i < ui->funcTab->count(); i++)
    {
        QWidget *page = ui->funcTab->widget(i);
        ui->funcTab->setTabEnabled(i, !active || page == ntagSubTabs);
    }
    for(QDockWidget *dock : dockList)
    {
        if(!dock)
            continue;
        const bool isNtag = (dock->widget() == ntagSubTabs);
        dock->setEnabled(!active || isNtag);
    }

    if(active)
    {
        ui->PM3_connectButton->setEnabled(false);
        ui->PM3_disconnectButton->setEnabled(false);
        ui->Raw_sendCMDButton->setEnabled(false);
        // The global Stop button also restarts the client, which ends the
        // detached simulation through the same validated path.
        if(stopButton)
            stopButton->setEnabled(true);
    }
    else
    {
        ui->PM3_connectButton->setEnabled(true);
        ui->PM3_disconnectButton->setEnabled(true);
        if(stopButton)
            stopButton->setEnabled(true);
        setState(pm3state);
    }
}

void MainWindow::setState(bool st)
{
    if(!st && pm3state)
    {
        setStatusBar(programStatusBar, tr("Running"));
    }
    else
    {
        setStatusBar(programStatusBar, tr("Idle"));
    }
    setButtonsEnabled(st || keepButtonsEnabled);
}

void MainWindow::setButtonsEnabled(bool st)
{
    ui->MF_attackGroupBox->setEnabled(st);
    ui->MF_normalGroupBox->setEnabled(st);
    ui->MF_UIDGroupBox->setEnabled(st);
    ui->MF_simGroupBox->setEnabled(st);
    ui->MF_sniffGroupBox->setEnabled(st);
    ui->Raw_CMDEdit->setEnabled(st);
    ui->Raw_sendCMDButton->setEnabled(st);
    ui->LF_LFconfigGroupBox->setEnabled(st);
    ui->LF_operationGroupBox->setEnabled(st);
}

void MainWindow::on_GroupBox_clicked(bool checked)
{
    QGroupBox* box = dynamic_cast<QGroupBox*>(sender());

    settings->beginGroup("UI_grpbox_preference");
    if(checked)
    {
        box->setMaximumHeight(16777215);
        settings->setValue(box->objectName(), true);
    }
    else
    {
        box->setMaximumHeight(20);
        settings->setValue(box->objectName(), false);
    }
    settings->endGroup();
}

void MainWindow::addClientPath(const QString& path)
{
    m_clientPathList.removeAll(path);
    m_clientPathList.prepend(path);
    while(m_clientPathList.size() > 32) // the maximum count of path items
        m_clientPathList.removeLast();
    // sync to the storage
    saveClientPathList();
    // sync to the UI
    loadClientPathList();
}

void MainWindow::loadClientPathList()
{
    m_clientPathList.clear();
    settings->beginGroup("Client_Path");
    int len = settings->beginReadArray("pathList");
    settings->endArray();
    if(settings->contains("path") && len == 0)
    {
        qDebug() << "Using old client path storage";
        m_clientPathList += settings->value("path", "proxmark3").toString();
    }
    else
    {
        int arrayLen = settings->beginReadArray("pathList");
        for(int i = 0; i < arrayLen; i++)
        {
            settings->setArrayIndex(i);
            QString path = settings->value("path").toString();
            if(!path.isEmpty())
                m_clientPathList += path;
        }
        settings->endArray();
    }
    settings->endGroup();

    ui->PM3_pathBox->clear();
    for(const QString& clientPath : qAsConst(m_clientPathList))
        ui->PM3_pathBox->addItem(clientPath);
}

void MainWindow::saveClientPathList()
{
    settings->beginGroup("Client_Path");
    if(settings->contains("path"))
    {
        qDebug() << "Upgrading client path storage";
        QString oldPath = settings->value("path").toString();
        if(!oldPath.isEmpty() && !m_clientPathList.contains(oldPath))
            m_clientPathList.append(oldPath);
        settings->remove("path");
    }

    settings->beginWriteArray("pathList");
    for(int i = 0; i < m_clientPathList.size(); i++)
    {
        settings->setArrayIndex(i);
        settings->setValue("path", m_clientPathList[i]);
    }
    settings->endArray();
    settings->endGroup();
}
// ***********************************************



void MainWindow::on_MF_Attack_darksideButton_clicked()
{
    setState(false);
    mifare->darkside();
    setState(true);
}

void MainWindow::on_Set_Client_startArgsEdit_editingFinished()
{
    settings->beginGroup("Client_Args");
    settings->setValue("args", ui->Set_Client_startArgsEdit->text());
    settings->endGroup();
}

void MainWindow::on_Set_Client_forceEnabledBox_stateChanged(int arg1)
{
    settings->beginGroup("Client_forceButtonsEnabled");
    keepButtonsEnabled = (arg1 == Qt::Checked);
    settings->setValue("state", keepButtonsEnabled);
    settings->endGroup();
    if(keepButtonsEnabled)
        setButtonsEnabled(true);
}

void MainWindow::on_Set_UI_setLanguageButton_clicked()
{
    Util::chooseLanguage(settings, this);
}

void MainWindow::on_PM3_refreshPortButton_clicked()
{
    on_portSearchTimer_timeout();
}

void MainWindow::on_Set_Client_envScriptEdit_editingFinished()
{
    settings->beginGroup("Client_Env");
    settings->setValue("scriptPath", ui->Set_Client_envScriptEdit->text());
    settings->endGroup();
}

void MainWindow::on_Set_Client_workingDirEdit_editingFinished()
{
    settings->beginGroup("Client_Env");
    settings->setValue("workingDir", ui->Set_Client_workingDirEdit->text());
    settings->endGroup();
}


void MainWindow::on_Set_Client_configPathEdit_editingFinished()
{
    settings->beginGroup("Client_Env");
    settings->setValue("extConfigFilePath", ui->Set_Client_configPathEdit->text());
    settings->endGroup();
}

void MainWindow::on_Set_Client_keepClientActiveBox_stateChanged(int arg1)
{
    settings->beginGroup("Client_keepClientActive");
    keepClientActive = (arg1 == Qt::Checked);
    settings->setValue("state", keepClientActive);
    settings->endGroup();
    // Kept for settings compatibility. Disconnect detection is now driven by
    // QProcess::finished/errorOccurred, not by serial-port occupancy.
}

void MainWindow::on_LF_LFConf_freqSlider_valueChanged(int value)
{
    onLFfreqConfChanged(value, true);
}

void MainWindow::onLFfreqConfChanged(int divisor, bool isCustomized)
{
    ui->LF_LFConf_freqDivisorBox->blockSignals(true);
    ui->LF_LFConf_freqSlider->blockSignals(true);

    if(isCustomized)
        ui->LF_LFConf_freqOtherButton->setChecked(true);
    else if(divisor == 95)
        ui->LF_LFConf_freq125kButton->setChecked(true);
    else if(divisor == 88)
        ui->LF_LFConf_freq134kButton->setChecked(true);
    ui->LF_LFConf_freqLabel->setText(tr("Actural Freq: ") + QString("%1kHz").arg(LF::divisor2Freq(divisor), 0, 'f', 3));
    ui->LF_LFConf_freqDivisorBox->setValue(divisor);
    ui->LF_LFConf_freqSlider->setValue(divisor);

    ui->LF_LFConf_freqDivisorBox->blockSignals(false);
    ui->LF_LFConf_freqSlider->blockSignals(false);
}

void MainWindow::on_LF_LFConf_freqDivisorBox_valueChanged(int arg1)
{
    onLFfreqConfChanged(arg1, true);
}

void MainWindow::on_LF_LFConf_freq125kButton_clicked()
{
    onLFfreqConfChanged(95, false);
}

void MainWindow::on_LF_LFConf_freq134kButton_clicked()
{
    onLFfreqConfChanged(88, false);
}

void MainWindow::on_LF_Op_searchButton_clicked()
{
    setState(false);
    lf->search();
    setState(true);
}

void MainWindow::on_LF_Op_readButton_clicked()
{
    setState(false);
    lf->read();
    setState(true);
}

void MainWindow::on_LF_Op_tuneButton_clicked()
{
    setState(false);
    lf->tune();
    setState(true);
}

void MainWindow::on_LF_Op_sniffButton_clicked()
{
    setState(false);
    lf->sniff();
    setState(true);
}

void MainWindow::dockInit()
{
    setDockNestingEnabled(true);
    QDockWidget* dock;
    QWidget* widget;
    int count = ui->funcTab->count();
    qDebug() << "dock count" << count;
    for(int i = 0; i < count; i++)
    {
        dock = new QDockWidget(ui->funcTab->tabText(0), this);
        qDebug() << "dock name" << ui->funcTab->tabText(0);
        dock->setFeatures(QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetMovable);// movable is necessary, otherwise the dock cannot be dragged
        dock->setAllowedAreas(Qt::BottomDockWidgetArea);
        dock->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        widget = ui->funcTab->widget(0);
        dock->setWidget(widget);
        if(widget->objectName() == "rawTab")
            Util::setRawTab(dock, i);
        addDockWidget(Qt::BottomDockWidgetArea, dock);
        if(!dockList.isEmpty())
            tabifyDockWidget(dockList[0], dock);
        dockList.append(dock);
    }
    ui->funcTab->setVisible(false);
    dockList[0]->setVisible(true);
    dockList[0]->raise();
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    contextMenu->exec(event->globalPos());
}

void MainWindow::on_LF_LFConf_getButton_clicked()
{
    setState(false);
    lf->getLFConfig();
    setState(true);
}

void MainWindow::on_LF_LFConf_setButton_clicked()
{
    LF::LFConfig config;
    setState(false);
    config.divisor = ui->LF_LFConf_freqDivisorBox->value();
    config.bitsPerSample = ui->LF_LFConf_bitsPerSampleBox->value();
    config.decimation = ui->LF_LFConf_decimationBox->value();
    config.averaging = ui->LF_LFConf_averagingBox->isChecked();
    config.triggerThreshold = ui->LF_LFConf_thresholdBox->value();
    config.samplesToSkip = ui->LF_LFConf_skipsBox->value();
    lf->setLFConfig(config);
    Util::gotoRawTab();
    setState(true);
}

void MainWindow::on_LF_LFConf_resetButton_clicked()
{
    setState(false);
    lf->resetLFConfig();
    setState(true);
}

void MainWindow::on_Set_Client_configFileBox_currentIndexChanged(int index)
{
    ui->Set_Client_configPathEdit->setVisible(ui->Set_Client_configFileBox->itemData(index).toString() == "(ext)");
    settings->beginGroup("Client_Env");
    settings->setValue("configFile", ui->Set_Client_configFileBox->currentData());
    settings->endGroup();
}


void MainWindow::on_Set_UI_Opacity_Box_valueChanged(int arg1)
{
    ui->Set_UI_Opacity_slider->blockSignals(true);
    ui->Set_UI_Opacity_slider->setValue(arg1);
    setWindowOpacity(arg1 / 100.0);
    settings->beginGroup("UI");
    settings->setValue("Opacity", ui->Set_UI_Opacity_Box->value());
    settings->endGroup();
    ui->Set_UI_Opacity_slider->blockSignals(false);
}


void MainWindow::on_Set_UI_Theme_setButton_clicked()
{
    const QString themeName = ui->Set_UI_Theme_nameBox->currentData().toString();
    settings->beginGroup("UI");
    settings->setValue("Theme_Name", themeName);
    settings->endGroup();
    settings->sync();

    // Apply immediately to every existing window: qApp's stylesheet reaches
    // the main window, nested docks, floating docks, dialogs, menus and
    // tooltips without rebuilding anything.
    qApp->setStyleSheet(themeStyleSheet(themeName));

    // QSS is re-evaluated for top-level windows on the next polish; force it
    // now for windows that are already visible (including floated docks and
    // modeless dialogs) so the switch is visible without a restart.
    const QWidgetList topLevels = QApplication::topLevelWidgets();
    for(QWidget* widget : topLevels)
    {
        if(!widget || !widget->style())
            continue;
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
    if(qApp->activePopupWidget())
        qApp->activePopupWidget()->update();

    if(terminalHighlighter)
        terminalHighlighter->setDarkBackground(TerminalPalette::detectDarkBackground());
    if(mfTerminalHighlighter)
        mfTerminalHighlighter->setDarkBackground(TerminalPalette::detectDarkBackground());
    if(rawWorkspace)
        rawWorkspace->update();
    update();
}


void MainWindow::on_Set_UI_Font_setButton_clicked()
{
    QFont font = ui->Set_UI_Font_nameBox->currentFont();
    font.setPointSize(ui->Set_UI_Font_sizeBox->value());
    QApplication::setFont(font, "QWidget");
    // Label widths and compact numeric widths are metric-derived; refresh them
    // for the new font without changing any saved value.
    refineSettingsLayout(ui->settingsTab);

    settings->beginGroup("UI");
    settings->setValue("Font_Name", ui->Set_UI_Font_nameBox->currentFont().family());
    settings->setValue("Font_Size", ui->Set_UI_Font_sizeBox->value());
    settings->endGroup();
}


void MainWindow::on_Set_UI_DataFont_setButton_clicked()
{
    QFont font = ui->Set_UI_DataFont_nameBox->currentFont();
    font.setPointSize(ui->Set_UI_DataFont_sizeBox->value());
    ui->MF_dataWidget->setFont(font);
    ui->MF_keyWidget->setFont(font);

    settings->beginGroup("UI");
    settings->setValue("DataFont_Name", ui->Set_UI_DataFont_nameBox->currentFont().family());
    settings->setValue("DataFont_Size", ui->Set_UI_DataFont_sizeBox->value());
    settings->endGroup();
}


void MainWindow::on_Set_UI_CMDFont_setButton_clicked()
{
    QFont font = ui->Set_UI_CMDFont_nameBox->currentFont();
    font.setPointSize(ui->Set_UI_CMDFont_sizeBox->value());
    ui->Raw_outputEdit->setFont(font);

    settings->beginGroup("UI");
    settings->setValue("CMDFont_Name", ui->Set_UI_CMDFont_nameBox->currentFont().family());
    settings->setValue("CMDFont_Size", ui->Set_UI_CMDFont_sizeBox->value());
    settings->endGroup();
}



