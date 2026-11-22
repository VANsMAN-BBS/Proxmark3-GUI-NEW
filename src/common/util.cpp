#include "util.h"
#include "core/clientsession.h"

#include <QEventLoop>
#include <QTimer>

Util::ClientType Util::clientType = CLIENTTYPE_OFFICIAL;

int Util::rawTabIndex = 0;
QDockWidget* Util::rawDockPtr = nullptr;
Ui::MainWindow* Util::ui = nullptr;


Util::Util(QObject *parent) : QObject(parent)
{
    session = nullptr;
    isRequiringOutput = false;
    isRunning = false;
    requiredOutput = new QString();
    timeStamp = QTime::currentTime();
    qRegisterMetaType<Util::ClientType>("Util::ClientType");
}

void Util::setSession(ClientSession *session)
{
    this->session = session;
}

ClientSession *Util::clientSession() const
{
    return this->session;
}


void Util::processOutput(const QString& output)
{
//    qDebug() << "Util::processOutput:" << output;
    if(isRequiringOutput)
    {
        requiredOutput->append(output);
        timeStamp = QTime::currentTime();
    }
    emit refreshOutput(output);
}

void Util::execCMD(const QString& cmd)
{
    qDebug() << "executing: " << cmd;
    if(session && session->isConnected())
        emit write(cmd + "\n");
}

void Util::execDetached(const QString& cmd)
{
    qDebug() << "executing detached: " << cmd;
    if(session && session->isConnected())
        emit writeDetached(cmd + "\n");
}

QString Util::execCMDWithOutput(const QString& cmd, ReturnTrigger trigger, bool rawOutput)
{
    // The old implementation busy-waited in the GUI thread and refreshed a
    // QTime-based deadline on every output chunk. It also inferred completion
    // from command-specific text. The new ClientSession uses a local `rem`
    // sentinel after every queued command, so completion is deterministic.
    //
    // A local QEventLoop without user input keeps the UI responsive (repaint,
    // timers) while preventing button re-entry during a synchronous command.
    if(!session || !session->isConnected())
        return "";

    const int timeoutMs = qMax<int>(static_cast<int>(trigger.waitTime) + 5000, 15000);
    const quint64 id = session->executeCommand(cmd, timeoutMs, rawOutput);

    QEventLoop loop;
    QTimer hardTimer;
    hardTimer.setSingleShot(true);

    bool completed = false;
    bool success = false;
    bool timedOut = false;
    QString output;
    QString error;

    QMetaObject::Connection connection = connect(
        session, &ClientSession::commandFinished, &loop,
        [&](quint64 requestId, const QString &text, bool ok, bool isTimeout, const QString &err) {
            if(requestId != id)
                return;
            completed = true;
            success = ok;
            timedOut = isTimeout;
            output = text;
            error = err;
            loop.quit();
        });

    QObject::connect(&hardTimer, &QTimer::timeout, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });

    hardTimer.start(timeoutMs + 2000);
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    disconnect(connection);
    hardTimer.stop();

    if(!completed) {
        qDebug() << "command wait ended without a matching result:" << cmd;
        return rawOutput ? output : QString();
    }
    if(!success) {
        qDebug() << "command failed:" << cmd << error;
        return rawOutput ? output : QString();
    }
    return output;
}

QString Util::execCMDWithOutputInterruptible(const QString& cmd, int timeoutMs,
                                             const QAtomicInt* cancel, bool rawOutput,
                                             const OutputHandler& onOutput,
                                             bool* commandSucceeded)
{
    if(commandSucceeded)
        *commandSucceeded = false;
    if(!session || !session->isConnected())
        return QString();
    if(cancel && cancel->loadRelaxed() != 0)
        return QString();

    const quint64 id = session->executeCommand(cmd, timeoutMs, rawOutput);

    QEventLoop loop;
    QTimer hardTimer;
    QTimer cancelTimer;
    hardTimer.setSingleShot(true);
    cancelTimer.setInterval(100);

    bool completed = false;
    bool success = false;
    QString output;

    QMetaObject::Connection connection = connect(
        session, &ClientSession::commandFinished, &loop,
        [&](quint64 requestId, const QString &text, bool ok, bool, const QString &) {
            if(requestId != id)
                return;
            completed = true;
            success = ok;
            output = text;
            loop.quit();
        });
    // Same decoded stream as the terminal; the request id keeps chunks from a
    // previous (cancelled) command out of the task-scoped detail log.
    QMetaObject::Connection outputConnection;
    if(onOutput)
    {
        outputConnection = connect(
            session, &ClientSession::commandOutput, &loop,
            [&](quint64 requestId, const QString &text) {
                if(requestId != id)
                    return;
                if(cancel && cancel->loadRelaxed() != 0)
                    return;
                onOutput(text);
            });
    }
    QMetaObject::Connection cancelConnection = connect(&cancelTimer, &QTimer::timeout, &loop, [&]() {
        if(cancel && cancel->loadRelaxed() != 0)
            loop.quit();
    });
    QMetaObject::Connection hardConnection = connect(&hardTimer, &QTimer::timeout, &loop, [&]() {
        loop.quit();
    });

    hardTimer.start(timeoutMs + 2000);
    cancelTimer.start();
    // Default flags: user input (the Cancel button) keeps being delivered, so
    // the waiting loop can be interrupted from the GUI.
    loop.exec();
    disconnect(connection);
    disconnect(outputConnection);
    disconnect(cancelConnection);
    disconnect(hardConnection);
    hardTimer.stop();
    cancelTimer.stop();

    if(cancel && cancel->loadRelaxed() != 0)
        return QString();
    if(!completed || !success)
    {
        qDebug() << "interruptible command ended without success:" << cmd;
        return rawOutput ? output : QString();
    }
    if(commandSucceeded)
        *commandSucceeded = true;
    return output;
}

void Util::delay(unsigned int msec){
    if(msec == 0)
        return;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(static_cast<int>(msec));
    loop.exec(QEventLoop::ExcludeUserInputEvents);
}

Util::ClientType Util::getClientType()
{
    return Util::clientType;
}

void Util::setClientType(Util::ClientType clientType)
{
    Util::clientType = clientType;
}

void Util::setRunningState(bool st)
{
    this->isRunning = st;
}

bool Util::chooseLanguage(QSettings* guiSettings, QMainWindow* window)
{
    // make sure the GUISettings is not in any group
    QSettings* langSettings = new QSettings(":/i18n/languages.ini", QSettings::IniFormat);
    QMap<QString, QString> langMap;
    langSettings->setIniCodec("UTF-8");
    langSettings->beginGroup("Languages");
    QStringList langList = langSettings->allKeys();
    for(int i = 0; i < langList.size(); i++)
        langMap.insert(langSettings->value(langList[i]).toString(), langList[i]);
    langMap.insert(tr("Load from external file"), "(ext)");
    langSettings->endGroup();
    delete langSettings;
    bool isOk = false;
    QString selectedText = QInputDialog::getItem(window, "", tr("Choose a language:"), langMap.keys(), 0, false, &isOk);
    if(!isOk)
        return false;
    if(langMap[selectedText] == "(ext)")
    {
        QString extPath = QFileDialog::getOpenFileName(window, tr("Select the translation file:"));
        if(extPath.isEmpty())
            return false;

        guiSettings->beginGroup("language");
        guiSettings->setValue("extPath", extPath);
        guiSettings->endGroup();
    }

    guiSettings->beginGroup("language");
    guiSettings->setValue("name", langMap[selectedText]);
    guiSettings->endGroup();
    guiSettings->sync();

    return isOk;
}

void Util::gotoRawTab()
{
    Util::ui->funcTab->setCurrentIndex(Util::rawTabIndex);
    Util::rawDockPtr->setVisible(true);
    Util::rawDockPtr->raise();
}

void Util::setUI(Ui::MainWindow *ui)
{
    Util::ui = ui;
}


void Util::setRawTab(QDockWidget *dockPtr, int tabIndex)
{
    Util::rawDockPtr = dockPtr;
    Util::rawTabIndex = tabIndex;
}
