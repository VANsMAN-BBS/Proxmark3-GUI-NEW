#ifndef ISO14443A_PAGE_H
#define ISO14443A_PAGE_H

#include <QJsonObject>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class Iso14443aService;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTableWidget;
class QTextBrowser;
class QTabWidget;

/**
 * "More Features > ISO14443A Parser" page.
 *
 * The page is deliberately independent from the Proxmark3 process: it reads
 * PM3 .trace files or text captures and parses them in the persistent Python
 * sidecar.  It never opens a serial port and never sends card commands.
 *
 * Terminal linkage is read-only: "Import latest trace" scans the terminal
 * text and the trace-save directory for the newest .trace/.trc file.  If no
 * file exists it reports that automatic capture is not supported instead of
 * starting a sniff session itself.
 */
class Iso14443aPage : public QWidget
{
    Q_OBJECT
public:
    explicit Iso14443aPage(QWidget *parent = nullptr);
    ~Iso14443aPage() override;

    static QString pageTitle();

    void setClientWorkingDirectory(const QString &directory);
    void setTraceDirectoryProvider(const std::function<QStringList()> &provider);
    void setTerminalText(const QString &text);
    bool isBusy() const;
    void shutdown();

signals:
    /** MainWindow supplies the current raw terminal text synchronously. */
    void terminalTextRequested();

private slots:
    void openTrace();
    void openCapture();
    void importLatestTrace();
    void parseCurrent();
    void clearView();
    void exportJson();

    void onRequestStarted(quint64 requestId, const QString &action);
    void onRequestFinished(quint64 requestId, const QString &action,
                           const QJsonObject &data);
    void onRequestFailed(quint64 requestId, const QString &action,
                         const QString &code, const QString &message);
    void onReady();
    void onServiceLog(const QString &line);
    void onSelectionChanged();

private:
    enum SourceKind
    {
        NoSource,
        TraceSource,
        CaptureSource
    };

    void buildUi();
    void setBusy(bool busy);
    void updateButtons();
    void resetResult();
    void showData(const QJsonObject &data);
    void populateTable();
    void updateOverview();
    void updateDetails(int row);
    void startParse();
    QString initialDirectory() const;
    QString findLatestTrace() const;
    QStringList traceSearchDirectories() const;
    QString defaultExportName() const;
    QString statusText(bool checked, bool ok) const;
    QString typeLabel(const QString &type) const;
    QString phaseLabel(const QString &phase) const;
    QString directionLabel(const QString &direction) const;
    static QString elideText(const QString &text, int maxChars);
    static QString asciiOf(const QString &hex);
    static QString htmlEscape(const QString &text);

    Iso14443aService *m_service = nullptr;

    QPushButton *m_openTraceButton = nullptr;
    QPushButton *m_openCaptureButton = nullptr;
    QPushButton *m_importTraceButton = nullptr;
    QPushButton *m_parseButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QPushButton *m_exportButton = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_uidValue = nullptr;
    QLabel *m_atqaValue = nullptr;
    QLabel *m_sakValue = nullptr;
    QLabel *m_uidLenValue = nullptr;
    QLabel *m_cardTypeValue = nullptr;
    QLabel *m_atsValue = nullptr;
    QLabel *m_stateValue = nullptr;
    QLabel *m_crcValue = nullptr;
    QLabel *m_parityValue = nullptr;

    QTableWidget *m_table = nullptr;
    QTextBrowser *m_details = nullptr;
    QPlainTextEdit *m_logEdit = nullptr;

    QJsonObject m_lastData;
    QVector<QJsonObject> m_frames;

    SourceKind m_sourceKind = NoSource;
    QString m_sourcePath;
    QString m_terminalText;
    QString m_clientWorkingDir;
    std::function<QStringList()> m_traceProvider;

    quint64 m_activeRequest = 0;
    QString m_activeAction;
    bool m_busy = false;
};

/** Add the page to a "More Features" container (used by MainWindow and tests). */
int registerIso14443aPage(QTabWidget *tabs, Iso14443aPage *page);

#endif // ISO14443A_PAGE_H
