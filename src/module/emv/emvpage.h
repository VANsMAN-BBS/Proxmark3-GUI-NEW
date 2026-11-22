#ifndef EMV_EMVPAGE_H
#define EMV_EMVPAGE_H

#include <QWidget>
#include <QPointer>

#include "emvtypes.h"

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QThread;

class EmvWorker;

/**
 * "More Features > EMV Contactless" page.
 *
 * Front-end for the bundled Python `emv reader` integration. The page owns no
 * serial port: it emits aboutToRead() so MainWindow can stop the shared
 * ClientSession, then runs EmvWorker in its own QThread. readFinished() lets
 * MainWindow restart the client if it was connected before the read.
 */
class EmvPage : public QWidget
{
    Q_OBJECT
public:
    explicit EmvPage(QWidget *parent = nullptr);
    ~EmvPage() override;

    void setDeviceInfo(const QString &port, const QString &clientPath);
    bool isReading() const;
    void shutdown();

signals:
    /** Emitted before the reader process starts; release the serial port. */
    void aboutToRead();
    /** Emitted after the reader process finished (success, failure, cancel). */
    void readFinished();
    void readingChanged(bool reading);

private slots:
    void readCard();
    void cancelRead();
    void clearView();
    void exportCsv();
    void exportJson();
    void onStarted(const QString &port);
    void onStatus(const QString &message);
    void onRecord(const Emv::Txn &txn);
    void onWarning(const QString &warning);
    void onFinished(const Emv::ReadResult &result);

private:
    void buildUi();
    void setBusy(bool busy);
    void resetView();
    void updateInfo();
    void setStatus(const QString &message);
    void appendDetail(const QString &line);
    void populateTable();
    void startWorker();
    void stopThread();
    QString stopReasonText(const QString &reason) const;
    QString defaultExportName(const QString &suffix) const;

    QString m_port;
    QString m_clientPath;
    Emv::ReadResult m_result;
    bool m_hasResult = false;
    bool m_reading = false;

    QThread *m_thread = nullptr;
    QPointer<EmvWorker> m_worker;

    QPushButton *m_readButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_exportCsvButton = nullptr;
    QPushButton *m_exportJsonButton = nullptr;
    QPushButton *m_clearButton = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_portValue = nullptr;
    QLabel *m_cardValue = nullptr;
    QLabel *m_countValue = nullptr;
    QLabel *m_reasonValue = nullptr;
    QProgressBar *m_progress = nullptr;
    QTableWidget *m_table = nullptr;
    QPlainTextEdit *m_details = nullptr;
};

#endif // EMV_EMVPAGE_H
