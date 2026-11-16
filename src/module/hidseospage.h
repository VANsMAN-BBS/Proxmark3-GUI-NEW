#ifndef HIDSEOSPAGE_H
#define HIDSEOSPAGE_H

#include <QWidget>

#include "hidseos/seos_types.h"

class Util;
class ClientSession;
namespace HidSeos
{
class SeosReader;
}

class QLabel;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QPlainTextEdit;

/**
 * Read-only HID SEOS page for ISO14443-A / ISO-DEP T-money and Climate Cards.
 *
 * The page issues only read APDUs through the shared ClientSession queue. There
 * is no MIFARE Classic path and no write, authentication, or cloning command.
 */
class HidSeosPage : public QWidget
{
    Q_OBJECT
public:
    explicit HidSeosPage(Util *util, ClientSession *session, QWidget *parent = nullptr);
    ~HidSeosPage() override;

public slots:
    void setClientConnected(bool connected);

private slots:
    void readCard();
    void cancelRead();
    void clearView();
    void exportJson();
    void exportCsv();
    void onProgress(const QString &message, int percent);
    void onFinished(const HidSeos::ReadResult &result);
    void onFailed(const QString &message);
    void onTransactionSelectionChanged();
    void onTripSelectionChanged();
    void onFinancialSelectionChanged();
    void showTransactionDetails();
    void showTripDetails();
    void showFinancialDetails();

private:
    void buildUi();
    void setBusy(bool busy);
    void resetView();
    void populate(const HidSeos::ReadResult &result);
    void populateCardPanel();
    void populateTransactions();
    void populateTrips();
    void populateFinancial();
    void populateRaw();
    void updateStatusLine();

    QString formatKrw(qint64 value) const;
    QString formatCount(qint64 value) const;
    QString cardTypeText() const;
    QString cardKindText() const;
    QString issuerText() const;
    QString userTypeText() const;
    QString eventText(int code) const;
    QString transportText(int code) const;
    QString markerText(int code) const;
    QString financialTypeText(int code) const;
    QString balanceValueText() const;
    QString balanceNoteText() const;
    QString passValidityText() const;
    QString calibrationText() const;
    QString tripDetails(const HidSeos::TripRecord &trip) const;
    QString tripDetails(const HidSeos::GroupedTrip &trip) const;
    QString financialDetails(const HidSeos::FinancialRecord &record) const;
    void showDetailsDialog(const QString &title, const QString &text);

    Util *m_util = nullptr;
    ClientSession *m_session = nullptr;
    HidSeos::SeosReader *m_reader = nullptr;

    QPushButton *m_readButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_exportJsonButton = nullptr;
    QPushButton *m_exportCsvButton = nullptr;
    QPushButton *m_clearButton = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_qualityLabel = nullptr;
    QProgressBar *m_progress = nullptr;

    QLabel *m_balanceCaption = nullptr;
    QLabel *m_balanceLabel = nullptr;
    QLabel *m_balanceNote = nullptr;
    QLabel *m_cardTypeValue = nullptr;
    QLabel *m_cardKindValue = nullptr;
    QLabel *m_uidValue = nullptr;
    QLabel *m_csnValue = nullptr;
    QLabel *m_issuerValue = nullptr;
    QLabel *m_userValue = nullptr;
    QLabel *m_issueValue = nullptr;
    QLabel *m_expiryValue = nullptr;
    QLabel *m_protocolValue = nullptr;

    QLabel *m_passValidValue = nullptr;
    QLabel *m_passDaysValue = nullptr;
    QLabel *m_passProductValue = nullptr;
    QLabel *m_passStatusValue = nullptr;
    QLabel *m_calibrationValue = nullptr;

    QTabWidget *m_tabs = nullptr;
    QTableWidget *m_transactionTable = nullptr;
    QTableWidget *m_tripTable = nullptr;
    QTableWidget *m_financialTable = nullptr;
    QTableWidget *m_rawTable = nullptr;
    QPlainTextEdit *m_transactionDetail = nullptr;
    QPlainTextEdit *m_tripDetail = nullptr;
    QPlainTextEdit *m_financialDetail = nullptr;

    HidSeos::ReadResult m_result;
    bool m_hasResult = false;
    bool m_connected = false;
};

#endif // HIDSEOSPAGE_H
