#ifndef MIFARE_H
#define MIFARE_H

#include "common/util.h"
#include "ui_mainwindow.h"
#include "ui/mf_attack_hardnesteddialog.h"
#include "ui/mf_uid_parameterdialog.h"
#include "ui/mf_sim_simdialog.h"
#include "module/mifare/mfcancelgate.h"
#include "module/mifare/mfgen3types.h"
#include "module/mifare/mfpm3log.h"
#include <QObject>
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QMessageBox>
#include <QJsonObject>
#include <QAtomicInt>

class ClientSession;

class Mifare : public QObject
{
    Q_OBJECT
public:
    /** Entry points offered by the MIFARE page's main operation area. */
    enum QuickTask
    {
        TASK_QUICK_READ = 0,
        TASK_CRACK_GEN3,
        TASK_IMPORT_PM3,
        TASK_IMPORT_PM3GUI,
    };

    explicit Mifare(Ui::MainWindow *ui, Util *addr, ClientSession *session = nullptr,
                    QWidget *parent = nullptr);
    ~Mifare() override;

    enum KeyType
    {
        KEY_A = 'A',
        KEY_B = 'B',
    };

    enum DataType
    {
        DATA_INVALID,
        DATA_WITHSPACE,
        DATA_NOSPACE,
    };

    struct CardType
    {
        quint8 type;
        quint8 sector_size;
        quint16 block_size;
        quint8 blk[40];
        quint8 blks[40];
        QString typeText;
    };

    enum AccessType
    {
        ACC_NEVER = 0,
        ACC_KEY_A = 1,
        ACC_KEY_B = 2,
        ACC_KEY_AB = 3,
    };

    enum TargetType
    {
        TARGET_MIFARE,
        TARGET_UID,
        TARGET_EMULATOR,
    };

    static const CardType card_mini;
    static const CardType card_1k;
    static const CardType card_2k;
    static const CardType card_4k;

    static const AccessType dataCondition[8][4];
    static const AccessType trailerReadCondition[8][3];
    static const AccessType trailerWriteCondition[8][3];

    QMap<QString, QString> info(bool isRequiringOutput = true);
    void chk();
    void nested(bool isStaticNested = false);
    void darkside();
    void hardnested();
    void sniff();
    void sniff14a();
    void list();
    void readOne(TargetType targetType = TARGET_MIFARE);
    void readSelected(TargetType targetType = TARGET_MIFARE);
    void writeOne(TargetType targetType = TARGET_MIFARE);
    void writeSelected(TargetType targetType = TARGET_MIFARE);
    void dump();
    void restore();

    void data_clearData(bool clearAll = true);
    void data_clearKey(bool clearAll = true);
    static bool data_isKeyValid(const QString& key);
    static Mifare::DataType data_isDataValid(const QString& data);
    void data_syncWithDataWidget(bool syncAll = true, int block = 0);
    void data_syncWithKeyWidget(bool syncAll = true, int sector = 0, KeyType keyType = KEY_A);

    CardType cardType;
    Mifare::CardType getCardType();
    void setCardType(int type);
    void wipeC();
    void setParameterC();

    bool data_loadDataFile(const QString& filename);
    bool data_loadKeyFile(const QString& filename);
    bool data_saveDataFile(const QString& filename, bool isBin);
    bool data_saveKeyFile(const QString& filename, bool isBin);
    void data_key2Data();
    void data_data2Key();

    void data_setData(int block, const QString& data);
    void data_setKey(int sector, KeyType keyType, const QString& key);
    void lockC();
    void wipeE();
    void simulate();
    void loadSniff(const QString& file);
    void saveSniff(const QString& file);
    void data_fillKeys();

    static QList<quint8> data_getACBits(const QString& text);
    static int data_b2s(int block);
    static bool data_isACBitsValid(const QString& text, QList<quint8> *returnHalfBytes = nullptr);
    QString data_getUID();
    quint16 getTrailerBlockId(quint8 sectorId, qint8 cardTypeId = -1); // -1: use current cardtype
    void setConfigMap(const QVariantMap& configMap);
    QString getTraceSavePath();

    // ---- MIFARE one-click read / FM11RF08S (Gen3) integration -------------
    /** "Read Card": identify the card and drive the cheapest working path. */
    void quickReadCard();
    /** "Crack FM11RF08S": probe -> collect -> solve -> verify -> read. */
    void crackGen3Card();
    /** Import a nonce evidence file and verify it on the card (candidate verification). */
    void importNonceEvidence(int format);
    /** Ask the running task to stop; safe to call when idle. */
    void cancelGen3Task();
    /** Busy flag for the main window (single running task at a time). */
    bool gen3Busy() const { return gen3Running; }
    /** Re-read `hf 14a info` and refresh the status area. */
    void refreshCardStatus();
    /**
     * Single read-card-info workflow: one `hf 14a info`, then `hf mf info`
     * only for a MIFARE Classic layout.  Keeps the MIFARE page and uses the
     * existing ClientSession output stream.
     */
    void readCardInfo();
    /** Popup menus shared by the primary operation area. */
    void showImportMenu();
    void showExportMenu();

public slots:
    void onPm3StateChanged(bool connected, const QString &info);

signals:
    /** The page asks the main window to reuse the original file dialogs. */
    void requestLegacyLoad();
    void requestLegacySave();
    void gen3TaskStateChanged(bool running);
    /** A confirmed read-only identity may change the table layout. */
    void cardTypeDetected(int typeId, const QString &uid, bool confirmed);
    /** The Gen3 result changed the card layout; the shell must resize the
     *  data/key tables (row count and model size change together). */
    void cardLayoutChangeRequested(int cardType);

private:
    QWidget* parent;
    Ui::MainWindow *ui;
    Util* util;
    ClientSession* session;

    // ---- one-click task plumbing (worker thread + GUI-thread command pump) --
    void startGen3Task(int task);
    void runGen3Task(int task);
    void finishGen3Task(const mfgen3::CrackResult &result);
    void setTaskUiBusy(bool busy);
    void setInfoBusy(bool busy);
    void appendResultLog(const QString &text);
    void appendPm3Log(const QString &text);
    void appendLogLine(const QString &prefix, const QString &text);
    void showResultSummary(const mfgen3::CrackResult &result);
    void applyCardIdentity(const mfgen3::CardIdentity &identity);
    QString identityConfidenceText(mfgen3::Confidence confidence) const;
    QString identityCapacityText(const mfgen3::CardIdentity &identity) const;
    QString identityAllowedFlowText(const mfgen3::CardIdentity &identity) const;
    QString quickFlowText(mfgen3::QuickFlow flow, const mfgen3::CardIdentity &identity,
                          const QString &detail = QString()) const;
    void applyCrackResult(const mfgen3::CrackResult &result);
    /** Back up the complete in-memory image; false when it could not be saved. */
    bool backupCurrentImage(const QString &uid);
    void updateStatusLabels();
    QString buildImportError(const QString &detail) const;
    QString uniqueOutputPath(const QString &path) const;
    mfgen3::FwFlavor currentFlavor() const;
    QString gen3WorkDir() const;
    QString gen3ExecFromWorker(const QString &cmd, int timeoutMs);
    void gen3StepFromWorker(int step, int total, const QString &text, int percent);
    void gen3LogFromWorker(const QString &text);

    QThread *gen3Thread = nullptr;
    mfgen3::CancelGate gen3Cancel;
    mfgen3::CancelGate infoCancel;
    bool infoRunning = false;
    bool gen3Running = false;
    int gen3Task = -1;
    quint64 gen3TaskSerial = 0;
    mfgen3::Pm3LogFilter pm3LogFilter;
    QString importPath;
    mfgen3::CrackResult lastResult;
    mfgen3::NonceSet lastNonceSet;
    bool lastNonceValid = false;
    mfgen3::CardIdentity lastIdentity;
    QString lastKeySource;
    bool pm3Connected = false;

    QVariantMap configMap;

    QStringList* keyAList;
    QStringList* keyBList;
    QStringList* dataList;
    QString gen3ButtonIdleText;
    bool sniffBufferLoaded = false;
    QRegularExpression* dataPattern;
    QRegularExpression* keyPattern_res;
    QRegularExpression* keyPattern;
    QString bin2text(const QByteArray& buff, int start, int length);

    QString _readblk(int blockId, KeyType keyType, const QString& key, TargetType targetType = TARGET_MIFARE, int waitTime = 300);
    QStringList _readsec(int sectorId, KeyType keyType, const QString& key, TargetType targetType = TARGET_MIFARE, int waitTime = 300);
    bool _writeblk(int blockId, KeyType keyType, const QString& key, const QString& data, TargetType targetType = TARGET_MIFARE, int waitTime = 300);
};

#endif // MIFARE_H
