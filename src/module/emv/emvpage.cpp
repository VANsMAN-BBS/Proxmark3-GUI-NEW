#include "emvpage.h"

#include "emvexport.h"
#include "emvworker.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QThread>
#include <QTime>
#include <QVBoxLayout>

namespace {

QTableWidgetItem *readOnlyItem(const QString &text)
{
    QTableWidgetItem *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

EmvPage::EmvPage(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    resetView();
}

EmvPage::~EmvPage()
{
    shutdown();
}

void EmvPage::setDeviceInfo(const QString &port, const QString &clientPath)
{
    m_port = port;
    m_clientPath = clientPath;
    updateInfo();
}

bool EmvPage::isReading() const
{
    return m_reading;
}

void EmvPage::buildUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);

    QHBoxLayout *actions = new QHBoxLayout();
    m_readButton = new QPushButton(tr("Read Transactions"), this);
    m_readButton->setObjectName(QStringLiteral("Emv_readButton"));
    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setObjectName(QStringLiteral("Emv_cancelButton"));
    m_exportCsvButton = new QPushButton(tr("Export CSV"), this);
    m_exportCsvButton->setObjectName(QStringLiteral("Emv_exportCsvButton"));
    m_exportJsonButton = new QPushButton(tr("Export JSON"), this);
    m_exportJsonButton->setObjectName(QStringLiteral("Emv_exportJsonButton"));
    m_clearButton = new QPushButton(tr("Clear View"), this);
    m_clearButton->setObjectName(QStringLiteral("Emv_clearButton"));

    actions->addWidget(m_readButton);
    actions->addWidget(m_cancelButton);
    actions->addSpacing(12);
    actions->addWidget(m_exportCsvButton);
    actions->addWidget(m_exportJsonButton);
    actions->addWidget(m_clearButton);
    actions->addStretch();
    root->addLayout(actions);

    QHBoxLayout *statusLayout = new QHBoxLayout();
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("Emv_statusLabel"));
    m_statusLabel->setWordWrap(true);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setMaximumWidth(220);
    statusLayout->addWidget(m_statusLabel, 1);
    statusLayout->addWidget(m_progress);
    root->addLayout(statusLayout);

    QGroupBox *infoGroup = new QGroupBox(tr("Reader status"), this);
    QFormLayout *infoLayout = new QFormLayout(infoGroup);
    m_portValue = new QLabel(this);
    m_cardValue = new QLabel(this);
    m_countValue = new QLabel(this);
    m_reasonValue = new QLabel(this);
    m_reasonValue->setWordWrap(true);
    infoLayout->addRow(tr("Port:"), m_portValue);
    infoLayout->addRow(tr("Card detected:"), m_cardValue);
    infoLayout->addRow(tr("Records:"), m_countValue);
    infoLayout->addRow(tr("Stop reason:"), m_reasonValue);
    root->addWidget(infoGroup);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("Emv_transactionTable"));
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(QStringList()
        << tr("#") << tr("Date") << tr("Time") << tr("Merchant") << tr("Amount")
        << tr("Record #"));
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(m_table, 2);

    QGroupBox *detailsGroup = new QGroupBox(tr("Details"), this);
    QVBoxLayout *detailsLayout = new QVBoxLayout(detailsGroup);
    m_details = new QPlainTextEdit(detailsGroup);
    m_details->setObjectName(QStringLiteral("Emv_details"));
    m_details->setReadOnly(true);
    m_details->setPlaceholderText(tr("Warnings and reader diagnostics appear here."));
    detailsLayout->addWidget(m_details);
    root->addWidget(detailsGroup, 1);

    connect(m_readButton, &QPushButton::clicked, this, &EmvPage::readCard);
    connect(m_cancelButton, &QPushButton::clicked, this, &EmvPage::cancelRead);
    connect(m_exportCsvButton, &QPushButton::clicked, this, &EmvPage::exportCsv);
    connect(m_exportJsonButton, &QPushButton::clicked, this, &EmvPage::exportJson);
    connect(m_clearButton, &QPushButton::clicked, this, &EmvPage::clearView);
}

void EmvPage::readCard()
{
    if (m_reading)
        return;

    resetView();
    setStatus(tr("Preparing the EMV reader…"));
    setBusy(true);

    // MainWindow stops its own proxmark3.exe synchronously here so that the
    // reader process is the only owner of the serial port.
    emit aboutToRead();
    startWorker();
}

void EmvPage::startWorker()
{
    m_thread = new QThread(this);
    m_worker = new EmvWorker;
    m_worker->setPort(m_port);
    m_worker->setClientPath(m_clientPath);
    m_worker->moveToThread(m_thread);

    connect(m_thread, &QThread::started, m_worker.data(), &EmvWorker::start);
    connect(m_worker.data(), &EmvWorker::started, this, &EmvPage::onStarted);
    connect(m_worker.data(), &EmvWorker::statusChanged, this, &EmvPage::onStatus);
    connect(m_worker.data(), &EmvWorker::recordReady, this, &EmvPage::onRecord);
    connect(m_worker.data(), &EmvWorker::warningAdded, this, &EmvPage::onWarning);
    connect(m_worker.data(), &EmvWorker::finished, this, &EmvPage::onFinished);
    connect(m_worker.data(), &EmvWorker::finished, m_thread, &QThread::quit);
    connect(m_thread, &QThread::finished, m_worker.data(), &QObject::deleteLater);
    connect(m_thread, &QThread::finished, this, [this]()
    {
        if (m_thread)
            m_thread->deleteLater();
        m_thread = nullptr;
        m_worker = nullptr;
    });

    m_thread->start();
}

void EmvPage::cancelRead()
{
    if (!m_reading)
        return;
    setStatus(tr("Cancelling…"));
    if (m_worker)
        QMetaObject::invokeMethod(m_worker.data(), "cancel", Qt::QueuedConnection);
}

void EmvPage::stopThread()
{
    if (m_thread && m_thread->isRunning() && m_worker)
        QMetaObject::invokeMethod(m_worker.data(), "cancel", Qt::BlockingQueuedConnection);
    if (m_thread)
    {
        m_thread->quit();
        if (!m_thread->wait(6000))
        {
            m_thread->terminate();
            m_thread->wait(1000);
        }
        m_thread->deleteLater();
    }
    m_thread = nullptr;
    m_worker = nullptr;
}

void EmvPage::shutdown()
{
    stopThread();
    m_reading = false;
}

void EmvPage::onStarted(const QString &port)
{
    if (!port.isEmpty())
        m_port = port;
    updateInfo();
    setStatus(tr("Reading… hold the card against the antenna."));
}

void EmvPage::onStatus(const QString &message)
{
    if (!message.isEmpty())
        setStatus(message);
}

void EmvPage::onRecord(const Emv::Txn &txn)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, readOnlyItem(QString::number(row + 1)));
    m_table->setItem(row, 1, readOnlyItem(txn.date));
    m_table->setItem(row, 2, readOnlyItem(txn.time));
    m_table->setItem(row, 3, readOnlyItem(txn.merchant));
    m_table->setItem(row, 4, readOnlyItem(txn.amount));
    m_table->setItem(row, 5, readOnlyItem(txn.recordNo > 0 ? QString::number(txn.recordNo)
                                                           : QStringLiteral("—")));
    m_table->scrollToBottom();
    m_countValue->setText(QString::number(row + 1));
    setStatus(tr("%1 transaction record(s) read…").arg(row + 1));
}

void EmvPage::onWarning(const QString &warning)
{
    if (!warning.isEmpty())
        appendDetail(warning);
}

void EmvPage::onFinished(const Emv::ReadResult &result)
{
    m_result = result;
    m_hasResult = true;
    setBusy(false);
    if (m_table->rowCount() != result.txns.size())
        populateTable();
    updateInfo();

    appendDetail(tr("Stop reason: %1 (return code %2)")
                     .arg(stopReasonText(result.stopReason))
                     .arg(result.returnCode));
    for (const QString &warning : result.warnings)
        appendDetail(warning);

    if (!result.txns.isEmpty())
        setStatus(tr("Read complete: %1 record(s).").arg(result.txns.size()));
    else
        setStatus(stopReasonText(result.stopReason));

    emit readFinished();
}

void EmvPage::setBusy(bool busy)
{
    const bool changed = (m_reading != busy);
    m_reading = busy;

    if (busy)
    {
        m_progress->setRange(0, 0); // indeterminate
        m_exportCsvButton->setEnabled(false);
        m_exportJsonButton->setEnabled(false);
        m_clearButton->setEnabled(false);
        m_readButton->setEnabled(false);
        m_cancelButton->setEnabled(true);
    }
    else
    {
        m_progress->setRange(0, 100);
        m_progress->setValue(m_hasResult ? 100 : 0);
        m_readButton->setEnabled(true);
        m_cancelButton->setEnabled(false);
        updateInfo();
    }

    if (changed)
        emit readingChanged(busy);
}

void EmvPage::resetView()
{
    m_result = Emv::ReadResult();
    m_hasResult = false;
    m_table->setRowCount(0);
    m_details->clear();
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    updateInfo();
    if (!m_reading)
        setStatus(tr("Idle. Connect the Proxmark3 and press Read Transactions."));
}

void EmvPage::updateInfo()
{
    if (!m_portValue)
        return;
    m_portValue->setText(m_port.isEmpty() ? tr("(auto-detect)") : m_port);
    m_cardValue->setText(!m_hasResult ? QStringLiteral("—")
                                      : (m_result.cardDetected ? tr("Yes") : tr("No")));
    m_countValue->setText(m_hasResult ? QString::number(m_result.txns.size())
                                     : QStringLiteral("0"));
    m_reasonValue->setText(m_hasResult ? stopReasonText(m_result.stopReason)
                                       : tr("Not read yet"));

    const bool hasRows = m_hasResult && !m_result.txns.isEmpty() && !m_reading;
    if (m_exportCsvButton)
        m_exportCsvButton->setEnabled(hasRows);
    if (m_exportJsonButton)
        m_exportJsonButton->setEnabled(hasRows);
    if (m_clearButton)
        m_clearButton->setEnabled(!m_reading && (m_hasResult || m_table->rowCount() > 0));
    if (m_readButton)
        m_readButton->setEnabled(!m_reading);
    if (m_cancelButton)
        m_cancelButton->setEnabled(m_reading);
}

void EmvPage::populateTable()
{
    m_table->setRowCount(0);
    for (int i = 0; i < m_result.txns.size(); ++i)
    {
        const Emv::Txn &txn = m_result.txns.at(i);
        m_table->insertRow(i);
        m_table->setItem(i, 0, readOnlyItem(QString::number(i + 1)));
        m_table->setItem(i, 1, readOnlyItem(txn.date));
        m_table->setItem(i, 2, readOnlyItem(txn.time));
        m_table->setItem(i, 3, readOnlyItem(txn.merchant));
        m_table->setItem(i, 4, readOnlyItem(txn.amount));
        m_table->setItem(i, 5, readOnlyItem(txn.recordNo > 0 ? QString::number(txn.recordNo)
                                                             : QStringLiteral("—")));
    }
    m_countValue->setText(QString::number(m_result.txns.size()));
}

void EmvPage::setStatus(const QString &message)
{
    if (m_statusLabel)
        m_statusLabel->setText(message);
}

void EmvPage::appendDetail(const QString &line)
{
    if (!m_details || line.isEmpty())
        return;
    const QString stamp = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    m_details->appendPlainText(QStringLiteral("[%1] %2").arg(stamp, line));
}

QString EmvPage::stopReasonText(const QString &reason) const
{
    // Values come from emv_log_reader/README.md §5.2; nothing is invented here.
    if (reason == QLatin1String("done"))
        return tr("Done — card read finished");
    if (reason == QLatin1String("no_card"))
        return tr("No card detected — hold the card against the antenna and try again");
    if (reason == QLatin1String("cancelled"))
        return tr("Cancelled — records read so far were kept");
    if (reason == QLatin1String("timeout"))
        return tr("Timed out — partial records were kept");
    if (reason == QLatin1String("error"))
        return tr("Failed — see Details");
    return tr("Not read yet");
}

QString EmvPage::defaultExportName(const QString &suffix) const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    return QDir(dir).filePath(QStringLiteral("emv_transactions_%1.%2").arg(stamp, suffix));
}

void EmvPage::exportCsv()
{
    if (!m_hasResult || m_result.txns.isEmpty())
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export EMV transactions as CSV"), defaultExportName(QStringLiteral("csv")),
        tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QFile::WriteOnly))
    {
        QMessageBox::warning(this, tr("Export failed"),
                             tr("Cannot write the CSV file:\n%1").arg(path));
        return;
    }
    file.write(EmvExport::toCsv(m_result.txns));
    file.close();
    setStatus(tr("Exported %1 record(s) to %2").arg(m_result.txns.size()).arg(path));
}

void EmvPage::exportJson()
{
    if (!m_hasResult || m_result.txns.isEmpty())
        return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export EMV transactions as JSON"), defaultExportName(QStringLiteral("json")),
        tr("JSON files (*.json)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QFile::WriteOnly))
    {
        QMessageBox::warning(this, tr("Export failed"),
                             tr("Cannot write the JSON file:\n%1").arg(path));
        return;
    }
    file.write(EmvExport::toJson(m_result));
    file.close();
    setStatus(tr("Exported %1 record(s) to %2").arg(m_result.txns.size()).arg(path));
}

void EmvPage::clearView()
{
    if (m_reading)
        return;
    resetView();
}
