#include "hidseospage.h"

#include "common/util.h"
#include "core/clientsession.h"
#include "hidseos/seos_codes.h"
#include "hidseos/seos_exporter.h"
#include "hidseos/seos_reader.h"

#include <QColor>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

namespace {

QString hexBytes(const QByteArray &data)
{
    return QString::fromLatin1(data.toHex(' ').toUpper());
}

QString hexByte(int value)
{
    return QStringLiteral("0x%1")
        .arg(value, 2, 16, QLatin1Char('0'))
        .toUpper()
        .replace(QStringLiteral("0X"), QStringLiteral("0x"));
}

QTableWidgetItem *textItem(const QString &text)
{
    QTableWidgetItem *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

HidSeosPage::HidSeosPage(Util *util, ClientSession *session, QWidget *parent)
    : QWidget(parent)
    , m_util(util)
    , m_session(session)
{
    m_reader = new HidSeos::SeosReader(m_session, this);
    connect(m_reader, &HidSeos::SeosReader::progress, this, &HidSeosPage::onProgress);
    connect(m_reader, &HidSeos::SeosReader::finished, this, &HidSeosPage::onFinished);
    connect(m_reader, &HidSeos::SeosReader::failed, this, &HidSeosPage::onFailed);

    buildUi();
    resetView();
    if (m_session)
        setClientConnected(m_session->isConnected());
}

HidSeosPage::~HidSeosPage() = default;

void HidSeosPage::buildUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);

    QHBoxLayout *actions = new QHBoxLayout();
    m_readButton = new QPushButton(tr("Read Card"), this);
    m_readButton->setObjectName(QStringLiteral("HidSeos_readButton"));
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setObjectName(QStringLiteral("HidSeos_cancelButton"));
    m_exportJsonButton = new QPushButton(tr("Export JSON"), this);
    m_exportJsonButton->setObjectName(QStringLiteral("HidSeos_exportJsonButton"));
    m_exportCsvButton = new QPushButton(tr("Export CSV"), this);
    m_exportCsvButton->setObjectName(QStringLiteral("HidSeos_exportCsvButton"));
    m_clearButton = new QPushButton(tr("Clear View"), this);
    m_clearButton->setObjectName(QStringLiteral("HidSeos_clearButton"));

    actions->addWidget(m_readButton);
    actions->addWidget(m_cancelButton);
    actions->addSpacing(12);
    actions->addWidget(m_exportJsonButton);
    actions->addWidget(m_exportCsvButton);
    actions->addWidget(m_clearButton);
    actions->addStretch();
    root->addLayout(actions);

    QFrame *cardFrame = new QFrame(this);
    cardFrame->setFrameShape(QFrame::StyledPanel);
    QHBoxLayout *cardLayout = new QHBoxLayout(cardFrame);

    QVBoxLayout *balanceLayout = new QVBoxLayout();
    m_balanceCaption = new QLabel(tr("Balance"), cardFrame);
    m_balanceCaption->setAlignment(Qt::AlignCenter);
    m_balanceLabel = new QLabel(QStringLiteral("—"), cardFrame);
    QFont balanceFont = m_balanceLabel->font();
    balanceFont.setPointSize(balanceFont.pointSize() + 14);
    balanceFont.setBold(true);
    m_balanceLabel->setFont(balanceFont);
    m_balanceLabel->setAlignment(Qt::AlignCenter);
    m_balanceNote = new QLabel(cardFrame);
    m_balanceNote->setAlignment(Qt::AlignCenter);
    m_balanceNote->setWordWrap(true);
    balanceLayout->addWidget(m_balanceCaption);
    balanceLayout->addWidget(m_balanceLabel);
    balanceLayout->addWidget(m_balanceNote);
    balanceLayout->addStretch();
    cardLayout->addLayout(balanceLayout, 1);

    QFormLayout *infoLayout = new QFormLayout();
    m_cardTypeValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_cardKindValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_uidValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_csnValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_issuerValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_userValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_issueValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_expiryValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_protocolValue = new QLabel(QStringLiteral("—"), cardFrame);
    m_csnValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_uidValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLayout->addRow(tr("Card type:"), m_cardTypeValue);
    infoLayout->addRow(tr("Card kind:"), m_cardKindValue);
    infoLayout->addRow(tr("UID:"), m_uidValue);
    infoLayout->addRow(tr("CSN:"), m_csnValue);
    infoLayout->addRow(tr("Issuer:"), m_issuerValue);
    infoLayout->addRow(tr("User type:"), m_userValue);
    infoLayout->addRow(tr("Issued:"), m_issueValue);
    infoLayout->addRow(tr("Expires:"), m_expiryValue);
    infoLayout->addRow(tr("Protocol:"), m_protocolValue);
    cardLayout->addLayout(infoLayout, 2);
    root->addWidget(cardFrame);

    QGroupBox *passGroup = new QGroupBox(tr("Period pass"), this);
    QHBoxLayout *passLayout = new QHBoxLayout(passGroup);
    QFormLayout *passForm = new QFormLayout();
    m_passValidValue = new QLabel(QStringLiteral("—"), passGroup);
    m_passDaysValue = new QLabel(QStringLiteral("—"), passGroup);
    m_passProductValue = new QLabel(QStringLiteral("—"), passGroup);
    m_passStatusValue = new QLabel(QStringLiteral("—"), passGroup);
    passForm->addRow(tr("Valid:"), m_passValidValue);
    passForm->addRow(tr("Days:"), m_passDaysValue);
    passForm->addRow(tr("Product code:"), m_passProductValue);
    passForm->addRow(tr("Status:"), m_passStatusValue);
    passLayout->addLayout(passForm, 1);

    QFormLayout *calibrationForm = new QFormLayout();
    m_calibrationValue = new QLabel(QStringLiteral("—"), passGroup);
    m_calibrationValue->setWordWrap(true);
    calibrationForm->addRow(tr("Date calibration:"), m_calibrationValue);
    passLayout->addLayout(calibrationForm, 2);
    root->addWidget(passGroup);

    QHBoxLayout *progressLayout = new QHBoxLayout();
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_statusLabel = new QLabel(tr("Connect the Proxmark3 client, then press Read Card."), this);
    m_qualityLabel = new QLabel(this);
    progressLayout->addWidget(m_statusLabel, 1);
    progressLayout->addWidget(m_qualityLabel);
    progressLayout->addWidget(m_progress, 1);
    root->addLayout(progressLayout);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("HidSeos_tabs"));

    // Raw trip taps (SFI 3)
    QWidget *transactionPage = new QWidget(m_tabs);
    QVBoxLayout *transactionLayout = new QVBoxLayout(transactionPage);
    m_transactionTable = new QTableWidget(transactionPage);
    m_transactionTable->setObjectName(QStringLiteral("HidSeos_transactionTable"));
    m_transactionTable->setColumnCount(9);
    m_transactionTable->setHorizontalHeaderLabels(QStringList()
        << tr("#") << tr("Date") << tr("Time") << tr("Event") << tr("Transport")
        << tr("Counter") << tr("Journey") << tr("Recorded fare (KRW)") << tr("Distance (m)"));
    m_transactionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_transactionTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_transactionTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_transactionTable->setAlternatingRowColors(true);
    m_transactionTable->horizontalHeader()->setStretchLastSection(false);
    m_transactionTable->verticalHeader()->setVisible(false);
    m_transactionDetail = new QPlainTextEdit(transactionPage);
    m_transactionDetail->setReadOnly(true);
    m_transactionDetail->setPlaceholderText(tr("Select a tap to inspect its fields; double-click for details."));
    transactionLayout->addWidget(m_transactionTable, 3);
    transactionLayout->addWidget(m_transactionDetail, 1);
    m_tabs->addTab(transactionPage, tr("Transactions"));

    // Grouped journeys
    QWidget *tripPage = new QWidget(m_tabs);
    QVBoxLayout *tripLayout = new QVBoxLayout(tripPage);
    m_tripTable = new QTableWidget(tripPage);
    m_tripTable->setObjectName(QStringLiteral("HidSeos_tripTable"));
    m_tripTable->setColumnCount(7);
    m_tripTable->setHorizontalHeaderLabels(QStringList()
        << tr("#") << tr("Date") << tr("Start") << tr("End") << tr("Legs")
        << tr("Taps") << tr("Recorded fare (KRW)"));
    m_tripTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tripTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tripTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tripTable->setAlternatingRowColors(true);
    m_tripTable->horizontalHeader()->setStretchLastSection(false);
    m_tripTable->verticalHeader()->setVisible(false);
    m_tripDetail = new QPlainTextEdit(tripPage);
    m_tripDetail->setReadOnly(true);
    m_tripDetail->setPlaceholderText(tr("Select a journey to inspect the merged transfer legs."));
    tripLayout->addWidget(m_tripTable, 3);
    tripLayout->addWidget(m_tripDetail, 1);
    m_tabs->addTab(tripPage, tr("Grouped trips"));

    // SFI 4/5 financial records (amount / top-up) and SFI 11 read events
    QWidget *financialPage = new QWidget(m_tabs);
    QVBoxLayout *financialLayout = new QVBoxLayout(financialPage);
    m_financialTable = new QTableWidget(financialPage);
    m_financialTable->setObjectName(QStringLiteral("HidSeos_financialTable"));
    m_financialTable->setColumnCount(8);
    m_financialTable->setHorizontalHeaderLabels(QStringList()
        << tr("#") << tr("SFI") << tr("Type") << tr("Balance (KRW)") << tr("Sequence")
        << tr("Fare (KRW)") << tr("Marker") << tr("Terminal"));
    m_financialTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_financialTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_financialTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_financialTable->setAlternatingRowColors(true);
    m_financialTable->horizontalHeader()->setStretchLastSection(false);
    m_financialTable->verticalHeader()->setVisible(false);
    m_financialDetail = new QPlainTextEdit(financialPage);
    m_financialDetail->setReadOnly(true);
    m_financialDetail->setPlaceholderText(tr("Select a record to inspect its fields; double-click for details."));
    financialLayout->addWidget(m_financialTable, 3);
    financialLayout->addWidget(m_financialDetail, 1);
    m_tabs->addTab(financialPage, tr("Financial records"));

    // Raw bytes
    QWidget *rawPage = new QWidget(m_tabs);
    QVBoxLayout *rawLayout = new QVBoxLayout(rawPage);
    m_rawTable = new QTableWidget(rawPage);
    m_rawTable->setObjectName(QStringLiteral("HidSeos_rawTable"));
    m_rawTable->setColumnCount(5);
    m_rawTable->setHorizontalHeaderLabels(QStringList()
        << tr("SFI") << tr("File") << tr("Record") << tr("Length") << tr("Data (hex)"));
    m_rawTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rawTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rawTable->setAlternatingRowColors(true);
    m_rawTable->horizontalHeader()->setStretchLastSection(true);
    m_rawTable->verticalHeader()->setVisible(false);
    rawLayout->addWidget(m_rawTable);
    m_tabs->addTab(rawPage, tr("Raw data"));

    root->addWidget(m_tabs, 1);

    connect(m_readButton, &QPushButton::clicked, this, &HidSeosPage::readCard);
    connect(m_cancelButton, &QPushButton::clicked, this, &HidSeosPage::cancelRead);
    connect(m_exportJsonButton, &QPushButton::clicked, this, &HidSeosPage::exportJson);
    connect(m_exportCsvButton, &QPushButton::clicked, this, &HidSeosPage::exportCsv);
    connect(m_clearButton, &QPushButton::clicked, this, &HidSeosPage::clearView);
    connect(m_transactionTable, &QTableWidget::itemSelectionChanged,
            this, &HidSeosPage::onTransactionSelectionChanged);
    connect(m_tripTable, &QTableWidget::itemSelectionChanged,
            this, &HidSeosPage::onTripSelectionChanged);
    connect(m_financialTable, &QTableWidget::itemSelectionChanged,
            this, &HidSeosPage::onFinancialSelectionChanged);
    connect(m_transactionTable, &QTableWidget::itemDoubleClicked,
            this, [this](QTableWidgetItem *) { showTransactionDetails(); });
    connect(m_tripTable, &QTableWidget::itemDoubleClicked,
            this, [this](QTableWidgetItem *) { showTripDetails(); });
    connect(m_financialTable, &QTableWidget::itemDoubleClicked,
            this, [this](QTableWidgetItem *) { showFinancialDetails(); });
}

void HidSeosPage::setClientConnected(bool connected)
{
    m_connected = connected;
    m_readButton->setEnabled(connected && !m_reader->isRunning());
    if (!connected && !m_reader->isRunning())
        m_statusLabel->setText(tr("Proxmark3 client is not connected."));
}

void HidSeosPage::setBusy(bool busy)
{
    m_readButton->setEnabled(m_connected && !busy);
    m_cancelButton->setEnabled(busy);
    m_exportJsonButton->setEnabled(!busy && m_hasResult);
    m_exportCsvButton->setEnabled(!busy && m_hasResult);
    m_clearButton->setEnabled(!busy && m_hasResult);
}

void HidSeosPage::resetView()
{
    m_result = HidSeos::ReadResult();
    m_hasResult = false;

    m_balanceLabel->setText(QStringLiteral("—"));
    m_balanceNote->clear();
    m_cardTypeValue->setText(QStringLiteral("—"));
    m_cardKindValue->setText(QStringLiteral("—"));
    m_uidValue->setText(QStringLiteral("—"));
    m_csnValue->setText(QStringLiteral("—"));
    m_issuerValue->setText(QStringLiteral("—"));
    m_userValue->setText(QStringLiteral("—"));
    m_issueValue->setText(QStringLiteral("—"));
    m_expiryValue->setText(QStringLiteral("—"));
    m_protocolValue->setText(QStringLiteral("—"));
    m_passValidValue->setText(QStringLiteral("—"));
    m_passDaysValue->setText(QStringLiteral("—"));
    m_passProductValue->setText(QStringLiteral("—"));
    m_passStatusValue->setText(QStringLiteral("—"));
    m_calibrationValue->setText(QStringLiteral("—"));
    m_calibrationValue->setToolTip(QString());
    m_qualityLabel->clear();
    m_qualityLabel->setToolTip(QString());
    m_statusLabel->setToolTip(QString());
    m_progress->setValue(0);

    m_transactionTable->setRowCount(0);
    m_tripTable->setRowCount(0);
    m_financialTable->setRowCount(0);
    m_rawTable->setRowCount(0);
    m_transactionDetail->clear();
    m_tripDetail->clear();
    m_financialDetail->clear();

    setBusy(false);
}

void HidSeosPage::clearView()
{
    resetView();
    m_statusLabel->setText(tr("Cleared. Press Read Card to read the card again."));
}

void HidSeosPage::readCard()
{
    if (!m_session || !m_session->isConnected()) {
        m_statusLabel->setText(tr("Proxmark3 client is not connected."));
        return;
    }

    resetView();
    m_statusLabel->setText(tr("Starting read..."));
    m_progress->setValue(0);
    setBusy(true);
    m_reader->start();
    setBusy(m_reader->isRunning());
}

void HidSeosPage::cancelRead()
{
    if (!m_reader->isRunning())
        return;
    m_statusLabel->setText(tr("Cancelling..."));
    m_reader->cancel();
}

void HidSeosPage::onProgress(const QString &message, int percent)
{
    m_statusLabel->setText(message);
    if (percent >= 0)
        m_progress->setValue(qBound(0, percent, 100));
}

void HidSeosPage::onFinished(const HidSeos::ReadResult &result)
{
    m_result = result;
    m_hasResult = result.selectOk;
    populate(result);
    setBusy(false);
    m_progress->setValue(100);

    if (result.cancelled)
        m_statusLabel->setText(tr("Read cancelled; showing partial data."));
    else if (result.partial)
        m_statusLabel->setText(tr("Read finished with warnings; some records may be missing."));
    else
        m_statusLabel->setText(tr("Read complete."));
}

void HidSeosPage::onFailed(const QString &message)
{
    setBusy(false);
    m_progress->setValue(0);
    m_statusLabel->setText(message);
}

void HidSeosPage::updateStatusLine()
{
    QStringList quality;
    if (m_result.partial)
        quality.append(tr("partial"));
    if (!m_result.warnings.isEmpty())
        quality.append(tr("%1 warning(s)").arg(m_result.warnings.size()));
    m_qualityLabel->setText(quality.join(QStringLiteral(" · ")));
}

void HidSeosPage::populate(const HidSeos::ReadResult &result)
{
    populateCardPanel();
    populateTransactions();
    populateTrips();
    populateFinancial();
    populateRaw();
    updateStatusLine();

    if (!result.warnings.isEmpty()) {
        m_qualityLabel->setToolTip(result.warnings.join(QLatin1Char('\n')));
        m_statusLabel->setToolTip(result.warnings.join(QLatin1Char('\n')));
    } else {
        m_qualityLabel->setToolTip(QString());
        m_statusLabel->setToolTip(QString());
    }
}

QString HidSeosPage::formatKrw(qint64 value) const
{
    return QStringLiteral("%1 KRW").arg(QLocale(QLocale::English).toString(value));
}

QString HidSeosPage::formatCount(qint64 value) const
{
    return QLocale(QLocale::English).toString(value);
}

QString HidSeosPage::cardTypeText() const
{
    if (!m_result.purse.valid)
        return QStringLiteral("—");
    const QString code = hexByte(m_result.purse.cardTypeCode);
    const QString label = HidSeos::Codes::cardTypeLabel(m_result.purse.cardTypeCode);
    if (label.isEmpty())
        return tr("Unknown (%1)").arg(code);
    return QStringLiteral("%1 — %2").arg(code, label);
}

QString HidSeosPage::cardKindText() const
{
    switch (m_result.kind) {
    case HidSeos::CardKind::TMoney: return tr("T-money");
    case HidSeos::CardKind::Climate: return tr("Climate Card");
    case HidSeos::CardKind::Unknown: break;
    }
    return tr("Unknown");
}

QString HidSeosPage::issuerText() const
{
    if (!m_result.purse.valid)
        return QStringLiteral("—");
    const QString label = HidSeos::Codes::issuerLabel(m_result.purse.idCenter);
    if (!label.isEmpty())
        return label;
    return tr("Unknown (%1)").arg(hexByte(m_result.purse.idCenter));
}

QString HidSeosPage::userTypeText() const
{
    if (!m_result.purse.valid)
        return QStringLiteral("—");
    const QString label = HidSeos::Codes::userTypeLabel(m_result.purse.userCode);
    if (!label.isEmpty())
        return label;
    return tr("Unknown (%1)").arg(hexByte(m_result.purse.userCode));
}

QString HidSeosPage::balanceValueText() const
{
    if (m_result.balance.known)
        return formatKrw(m_result.balance.amountKrw);
    if (!m_result.balance.supported && m_result.balance.sw == 0x6D00)
        return tr("N/A");
    return QStringLiteral("—");
}

QString HidSeosPage::balanceNoteText() const
{
    if (!m_result.balance.reason.isEmpty())
        return m_result.balance.reason;
    if (m_result.balance.known)
        return tr("Stored balance returned by GET BALANCE (90 4C).");
    return QString();
}

QString HidSeosPage::passValidityText() const
{
    if (!m_result.pass.present)
        return QStringLiteral("—");
    return QStringLiteral("%1 → %2").arg(m_result.pass.validFrom, m_result.pass.validTo);
}

QString HidSeosPage::calibrationText() const
{
    const HidSeos::TimeCalibration &calibration = m_result.calibration;
    if (calibration.verified) {
        return tr("%1 (%2 day(s), verified)")
            .arg(calibration.observedEpoch.toString(Qt::ISODate))
            .arg(calibration.offsetDays);
    }
    return tr("%1 (community default)").arg(calibration.communityEpoch.toString(Qt::ISODate));
}

void HidSeosPage::populateCardPanel()
{
    const HidSeos::ReadResult &result = m_result;
    m_balanceLabel->setText(balanceValueText());
    m_balanceNote->setText(balanceNoteText());

    m_cardTypeValue->setText(cardTypeText());
    m_cardKindValue->setText(cardKindText());
    m_uidValue->setText(result.uid.isEmpty() ? QStringLiteral("—") : result.uid);
    m_csnValue->setText(result.purse.csn.isEmpty() ? QStringLiteral("—") : result.purse.csn);
    m_issuerValue->setText(issuerText());
    m_userValue->setText(userTypeText());
    m_issueValue->setText(result.purse.issueDate.isEmpty() ? QStringLiteral("—")
                                                           : result.purse.issueDate);
    m_expiryValue->setText(result.purse.expiryDate.isEmpty() ? QStringLiteral("—")
                                                             : result.purse.expiryDate);

    QString protocol = result.protocol;
    QStringList details;
    if (!result.atqa.isEmpty())
        details.append(tr("ATQA: %1").arg(result.atqa));
    if (!result.sak.isEmpty())
        details.append(tr("SAK: %1").arg(result.sak));
    if (!result.ats.isEmpty())
        details.append(tr("ATS: %1").arg(result.ats));
    if (!details.isEmpty())
        protocol += QStringLiteral(" (%1)").arg(details.join(QStringLiteral(", ")));
    m_protocolValue->setText(protocol.isEmpty() ? QStringLiteral("—") : protocol);
    m_protocolValue->setToolTip(details.join(QLatin1Char('\n')));

    if (result.pass.present) {
        m_passValidValue->setText(passValidityText());
        m_passDaysValue->setText(tr("%1 day(s)").arg(result.pass.days));
        m_passProductValue->setText(QStringLiteral("0x%1")
                                        .arg(result.pass.productCode, 4, 16, QLatin1Char('0'))
                                        .toUpper()
                                        .replace(QStringLiteral("0X"), QStringLiteral("0x")));
        m_passStatusValue->setText(hexByte(result.pass.status));
    } else {
        m_passValidValue->setText(QStringLiteral("—"));
        m_passDaysValue->setText(QStringLiteral("—"));
        m_passProductValue->setText(QStringLiteral("—"));
        m_passStatusValue->setText(QStringLiteral("—"));
    }
    m_passProductValue->setToolTip(result.pass.present
        ? tr("Stored product bytes: %1; their semantics are not publicly documented.")
              .arg(hexBytes(result.pass.unknown))
        : QString());

    m_calibrationValue->setText(calibrationText());
    m_calibrationValue->setToolTip(result.calibration.source);
}

void HidSeosPage::populateTransactions()
{
    m_transactionTable->clearContents();
    m_transactionTable->setRowCount(m_result.trips.size());

    for (int row = 0; row < m_result.trips.size(); ++row) {
        const HidSeos::TripRecord &trip = m_result.trips.at(row);
        m_transactionTable->setItem(row, 0, textItem(formatCount(trip.index)));
        m_transactionTable->setItem(row, 1, textItem(trip.date));
        m_transactionTable->setItem(row, 2, textItem(trip.time));
        m_transactionTable->setItem(row, 3, textItem(eventText(trip.eventCode)));
        m_transactionTable->setItem(row, 4, textItem(transportText(trip.transportCode)));
        m_transactionTable->setItem(row, 5, textItem(formatCount(trip.counter)));
        m_transactionTable->setItem(row, 6, textItem(formatCount(trip.journeyCounter)));
        m_transactionTable->setItem(row, 7, textItem(formatCount(trip.journeyFareKrw)));
        m_transactionTable->setItem(row, 8, textItem(formatCount(trip.distanceM)));

        if (trip.journeyFareKrw > 0) {
            QTableWidgetItem *fare = m_transactionTable->item(row, 7);
            fare->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }
    }
    m_transactionTable->resizeColumnsToContents();
    m_transactionDetail->clear();
}

void HidSeosPage::populateTrips()
{
    m_tripTable->clearContents();
    m_tripTable->setRowCount(m_result.groupedTrips.size());

    for (int row = 0; row < m_result.groupedTrips.size(); ++row) {
        const HidSeos::GroupedTrip &trip = m_result.groupedTrips.at(row);
        QStringList legs;
        for (const HidSeos::GroupedTripLeg &leg : trip.legs) {
            QString text = transportText(leg.transportCode);
            if (!leg.boardTime.isEmpty() || !leg.alightTime.isEmpty()) {
                text += QLatin1Char(' ') + (leg.boardTime.isEmpty() ? QStringLiteral("—") : leg.boardTime)
                        + QStringLiteral("→")
                        + (leg.alightTime.isEmpty() ? QStringLiteral("—") : leg.alightTime);
            }
            legs.append(text);
        }

        m_tripTable->setItem(row, 0, textItem(formatCount(row + 1)));
        m_tripTable->setItem(row, 1, textItem(trip.date));
        m_tripTable->setItem(row, 2, textItem(trip.startTime.isEmpty() ? QStringLiteral("—")
                                                                       : trip.startTime));
        m_tripTable->setItem(row, 3, textItem(trip.endTime.isEmpty() ? QStringLiteral("—")
                                                                     : trip.endTime));
        m_tripTable->setItem(row, 4, textItem(legs.join(QStringLiteral(" + "))));
        m_tripTable->setItem(row, 5, textItem(formatCount(trip.tapCount)));
        m_tripTable->setItem(row, 6, textItem(formatCount(trip.journeyFareKrw)));
    }
    m_tripTable->resizeColumnsToContents();
    m_tripDetail->clear();
}

void HidSeosPage::populateFinancial()
{
    const int total = m_result.financial.size() + m_result.readEvents.size();
    m_financialTable->clearContents();
    m_financialTable->setRowCount(total);

    int row = 0;
    for (const HidSeos::FinancialRecord &record : m_result.financial) {
        m_financialTable->setItem(row, 0, textItem(formatCount(record.index)));
        m_financialTable->setItem(row, 1, textItem(formatCount(record.sfi)));
        m_financialTable->setItem(row, 2, textItem(financialTypeText(record.type)));
        m_financialTable->setItem(row, 3, textItem(formatCount(record.balanceKrw)));
        m_financialTable->setItem(row, 4, textItem(formatCount(record.sequence)));
        m_financialTable->setItem(row, 5, textItem(formatCount(record.fareKrw)));
        m_financialTable->setItem(row, 6, textItem(markerText(record.marker)));
        m_financialTable->setItem(row, 7, textItem(record.terminal));
        ++row;
    }
    for (const HidSeos::FinancialRecord &record : m_result.readEvents) {
        m_financialTable->setItem(row, 0, textItem(formatCount(record.index)));
        m_financialTable->setItem(row, 1, textItem(formatCount(record.sfi)));
        m_financialTable->setItem(row, 2, textItem(tr("card-read event")));
        m_financialTable->setItem(row, 3, textItem(formatCount(record.balanceKrw)));
        m_financialTable->setItem(row, 4, textItem(formatCount(record.sequence)));
        m_financialTable->setItem(row, 5, textItem(formatCount(record.fareKrw)));
        m_financialTable->setItem(row, 6, textItem(markerText(record.marker)));
        m_financialTable->setItem(row, 7, textItem(record.terminal));
        ++row;
    }
    m_financialTable->resizeColumnsToContents();
    m_financialDetail->clear();
}

void HidSeosPage::populateRaw()
{
    m_rawTable->clearContents();
    m_rawTable->setRowCount(m_result.rawRecords.size());

    for (int row = 0; row < m_result.rawRecords.size(); ++row) {
        const HidSeos::RawRecord &record = m_result.rawRecords.at(row);
        m_rawTable->setItem(row, 0, textItem(formatCount(record.sfi)));
        QString file = HidSeos::Codes::sfiName(record.sfi);
        if (file.isEmpty())
            file = tr("Unknown SFI");
        m_rawTable->setItem(row, 1, textItem(file));
        m_rawTable->setItem(row, 2, textItem(record.index == 0 ? QStringLiteral("—")
                                                               : formatCount(record.index)));
        m_rawTable->setItem(row, 3, textItem(formatCount(record.data.size())));
        m_rawTable->setItem(row, 4, textItem(hexBytes(record.data)));
    }
    m_rawTable->resizeColumnsToContents();
}

QString HidSeosPage::eventText(int code) const
{
    switch (code) {
    case 0x00: return tr("boarding");
    case 0x01: return tr("alighting");
    default: break;
    }
    return tr("Unknown (%1)").arg(hexByte(code));
}

QString HidSeosPage::transportText(int code) const
{
    switch (code) {
    case 0x00: return tr("bus");
    case 0x01: return tr("metro");
    case 0x02: return tr("train");
    default: break;
    }
    return tr("Unknown (%1)").arg(hexByte(code));
}

QString HidSeosPage::markerText(int code) const
{
    const QString label = HidSeos::Codes::markerLabel(code);
    const QString hex = QStringLiteral("0x%1")
                            .arg(code, 4, 16, QLatin1Char('0'))
                            .toUpper()
                            .replace(QStringLiteral("0X"), QStringLiteral("0x"));
    if (label.isEmpty())
        return tr("Unknown (%1)").arg(hex);
    return QStringLiteral("%1 (%2)").arg(hex, label);
}

QString HidSeosPage::financialTypeText(int code) const
{
    switch (code) {
    case 0x01: return tr("transit");
    case 0x02: return tr("top-up");
    default: break;
    }
    return tr("Unknown (%1)").arg(hexByte(code));
}

QString HidSeosPage::tripDetails(const HidSeos::TripRecord &trip) const
{
    QStringList lines;
    lines.append(tr("SFI 3 record #%1").arg(trip.index));
    lines.append(tr("Date: %1  Time: %2 (second %3)")
                     .arg(trip.date, trip.time)
                     .arg(trip.second));
    lines.append(tr("Event: %1 (%2)").arg(eventText(trip.eventCode), hexByte(trip.eventCode)));
    lines.append(tr("Transport: %1 (%2)")
                     .arg(transportText(trip.transportCode), hexByte(trip.transportCode)));
    lines.append(tr("Journey counter: %1").arg(trip.counter));
    lines.append(tr("Journey fare (recorded): %1").arg(formatKrw(trip.journeyFareKrw)));
    lines.append(tr("Leg base fare: %1").arg(formatKrw(trip.fareBaseKrw)));
    lines.append(tr("Fare field 22: %1").arg(formatKrw(trip.fareField22Krw)));
    lines.append(tr("Distance: %1 m").arg(trip.distanceM));
    lines.append(tr("Station / terminal: %1").arg(trip.stationId));
    lines.append(tr("Device: %1").arg(trip.device));
    lines.append(tr("Surcharge: %1").arg(formatKrw(trip.surchargeKrw)));
    lines.append(tr("Packed timestamp: %1 (day %2)")
                     .arg(trip.rawTimestamp)
                     .arg(trip.dayCounter));
    lines.append(tr("Community-epoch time: %1").arg(trip.communityDatetime));
    lines.append(tr("Calibrated time: %1").arg(trip.datetime));
    lines.append(QString());
    lines.append(tr("Recorded fare is a gate-written nominal amount; the pass may cover it."));
    lines.append(QString());
    lines.append(tr("Raw: %1").arg(hexBytes(trip.raw)));
    return lines.join(QLatin1Char('\n'));
}

QString HidSeosPage::tripDetails(const HidSeos::GroupedTrip &trip) const
{
    QStringList lines;
    lines.append(tr("Grouped journey (counter %1)").arg(trip.counter));
    lines.append(tr("Date: %1  Start: %2  End: %3")
                     .arg(trip.date,
                          trip.startTime.isEmpty() ? QStringLiteral("—") : trip.startTime,
                          trip.endTime.isEmpty() ? QStringLiteral("—") : trip.endTime));
    lines.append(tr("Taps: %1").arg(trip.tapCount));
    lines.append(tr("Recorded journey fare: %1").arg(formatKrw(trip.journeyFareKrw)));
    lines.append(QString());
    lines.append(tr("Legs:"));
    int index = 1;
    for (const HidSeos::GroupedTripLeg &leg : trip.legs) {
        lines.append(tr("  %1. %2 %3 → %4  base %5  distance %6 m")
                         .arg(index)
                         .arg(transportText(leg.transportCode))
                         .arg(leg.boardTime.isEmpty() ? QStringLiteral("—") : leg.boardTime)
                         .arg(leg.alightTime.isEmpty() ? QStringLiteral("—") : leg.alightTime)
                         .arg(formatKrw(leg.fareBaseKrw))
                         .arg(leg.distanceM));
        ++index;
    }
    lines.append(QString());
    lines.append(tr("Taps in this journey:"));
    for (const HidSeos::TripRecord &tap : m_result.trips) {
        if (tap.counter != trip.counter)
            continue;
        lines.append(tr("  #%1 %2 %3 %4 — %5")
                         .arg(tap.index)
                         .arg(tap.date, tap.time,
                              eventText(tap.eventCode),
                              formatKrw(tap.fareTotalKrw)));
    }
    lines.append(QString());
    lines.append(tr("Transfer legs share one journey counter; their amounts are cumulative, not repeated charges."));
    return lines.join(QLatin1Char('\n'));
}

QString HidSeosPage::financialDetails(const HidSeos::FinancialRecord &record) const
{
    QStringList lines;
    lines.append(tr("SFI %1 record #%2").arg(record.sfi).arg(record.index));
    if (record.sfi == 11)
        lines.append(tr("Type: card-read event"));
    else
        lines.append(tr("Type: %1 (%2)").arg(financialTypeText(record.type), hexByte(record.type)));
    lines.append(tr("Balance after: %1").arg(formatKrw(record.balanceKrw)));
    lines.append(tr("Sequence: %1").arg(record.sequence));
    lines.append(tr("Fare: %1").arg(formatKrw(record.fareKrw)));
    lines.append(tr("Marker: %1").arg(markerText(record.marker)));
    lines.append(tr("Terminal: %1").arg(record.terminal));
    lines.append(tr("BCD date/time: %1").arg(record.bcdDatetime.isEmpty()
                                                  ? QStringLiteral("—")
                                                  : record.bcdDatetime));
    if (record.marker == 0x0720) {
        lines.append(tr("Note: marker 0x0720 is a card-read / initialization event, not a purchase."));
    }
    if (record.marker == 0x0000) {
        lines.append(tr("Note: marker 0x0000 is a top-up record."));
    }
    lines.append(QString());
    lines.append(tr("Raw: %1").arg(hexBytes(record.raw)));
    return lines.join(QLatin1Char('\n'));
}

void HidSeosPage::onTransactionSelectionChanged()
{
    const int row = m_transactionTable->currentRow();
    if (row < 0 || row >= m_result.trips.size()) {
        m_transactionDetail->clear();
        return;
    }
    m_transactionDetail->setPlainText(tripDetails(m_result.trips.at(row)));
}

void HidSeosPage::onTripSelectionChanged()
{
    const int row = m_tripTable->currentRow();
    if (row < 0 || row >= m_result.groupedTrips.size()) {
        m_tripDetail->clear();
        return;
    }
    m_tripDetail->setPlainText(tripDetails(m_result.groupedTrips.at(row)));
}

void HidSeosPage::onFinancialSelectionChanged()
{
    const int row = m_financialTable->currentRow();
    const int count = m_result.financial.size() + m_result.readEvents.size();
    if (row < 0 || row >= count) {
        m_financialDetail->clear();
        return;
    }
    if (row < m_result.financial.size())
        m_financialDetail->setPlainText(financialDetails(m_result.financial.at(row)));
    else
        m_financialDetail->setPlainText(
            financialDetails(m_result.readEvents.at(row - m_result.financial.size())));
}

void HidSeosPage::showTransactionDetails()
{
    const int row = m_transactionTable->currentRow();
    if (row < 0 || row >= m_result.trips.size())
        return;
    showDetailsDialog(tr("Trip record #%1").arg(m_result.trips.at(row).index),
                      tripDetails(m_result.trips.at(row)));
}

void HidSeosPage::showTripDetails()
{
    const int row = m_tripTable->currentRow();
    if (row < 0 || row >= m_result.groupedTrips.size())
        return;
    showDetailsDialog(tr("Grouped journey %1").arg(row + 1),
                      tripDetails(m_result.groupedTrips.at(row)));
}

void HidSeosPage::showFinancialDetails()
{
    const int row = m_financialTable->currentRow();
    const int count = m_result.financial.size() + m_result.readEvents.size();
    if (row < 0 || row >= count)
        return;
    if (row < m_result.financial.size()) {
        const HidSeos::FinancialRecord &record = m_result.financial.at(row);
        showDetailsDialog(tr("SFI %1 record #%2").arg(record.sfi).arg(record.index),
                          financialDetails(record));
    } else {
        const HidSeos::FinancialRecord &record =
            m_result.readEvents.at(row - m_result.financial.size());
        showDetailsDialog(tr("SFI 11 record #%1").arg(record.index), financialDetails(record));
    }
}

void HidSeosPage::showDetailsDialog(const QString &title, const QString &text)
{
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("HidSeos_detailDialog"));
    dialog.setWindowTitle(title);
    dialog.resize(760, 560);

    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    QPlainTextEdit *view = new QPlainTextEdit(&dialog);
    view->setReadOnly(true);
    view->setPlainText(text);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    layout->addWidget(view, 1);
    layout->addWidget(buttons);
    dialog.exec();
}

void HidSeosPage::exportJson()
{
    if (!m_hasResult)
        return;

    const QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested = QDir(base).filePath(
        QStringLiteral("hidseos_%1.json")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmm"))));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export JSON"), suggested,
                                                      tr("JSON files (*.json)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, tr("Export failed"), file.errorString());
        return;
    }
    file.write(HidSeos::Export::toJson(m_result));
    file.close();
    m_statusLabel->setText(tr("Exported JSON: %1").arg(path));
}

void HidSeosPage::exportCsv()
{
    if (!m_hasResult)
        return;

    const QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString suggested = QDir(base).filePath(
        QStringLiteral("hidseos_%1")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmm"))));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export CSV"), suggested,
                                                      tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;

    const QString stem = path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)
                             ? path.left(path.size() - 4)
                             : path;
    const QString transactionsPath = stem + QStringLiteral("_transactions.csv");
    const QString tripsPath = stem + QStringLiteral("_trips.csv");

    auto writeFile = [this](const QString &filePath, const QByteArray &content) -> bool
    {
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(this, tr("Export failed"), file.errorString());
            return false;
        }
        file.write(content);
        file.close();
        return true;
    };

    if (!writeFile(transactionsPath, HidSeos::Export::transactionsCsv(m_result))
        || !writeFile(tripsPath, HidSeos::Export::tripsCsv(m_result)))
    {
        return;
    }

    m_statusLabel->setText(tr("Exported CSV: %1, %2").arg(transactionsPath, tripsPath));
}
