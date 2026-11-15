#ifndef NTAGTAB_H
#define NTAGTAB_H

#include <QWidget>
#include <QString>
#include <QMap>
#include <QVector>

#include "ntag/ntagmodel.h"
#include "ntag/ntagmemory.h"

class Util;
class ClientSession;
class QTableWidget;
class QTableWidgetItem;
class QLineEdit;
class QSpinBox;
class QLabel;
class QTabWidget;
class QPlainTextEdit;
class QComboBox;
class QPushButton;
class QCheckBox;
class QEvent;

/**
 * NTAG / MIFARE Ultralight page.
 *
 * Commands are limited to the v4.23346 verified set:
 *   hf mfu info
 *   hf mfu rdbl -b <page> [--force]
 *   hf mfu wrbl -b <page> -d <8 hex> [--force]
 *   hf mfu dump -p <first> -q <count> -f <file>
 *
 * `--force` is used only for pages above the client's autodetected maximum
 * (41 on tags whose GET_VERSION is unavailable); the page range has already
 * been validated against the detected NTAG layout.
 */
class NtagTab : public QWidget
{
    Q_OBJECT
public:
    explicit NtagTab(Util *util, ClientSession *session = nullptr,
                     QWidget *parent = nullptr);
    ~NtagTab();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void changeEvent(QEvent *event) override;

private slots:
    void detectCard();
    void readAllPages();
    void readSelectedPages();
    void readOnePage();
    void writeSelectedPages();
    void writeDirtyPages();
    void writeOnePage();
    void verifyOnePage();
    void verifyWritten();
    void eraseTag();
    void quickTypeChanged();
    void updateQuickPreview();
    void writeQuickLaunch();
    void undoEdit();    void redoEdit();
    void discardEdits();
    void exportMemory();
    void importMemory();
    void showSecurityToggled();
    void updateDetailForSelection();
    void onTableItemChanged(QTableWidgetItem *item);
    void cancelActiveJob();
    void onReadCommandFinished(quint64 id, const QString &output, bool success,
                               bool timedOut, const QString &error);
    void onWriteCommandFinished(quint64 id, const QString &output, bool success,
                                bool timedOut, const QString &error);

private:
    struct MemoryEdit
    {
        QMap<int, QByteArray> before;
        QMap<int, QByteArray> after;
    };

    Util *util;
    ClientSession *session;

    QTableWidget *table;
    QLineEdit *keyEdit;
    QLineEdit *dataEdit;
    QSpinBox *pageSpin;
    QLabel *uidLabel;
    QLabel *typeLabel;
    QLabel *pagesLabel;
    QLabel *statusLabel;
    QLabel *modelLabel;
    QLabel *versionLabel;
    QLabel *ccLabel;
    QLabel *capacityLabel;
    QLabel *atqaLabel;
    QLabel *sakLabel;
    QLabel *readCountLabel;
    QLabel *writableCountLabel;
    QLabel *lockStateLabel;
    QTabWidget *viewTabs;
    QPlainTextEdit *ndefView;

    QLabel *detailPageLabel;
    QLabel *detailTypeLabel;
    QLabel *detailOriginalLabel;
    QLabel *detailEditedLabel;
    QLabel *detailWritableLabel;
    QLabel *detailLockLabel;
    QLabel *detailLastWriteLabel;
    QLabel *detailLastReadLabel;
    QLabel *detailNoteLabel;
    QCheckBox *showSecurityCheck;

    QPushButton *undoButton;
    QPushButton *redoButton;
    QPushButton *discardButton;
    QPushButton *exportButton;
    QPushButton *importButton;
    QPushButton *verifyWrittenButton;
    QPushButton *writeDirtyButton;
    QPushButton *eraseButton;
    QPushButton *cancelReadButton;
    QList<QPushButton *> m_busyButtons;

    QComboBox *quickType;
    QLabel *quickPrimaryLabel;
    QLineEdit *quickPrimary;
    QLabel *quickSubjectLabel;
    QLineEdit *quickSubject;
    QLabel *quickBodyLabel;
    QLineEdit *quickBody;
    QLabel *quickCapacityLabel;
    QLabel *quickInfoLabel;
    QPlainTextEdit *quickPreview;
    QPushButton *quickWriteButton;
    QLabel *quickStatusLabel;

    int m_pages = 0;
    int m_configStart = -1;
    bool m_knownLayout = false;
    QString m_type;
    Ntag::Capability m_cap;
    Ntag::Version m_version;
    Ntag::CC m_cc;
    Ntag::MemoryImage m_mem;
    QList<MemoryEdit> m_undoStack;
    QList<MemoryEdit> m_redoStack;
    QMap<int, QByteArray> m_lastWriteTargets;
    QString m_lastWriteError;
    bool m_updatingTable = false;

    Ntag::ReadJobState m_readJob;
    quint64 m_readJobId = 0;
    quint64 readCommandId = 0;
    QList<int> readPages;
    int readPageIndex = 0;
    int readChunkStart = 0;
    QString readPath;

    Ntag::WriteJobState m_writeJob;
    quint64 m_writeJobId = 0;
    quint64 writeCommandId = 0;
    QList<int> writeTargets;
    QMap<int, QByteArray> writeExpected;
    QMap<int, QByteArray> writePlanned;
    int writeIndex = 0;
    bool writeAllowRetry = false;
    bool writeVerifyPageOnly = false;
    bool writeEraseFlow = false;
    QString writeTitle;
    QString writePageHex;
    QStringList m_writeFailedPages;
    Ntag::ErasePlan m_erasePlan;
    Ntag::DumpWorkspace m_dumpWorkspace;

    bool deviceBusy() const { return m_readJob.busy || m_writeJob.busy; }
    void startWriteJob(Ntag::WriteJobMode mode, const QList<int> &targets,
                       const QMap<int, QByteArray> &expected,
                       const QString &title, bool allowRetry,
                       bool verifyPageOnly = false);
    void issueNextWriteCommand();
    void submitWriteVerifyDump();
    void submitWriteReadBack(int page);
    void writeAttemptFailed(const QString &reason);
    void commitWrittenPage(const QString &actual);
    void finishWriteJob(bool cancelled, const QString &message, bool error = false);
    void showWriteReport(const QMap<int, QByteArray> &expected,
                         const QList<QByteArray> &pages,
                         const QVector<Ntag::PageState> &states,
                         const QString &title);
    void showEraseReport(const QList<QByteArray> &after,
                         const QVector<Ntag::PageState> &states, bool readOk);
    void buildUi();
    void setStatus(const QString &text, bool error = false);
    bool checkKey(QString *key) const;
    bool isWritable(int page) const;
    void populateTable();
    void updateRow(int page);
    void updateRowState(int page);
    void setRowData(int page, const QString &data);
    QString rowData(int page) const;
    QString stateText(int page) const;
    QString regionText(int page) const;
    QString pageClassNameFor(int page) const;
    bool readAllFromDump();
    bool readPagesForVerify(QList<QByteArray> *pages, QVector<Ntag::PageState> *states);
    void startReadJob(Ntag::ReadJobMode mode, const QList<int> &pages);
    void issueNextReadCommand();
    void finishReadJob(bool cancelled);
    void setReadBusy(bool busy);
    static bool parseReadPageOutput(const QString &output, QString *data);
    void refreshCcFromTable();
    void verifyWrittenPages(const QMap<int, QByteArray> &expected, const QString &title);
    void showVerifyReport(const Ntag::VerifyReport &report, const QString &title,
                          const QString &context);
    int availableNdefCapacity() const;
    QString pageClassText(Ntag::PageClass pageClass) const;
    QString verdictText(Ntag::PageVerdict verdict) const;
    void fillNdef();
    QString dumpPath() const;
    bool readPageData(int page, QString *data);
    bool writePageData(int page, const QString &data, bool confirm);
    bool writePages(const QList<int> &pages, const QString &title);
    static QString pageForceSuffix(int page);
    bool parseInfo(const QString &output);
    int pagesForType(const QString &type, int *configStart) const;
    void updateCounts();
    void updateLockState();
    void updateDirtyButtons();
    void updateDetail();
    void repopulateRow(int page);
    void snapshotEdit(const QList<int> &pages, const QMap<int, QByteArray> &before);
    QMap<int, QByteArray> editedSnapshot(const QList<int> &pages) const;
    void applyEditedSnapshot(const QMap<int, QByteArray> &snapshot);
    bool confirmDiscardEdits(const QString &reason);
    QByteArray dumpVersion() const;
    bool loadDumpIntoMemory(const QByteArray &file);
    void copySelection();
    void pasteHex();
    static QString normalizePageData(const QString &text);
    static QString asciiFor(const QString &hex);
};

#endif // NTAGTAB_H
