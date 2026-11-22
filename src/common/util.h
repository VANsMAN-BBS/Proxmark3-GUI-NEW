#ifndef UTIL_H
#define UTIL_H

#include <QObject>
#include <QString>
#include <QDebug>
#include <QThread>
#include <QAtomicInt>
#include <QApplication>
#include <QTime>
#include <QTimer>
#include <QMetaType>
#include <QRegularExpression>
#include <QSettings>
#include <QMainWindow>
#include <QInputDialog>
#include <QFileDialog>
#include <QDockWidget>
#include <functional>

#include "ui_mainwindow.h"

class ClientSession;

class Util : public QObject
{
    Q_OBJECT
public:
    enum ClientType
    {
        CLIENTTYPE_OFFICIAL,
        CLIENTTYPE_ICEMAN,
    };

    struct ReturnTrigger
    {
        unsigned long waitTime;
        QStringList expectedOutputs;
        ReturnTrigger(unsigned long time)
        {
            waitTime = time;
            expectedOutputs = QStringList();
        }
        ReturnTrigger(const QStringList& outputs)
        {
            waitTime = 10000;
            expectedOutputs = outputs;
        }
        ReturnTrigger(unsigned long time, const QStringList& outputs)
        {
            waitTime = time;
            expectedOutputs = outputs;
        }
    };

    Q_ENUM(Util::ClientType)

    explicit Util(QObject *parent = nullptr);

    void setSession(ClientSession *session);
    ClientSession *clientSession() const;
    void execCMD(const QString& cmd);
    void execDetached(const QString& cmd);
    QString execCMDWithOutput(const QString& cmd, ReturnTrigger trigger = 10000, bool rawOutput = false);
    /**
     * Same as execCMDWithOutput(), but keeps delivering user input while the
     * command runs and stops waiting as soon as *cancel becomes non-zero.
     *
     * Used by the MIFARE one-click tasks: they run on a worker thread and call
     * back into the GUI thread for every command, so a Cancel click has to be
     * processed by the same nested event loop that is waiting for the client.
     * An empty return value means "no output" (failure, timeout or cancel).
     */
    /** Receives decoded output chunks of the command being waited for. */
    typedef std::function<void(const QString&)> OutputHandler;

    QString execCMDWithOutputInterruptible(const QString& cmd, int timeoutMs,
                                           const QAtomicInt* cancel, bool rawOutput = true,
                                           const OutputHandler& onOutput = OutputHandler(),
                                           bool* commandSucceeded = nullptr);
    void delay(unsigned int msec);
    static ClientType getClientType();
    static int rawTabIndex;
    static QDockWidget* rawDockPtr;
    static bool chooseLanguage(QSettings *guiSettings, QMainWindow *window = nullptr);
public slots:
    void processOutput(const QString& output);
    static void setClientType(Util::ClientType clientType);
    void setRunningState(bool st);
    static void gotoRawTab();
    static void setUI(Ui::MainWindow *ui);
    static void setRawTab(QDockWidget* dockPtr, int tabIndex);
private:
    ClientSession* session;
    bool isRequiringOutput;
    bool isRunning;
    QString* requiredOutput;
    QTime timeStamp;
    static ClientType clientType;
    static Ui::MainWindow *ui;
signals:
    void refreshOutput(const QString& output);
    void write(QString data); // connected to ClientSession::sendRawCommand
    void writeDetached(QString data); // connected to ClientSession::executeDetached
};

#endif // UTIL_H
