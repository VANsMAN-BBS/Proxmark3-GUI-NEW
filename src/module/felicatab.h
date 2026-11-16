#ifndef FELICATAB_H
#define FELICATAB_H

#include <QWidget>

#include "felica/transit_types.h"

class Util;
class ClientSession;
class FelicaReader;

class QPushButton;
class QLabel;
class QProgressBar;
class QTabWidget;
class QTableWidget;
class QPlainTextEdit;
class QCheckBox;

/**
 * FeliCa transit card page (read-only).
 *
 * UI layer for CjrcProfile: card detection, balance, transaction history,
 * gate history, service status, raw blocks and JSON/CSV export.
 */
class FelicaTab : public QWidget
{
    Q_OBJECT
public:
    explicit FelicaTab(Util *util, ClientSession *session, QWidget *parent = nullptr);
    ~FelicaTab() override;

public slots:
    void setClientConnected(bool connected);

private slots:
    void readCard();
    void cancelRead();
    void clearView();
    void exportJson();
    void exportCsv();
    void onProgress(const QString &message, int percent);
    void onCardDetected(const Transit::CardInfo &card);
    void onFinished(const Transit::ReadResult &result);
    void onFailed(const QString &message);
    void onHistorySelectionChanged();
    void onGateSelectionChanged();

private:
    void buildUi();
    void setBusy(bool busy);
    void resetView();
    void populate(const Transit::ReadResult &result);
    void populateHistory();
    void populateGates();
    void populateServices();
    void populateRaw();
    void updateCardPanel();
    void updateStatusLine();
    QString formatAmount(qint64 value, bool known) const;
    QString stationText(const Transit::StationInfo &station) const;
    QString candidateText(const Transit::StationInfo &station) const;
    QString tableCell(const Transit::StationInfo &station) const;
    QString timestampOf(const Transit::TransactionRecord &record) const;

    Util *m_util = nullptr;
    ClientSession *m_session = nullptr;
    FelicaReader *m_reader = nullptr;

    QPushButton *m_readButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_exportJsonButton = nullptr;
    QPushButton *m_exportCsvButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QCheckBox *m_includeRawCheck = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_balanceLabel = nullptr;
    QLabel *m_balanceCaption = nullptr;
    QLabel *m_cardTypeValue = nullptr;
    QLabel *m_cardIdValue = nullptr;
    QLabel *m_pmmValue = nullptr;
    QLabel *m_icValue = nullptr;
    QLabel *m_systemsValue = nullptr;
    QLabel *m_detectedValue = nullptr;
    QLabel *m_qualityLabel = nullptr;
    QProgressBar *m_progress = nullptr;

    QTabWidget *m_tabs = nullptr;
    QTableWidget *m_historyTable = nullptr;
    QTableWidget *m_gateTable = nullptr;
    QTableWidget *m_serviceTable = nullptr;
    QTableWidget *m_rawTable = nullptr;
    QPlainTextEdit *m_historyDetail = nullptr;
    QPlainTextEdit *m_gateDetail = nullptr;

    Transit::ReadResult m_result;
    bool m_hasResult = false;
    bool m_connected = false;
};

#endif // FELICATAB_H
