#ifndef AMIIBOPAGE_H
#define AMIIBOPAGE_H

#include <QWidget>
#include <QByteArray>
#include <QList>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QLineEdit;
class QPushButton;
class QComboBox;
class QNetworkAccessManager;
class QNetworkReply;
class Util;
class ClientSession;

/** One Amiibo file reference plus display metadata. */
struct AmiiboEntry
{
    QString name;
    QString source;      // "file", "online"
    QString path;        // original local file or GUI-created raw copy
    QString series;
    QString imageUrl;
    QString binUrl;
    QByteArray raw;      // exact bytes, for preview/hash only
    QString hash;
};

/**
 * Amiibo library and raw-BIN simulation page.
 *
 * The library stores file references and display metadata only.  The
 * simulation path still re-reads the selected file and passes the exact bytes
 * to `hf mfu eload`; no tag read/write/verify operations exist here.
 */
class AmiiboPage : public QWidget
{
    Q_OBJECT
public:
    explicit AmiiboPage(Util *util, ClientSession *session, QWidget *parent = nullptr);
    ~AmiiboPage() override;

    void setDeviceInfo(const QString &port, const QString &clientPath,
                       const QString &workingDir);
    void setClientConnected(bool connected);
    bool isEmulating() const;

signals:
    /** Stop the running simulation (the owner restarts its client). */
    void stopRequested();

private slots:
    void importFile();
    void importFolder();
    void removeEntry();
    void onEntrySelected();
    void startEmulation();
    void stopEmulation();
    void searchCatalog();
    void onCatalogReply(QNetworkReply *reply);
    void onCatalogSelectionChanged();
    void downloadCatalogItem();

private:
    void buildUi();
    void refreshList();
    void showEntry(int index);
    void setStatus(const QString &text, bool error = false);
    void setBusy(bool busy);
    void updateButtons();
    void addEntry(const AmiiboEntry &entry);
    int importPaths(const QStringList &paths, bool showSummary);
    void selectEntry(int index);
    void loadImageFor(const AmiiboEntry &entry);
    QString standardLoadProblem(const QString &output) const;
    bool loadStandard(const QString &clientPath, QString *log, QString *error);
    bool loadDirect(const QByteArray &bytes, QString *log, QString *error);
    static bool plausibleRaw540(const QByteArray &bytes, QString *reason);
    QString clientPathFor(const QByteArray &bytes, const QString &hash,
                          QString *temporaryFile);
    QString tempPath(const QString &name) const;
    static QString formatOf(const QByteArray &data);
    static QString localSidecarImage(const QString &binPath);

    Util *util;
    ClientSession *session;
    QNetworkAccessManager *net;

    QListWidget *library;
    QLabel *nameLabel;
    QLabel *sourceLabel;
    QLabel *sizeLabel;
    QLabel *hashLabel;
    QLabel *formatLabel;
    QLabel *imageLabel;
    QLabel *stateLabel;
    QPlainTextEdit *outputView;
    QComboBox *simModeCombo;
    QPushButton *importButton;
    QPushButton *importFolderButton;
    QPushButton *removeButton;
    QPushButton *emulateButton;
    QPushButton *stopButton;

    QLineEdit *catalogUrlEdit;
    QLineEdit *catalogSearchEdit;
    QPushButton *catalogSearchButton;
    QPushButton *catalogDownloadButton;
    QListWidget *catalogList;
    QLabel *catalogInfo;

    QList<AmiiboEntry> entries;
    QList<AmiiboEntry> catalogEntries;
    QString selectedCatalogName;

    QString filePath;
    QByteArray rawBytes;
    QString rawHash;
    QString workingDir;
    bool loaded;
    bool connected;
    bool busy;
    bool emulating;
};

#endif // AMIIBOPAGE_H
