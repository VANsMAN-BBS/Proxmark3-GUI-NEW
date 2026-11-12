#ifndef EMRTD_H
#define EMRTD_H

#include <QWidget>
#include <QByteArray>
#include <QJsonObject>
#include <QPixmap>
#include <QProcess>
#include <QThread>
#include <QImage>

class QPlainTextEdit;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTimer;

/**
 * Decodes the portrait in a worker thread: large DG2 images must never block
 * the GUI thread.  QPixmap is created later on the GUI thread from the QImage.
 */
class ImageDecodeTask : public QThread
{
    Q_OBJECT
public:
    ImageDecodeTask(const QByteArray &data, quint64 generation, QObject *parent = nullptr);
signals:
    void decoded(quint64 generation, const QImage &image, bool ok);
protected:
    void run() override;
private:
    QByteArray m_data;
    quint64 m_generation;
};

class QPlainTextEdit;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QProcess;
class QTimer;

/**
 * eMRTD (passport / HK-Macau permit) reader page.
 *
 * The page is a thin front-end: all BAC, secure messaging, TLV and image
 * decoding happens in the existing Python project (emrtd_pm3), which runs as a
 * separate process and talks to proxmark3.exe itself.  The GUI therefore hands
 * the serial device over (stops its own ClientSession) before a read starts.
 *
 * Nothing is written to disk unless the user explicitly saves the portrait,
 * and no document data is ever logged or uploaded.
 */
class EMRTDPage : public QWidget
{
    Q_OBJECT
public:
    explicit EMRTDPage(QWidget *parent = nullptr);
    ~EMRTDPage() override;

    void setDeviceInfo(const QString &port, const QString &clientPath);
    void setPythonExecutable(const QString &path);
    bool isReading() const;

signals:
    /** Emitted right before the reader process starts; the owner must stop its
     *  own proxmark3.exe so only one process owns the serial port. */
    void aboutToRead();
    /** Emitted when the reader finished (success, failure or cancel). */
    void readFinished();

private slots:
    void validateMrz();
    void clearMrz();
    void startRead();
    void cancelRead();
    void onProcessStdout();
    void onProcessStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void onCancelTimeout();
    void onStallTimeout();
    void onImageDecoded(quint64 generation, const QImage &image, bool ok);
    void copyAllFields();
    void copyMrz();
    void copySelection();
    void savePortrait();
    void zoomIn();
    void zoomOut();
    void fitPortrait();

private:
    void buildUi();
    void setBusy(bool busy, const QString &stage = QString());
    void resetReport();
    void handleEvent(const QJsonObject &event);
    void applyResult(const QJsonObject &data);
    void showError(const QString &message, const QString &field = QString());
    void stopReaderProcess();
    QString pythonExecutable() const;
    QString readerDirectory() const;
    QString stageLabel(const QString &stage) const;
    void updatePortrait();
    void clearSensitiveData();

    QPlainTextEdit *mrzEdit;
    QLabel *mrzStatus;
    QPushButton *validateButton;
    QPushButton *clearButton;
    QPushButton *startButton;
    QPushButton *cancelButton;
    QProgressBar *progress;
    QLabel *stageLabelWidget;
    QLabel *detailLabel;
    QLabel *deviceLabel;
    QTabWidget *resultTabs;
    QTableWidget *infoTable;
    QLabel *portraitLabel;
    QTabWidget *otherTabs;
    QPlainTextEdit *hexView;

    QProcess *process;
    QTimer *cancelTimer;
    QTimer *stallTimer;
    ImageDecodeTask *decoder;
    quint64 imageGeneration;
    bool decoding;
    QByteArray stdoutBuffer;
    QByteArray stderrBuffer;

    QString port;
    QString clientPath;
    QString pythonPathOverride;
    QString pendingError;
    QString pendingErrorField;
    bool reading;
    bool gotResult;

    QPixmap portrait;
    QByteArray portraitBytes;
    QString portraitMime;
    QString portraitFormat;
    double zoom;
};

#endif // EMRTD_H
