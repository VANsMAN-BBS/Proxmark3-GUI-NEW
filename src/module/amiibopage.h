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
class QNetworkAccessManager;
class QNetworkReply;
class QPixmap;
class Util;
class ClientSession;

/** A loadable Amiibo / NTAG215 image. */
struct AmiiboEntry
{
    QString name;
    QString source;      // "sample", "file", "online", "tag"
    QString path;
    QString series;
    QString imageUrl;
    QString binUrl;
    QByteArray data;
};

/**
 * Amiibo sub-page of the NTAG group.
 *
 * Amiibo tags are NFC Forum Type 2 / NTAG215, so they belong with the NTAG
 * functionality (they were grouped under JTAG in v0.4.0 by mistake).
 *
 * Writing requires an explicit confirmation and is always followed by a
 * read-back verification.  No irreversible commands (lock/OTP/PWD) are issued.
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
    void addSyntheticSample();
    void importFile();
    void saveAsFile();
    void removeEntry();
    void onEntrySelected();
    void detectTag();
    void readTag();
    void writeTag();
    void verifyTag();
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
    void setBusy(bool busy, const QString &text = QString());
    void setStatus(const QString &text);
    QString tempPath(const QString &name) const;
    bool readBackInto(QByteArray &out, const QString &why);
    void addEntry(const AmiiboEntry &entry);
    static QString uidOf(const QByteArray &data);
    static QString formatOf(const QByteArray &data);
    static bool isNtag215(const QByteArray &data);

    Util *util;
    ClientSession *session;
    QListWidget *library;
    QLabel *nameLabel;
    QLabel *sourceLabel;
    QLabel *sizeLabel;
    QLabel *formatLabel;
    QLabel *imageLabel;
    QPlainTextEdit *hexView;
    QPushButton *sampleButton;
    QPushButton *importButton;
    QPushButton *saveButton;
    QPushButton *removeButton;
    QPushButton *detectButton;
    QPushButton *readButton;
    QPushButton *writeButton;
    QPushButton *verifyButton;
    QPushButton *emulateButton;
    QPushButton *stopButton;
    QLineEdit *catalogUrlEdit;
    QLineEdit *catalogSearchEdit;
    QPushButton *catalogSearchButton;
    QPushButton *catalogDownloadButton;
    QListWidget *catalogList;
    QLabel *catalogInfo;
    QLabel *statusLabel;

    QNetworkAccessManager *net;
    QList<AmiiboEntry> entries;
    QList<AmiiboEntry> catalogEntries;
    QByteArray selectedCatalogBin;
    QString selectedCatalogName;

    QString port;
    QString clientPath;
    QString workingDir;
    bool connected;
    bool busy;
    bool emulating;
};

#endif // AMIIBOPAGE_H

