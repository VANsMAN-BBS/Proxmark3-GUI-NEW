#include "ntagtab.h"

#include "common/util.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QEventLoop>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {
const char *kPageDataPattern = "(\\d+)/0x[0-9A-Fa-f]+\\s*\\|\\s*((?:[0-9A-Fa-f]{2}\\s*){4})";
}

NtagTab::NtagTab(Util *util, QWidget *parent)
    : QWidget(parent), util(util)
{
    buildUi();
}

NtagTab::~NtagTab() = default;

void NtagTab::buildUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QGroupBox *cardGroup = new QGroupBox(tr("Card"), this);
    QGridLayout *cardLayout = new QGridLayout(cardGroup);

    QPushButton *detectButton = new QPushButton(tr("Detect / Read Info"), cardGroup);
    connect(detectButton, &QPushButton::clicked, this, &NtagTab::detectCard);
    cardLayout->addWidget(detectButton, 0, 0, 1, 2);

    cardLayout->addWidget(new QLabel(tr("UID:"), cardGroup), 1, 0);
    uidLabel = new QLabel(QStringLiteral("-"), cardGroup);
    uidLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cardLayout->addWidget(uidLabel, 1, 1);

    cardLayout->addWidget(new QLabel(tr("Type:"), cardGroup), 2, 0);
    typeLabel = new QLabel(tr("Not detected"), cardGroup);
    typeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cardLayout->addWidget(typeLabel, 2, 1);

    cardLayout->addWidget(new QLabel(tr("Pages:"), cardGroup), 3, 0);
    pagesLabel = new QLabel(QStringLiteral("-"), cardGroup);
    cardLayout->addWidget(pagesLabel, 3, 1);

    cardLayout->addWidget(new QLabel(tr("Key (optional):"), cardGroup), 4, 0);
    keyEdit = new QLineEdit(cardGroup);
    keyEdit->setPlaceholderText(tr("UL-C/AES 16 bytes or NTAG/EV1 4 bytes, hex"));
    cardLayout->addWidget(keyEdit, 4, 1);
    mainLayout->addWidget(cardGroup);

    QGroupBox *pagesGroup = new QGroupBox(tr("Pages"), this);
    QVBoxLayout *pagesLayout = new QVBoxLayout(pagesGroup);

    table = new QTableWidget(0, 4, pagesGroup);
    table->setHorizontalHeaderLabels({tr("Page"), tr("Data (hex)"), tr("ASCII"), tr("Access")});
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setColumnWidth(0, 55);
    table->setColumnWidth(2, 120);
    table->setColumnWidth(3, 150);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    pagesLayout->addWidget(table);

    QHBoxLayout *readLayout = new QHBoxLayout;
    QPushButton *readAllButton = new QPushButton(tr("Read All"), pagesGroup);
    QPushButton *readSelectedButton = new QPushButton(tr("Read Selected"), pagesGroup);
    QPushButton *readPageButton = new QPushButton(tr("Read Page"), pagesGroup);
    pageSpin = new QSpinBox(pagesGroup);
    pageSpin->setRange(0, 255);
    pageSpin->setPrefix(tr("Page "));
    readLayout->addWidget(readAllButton);
    readLayout->addWidget(readSelectedButton);
    readLayout->addWidget(pageSpin);
    readLayout->addWidget(readPageButton);
    readLayout->addStretch();
    pagesLayout->addLayout(readLayout);

    QHBoxLayout *writeLayout = new QHBoxLayout;
    dataEdit = new QLineEdit(pagesGroup);
    dataEdit->setPlaceholderText(tr("Page data: 8 hex digits"));
    QPushButton *writePageButton = new QPushButton(tr("Write Page"), pagesGroup);
    QPushButton *writeSelectedButton = new QPushButton(tr("Write Selected"), pagesGroup);
    QPushButton *verifyButton = new QPushButton(tr("Verify Page"), pagesGroup);
    writeLayout->addWidget(new QLabel(tr("Data:"), pagesGroup));
    writeLayout->addWidget(dataEdit, 1);
    writeLayout->addWidget(writePageButton);
    writeLayout->addWidget(writeSelectedButton);
    writeLayout->addWidget(verifyButton);
    pagesLayout->addLayout(writeLayout);

    statusLabel = new QLabel(tr("Experimental: no NTAG/Ultralight hardware verification yet."), pagesGroup);
    statusLabel->setWordWrap(true);
    pagesLayout->addWidget(statusLabel);

    mainLayout->addWidget(pagesGroup);

    connect(readAllButton, &QPushButton::clicked, this, &NtagTab::readAllPages);
    connect(readSelectedButton, &QPushButton::clicked, this, &NtagTab::readSelectedPages);
    connect(readPageButton, &QPushButton::clicked, this, &NtagTab::readOnePage);
    connect(writePageButton, &QPushButton::clicked, this, &NtagTab::writeOnePage);
    connect(writeSelectedButton, &QPushButton::clicked, this, &NtagTab::writeSelectedPages);
    connect(verifyButton, &QPushButton::clicked, this, &NtagTab::verifyOnePage);
}

void NtagTab::setStatus(const QString &text, bool error)
{
    statusLabel->setStyleSheet(error ? QStringLiteral("color:rgb(200,20,20);") : QString());
    statusLabel->setText(text);
}

QString NtagTab::normalizePageData(const QString &text)
{
    QString value = text;
    value.remove(QRegularExpression(QStringLiteral("\\s")));
    value = value.toUpper();
    if(value.size() != 8)
        return QString();
    for(const QChar ch : value)
    {
        if(!((ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
             (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'))))
            return QString();
    }
    return value;
}

QString NtagTab::asciiFor(const QString &hex)
{
    QString result;
    for(int i = 0; i < hex.size(); i += 2)
    {
        bool ok = false;
        const int value = hex.mid(i, 2).toInt(&ok, 16);
        if(!ok)
            return QString();
        result += (value >= 0x20 && value <= 0x7E) ? QChar(value) : QLatin1Char('.');
    }
    return result;
}

bool NtagTab::checkKey(QString *key) const
{
    QString value = keyEdit->text();
    value.remove(QRegularExpression(QStringLiteral("\\s")));
    value = value.toUpper();
    if(value.isEmpty())
    {
        if(key)
            key->clear();
        return true;
    }
    if(value.size() != 8 && value.size() != 32)
        return false;
    for(const QChar ch : value)
    {
        if(!((ch >= QLatin1Char('0') && ch <= QLatin1Char('9')) ||
             (ch >= QLatin1Char('A') && ch <= QLatin1Char('F'))))
            return false;
    }
    if(key)
        *key = value;
    return true;
}

int NtagTab::pagesForType(const QString &type, int *configStart) const
{
    struct TypeInfo { const char *token; int pages; int config; };
    static const TypeInfo types[] = {
        {"Ultralight C", 48, 40},
        {"Ultralight EV1 48", 20, 16},
        {"Ultralight EV1 128", 41, 36},
        {"Ultralight Nano", 11, 4},
        {"Ultralight (MF0ICU1)", 16, 16},
        {"NTAG 213", 45, 40},
        {"NTAG 215", 135, 130},
        {"NTAG 216", 231, 226},
        {"NTAG 203", 42, 37},
        {"NTAG 210", 20, 16},
        {"NTAG 212", 41, 36},
        {"NTAG 223", 60, 4},
        {"NTAG 224", 76, 4},
        {"Ultralight AES", 56, 4},
        {"FUDAN", 16, 4},
        {"ST25TN512", 16, 4},
        {"ST25TN01K", 40, 4}
    };

    for(const TypeInfo &info : types)
    {
        if(type.contains(QString::fromLatin1(info.token), Qt::CaseInsensitive))
        {
            if(configStart)
                *configStart = info.config;
            return info.pages;
        }
    }
    if(configStart)
        *configStart = 4;
    return 0;
}

bool NtagTab::parseInfo(const QString &output)
{
    const QRegularExpressionMatch uidMatch = QRegularExpression(
        QStringLiteral("UID:\\s*([0-9A-Fa-f ]+)")).match(output);
    const QRegularExpressionMatch typeMatch = QRegularExpression(
        QStringLiteral("TYPE:\\s*([^\\r\\n]+)")).match(output);

    if(!uidMatch.hasMatch() && !typeMatch.hasMatch())
        return false;

    const QString uid = uidMatch.hasMatch()
            ? uidMatch.captured(1).remove(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]"))).toUpper()
            : QString();
    m_type = typeMatch.hasMatch() ? typeMatch.captured(1).trimmed() : QString();

    int configStart = 4;
    m_pages = pagesForType(m_type, &configStart);

    const QRegularExpressionMatch cfgMatch = QRegularExpression(
        QStringLiteral("cfg0\\s*\\[\\s*(\\d+)/0x")).match(output);
    if(cfgMatch.hasMatch())
        configStart = cfgMatch.captured(1).toInt();

    const QRegularExpressionMatch memoryMatch = QRegularExpression(
        QStringLiteral("Total memory\\.{2,}\\s*(\\d+)")).match(output);
    if(memoryMatch.hasMatch())
    {
        const int pages = memoryMatch.captured(1).toInt() / 4;
        if(pages > 0)
            m_pages = pages;
    }

    m_configStart = configStart;
    m_knownLayout = m_pages > 0 && !m_type.isEmpty() &&
                    !m_type.contains(QStringLiteral("Unknown"), Qt::CaseInsensitive);

    uidLabel->setText(uid.isEmpty() ? tr("(not reported)") : uid);
    typeLabel->setText(m_type.isEmpty() ? tr("(not reported)") : m_type);
    pagesLabel->setText(m_pages > 0 ? QString::number(m_pages) : tr("unknown"));
    return true;
}

void NtagTab::populateTable()
{
    table->setRowCount(m_pages);
    for(int page = 0; page < m_pages; page++)
    {
        QString access = tr("writable");
        if(page < 4)
            access = tr("read-only (system)");
        else if(m_configStart >= 0 && page >= m_configStart)
            access = tr("read-only (config/OTP)");
        else if(!m_knownLayout)
            access = tr("read-only (unknown layout)");

        QTableWidgetItem *pageItem = new QTableWidgetItem(QString::number(page));
        pageItem->setFlags(pageItem->flags() & ~Qt::ItemIsEditable);
        QTableWidgetItem *dataItem = new QTableWidgetItem(QString());
        QTableWidgetItem *asciiItem = new QTableWidgetItem(QString());
        asciiItem->setFlags(asciiItem->flags() & ~Qt::ItemIsEditable);
        QTableWidgetItem *accessItem = new QTableWidgetItem(access);
        accessItem->setFlags(accessItem->flags() & ~Qt::ItemIsEditable);

        table->setItem(page, 0, pageItem);
        table->setItem(page, 1, dataItem);
        table->setItem(page, 2, asciiItem);
        table->setItem(page, 3, accessItem);
    }
    if(m_pages > 0)
        pageSpin->setMaximum(m_pages - 1);
    else
        pageSpin->setMaximum(255);
}

bool NtagTab::isWritable(int page) const
{
    if(!m_knownLayout || m_pages <= 0)
        return false;
    if(page < 4)
        return false;
    if(m_configStart >= 0 && page >= m_configStart)
        return false;
    return page < m_pages;
}

void NtagTab::setRowData(int page, const QString &data)
{
    if(page < 0 || page >= table->rowCount())
        return;
    table->item(page, 1)->setText(data);
    table->item(page, 2)->setText(asciiFor(data));
}

QString NtagTab::rowData(int page) const
{
    if(page < 0 || page >= table->rowCount())
        return QString();
    return normalizePageData(table->item(page, 1)->text());
}

void NtagTab::detectCard()
{
    QString key;
    if(!checkKey(&key))
    {
        setStatus(tr("Key must be 8 or 32 hex digits."), true);
        return;
    }

    QString cmd = QStringLiteral("hf mfu info");
    if(!key.isEmpty())
        cmd += QStringLiteral(" -k ") + key;
    const QString output = util->execCMDWithOutput(cmd, 20000, true);
    if(output.isEmpty() || output.contains(QStringLiteral("not available"), Qt::CaseInsensitive))
    {
        m_pages = 0;
        m_knownLayout = false;
        table->setRowCount(0);
        uidLabel->setText(QStringLiteral("-"));
        typeLabel->setText(tr("Not detected"));
        pagesLabel->setText(QStringLiteral("-"));
        setStatus(tr("No response from `hf mfu info`. No NTAG/Ultralight tag was detected."), true);
        return;
    }

    if(!parseInfo(output))
    {
        m_pages = 0;
        m_knownLayout = false;
        table->setRowCount(0);
        setStatus(tr("Tag information did not contain UID/TYPE. See the RawCommand output for details."), true);
        return;
    }

    populateTable();
    if(m_pages <= 0)
    {
        setStatus(tr("Card type is unknown. Page count and write access are not assumed; use Read Page at your own risk."), true);
    }
    else if(m_knownLayout)
    {
        setStatus(tr("Detected %1 pages. Writable pages: 4..%2. Config/OTP pages are read-only.")
                  .arg(m_pages).arg(m_configStart > 0 ? m_configStart - 1 : m_pages - 1));
    }
    else
    {
        setStatus(tr("Recognized type but layout is not enabled for writing. Read-only mode."), true);
    }
}

bool NtagTab::readPageData(int page, QString *data)
{
    if(page < 0)
        return false;
    if(m_pages > 0 && page >= m_pages)
        return false;

    QString key;
    if(!checkKey(&key))
    {
        setStatus(tr("Key must be 8 or 32 hex digits."), true);
        return false;
    }

    QString cmd = QStringLiteral("hf mfu rdbl -b %1").arg(page);
    if(!key.isEmpty())
        cmd += QStringLiteral(" -k ") + key;
    const QString output = util->execCMDWithOutput(cmd, 8000, true);
    const QRegularExpressionMatch match = QRegularExpression(
        QString::fromLatin1(kPageDataPattern)).match(output);
    if(!match.hasMatch())
    {
        setStatus(tr("Failed to parse page %1 data. See the RawCommand output.").arg(page), true);
        return false;
    }
    const QString value = normalizePageData(match.captured(2));
    if(value.isEmpty())
        return false;
    if(data)
        *data = value;
    return true;
}

void NtagTab::readAllPages()
{
    if(m_pages <= 0)
    {
        setStatus(tr("Detect a card first; page count is unknown."), true);
        return;
    }

    int success = 0;
    for(int page = 0; page < m_pages; page++)
    {
        QString data;
        if(readPageData(page, &data))
        {
            setRowData(page, data);
            success++;
        }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    setStatus(tr("Read %1 of %2 pages.").arg(success).arg(m_pages), success != m_pages);
}

void NtagTab::readSelectedPages()
{
    const QList<QTableWidgetItem *> selected = table->selectedItems();
    QSet<int> pages;
    for(QTableWidgetItem *item : selected)
        pages.insert(item->row());
    if(pages.isEmpty())
    {
        setStatus(tr("Select one or more page rows first."), true);
        return;
    }

    int success = 0;
    for(int page : pages)
    {
        QString data;
        if(readPageData(page, &data))
        {
            setRowData(page, data);
            success++;
        }
    }
    setStatus(tr("Read %1 of %2 selected pages.").arg(success).arg(pages.size()), success != pages.size());
}

void NtagTab::readOnePage()
{
    const int page = pageSpin->value();
    QString data;
    if(!readPageData(page, &data))
        return;
    setRowData(page, data);
    setStatus(tr("Page %1 read.").arg(page));
}

bool NtagTab::writePageData(int page, const QString &data, bool confirm)
{
    const QString value = normalizePageData(data);
    if(value.isEmpty())
    {
        setStatus(tr("Page data must be 8 hex digits."), true);
        return false;
    }
    if(!isWritable(page))
    {
        setStatus(tr("Page %1 is not writable under the detected layout.").arg(page), true);
        return false;
    }

    QString key;
    if(!checkKey(&key))
    {
        setStatus(tr("Key must be 8 or 32 hex digits."), true);
        return false;
    }

    if(confirm)
    {
        const QMessageBox::StandardButton choice = QMessageBox::warning(
            this, tr("Write NTAG/Ultralight page"),
            tr("Write %1 to page %2?\nThis is an experimental operation.").arg(value).arg(page),
            QMessageBox::Yes | QMessageBox::No);
        if(choice != QMessageBox::Yes)
            return false;
    }

    QString cmd = QStringLiteral("hf mfu wrbl -b %1 -d %2").arg(page).arg(value);
    if(!key.isEmpty())
        cmd += QStringLiteral(" -k ") + key;
    const QString output = util->execCMDWithOutput(cmd, 10000, true);
    if(output.isEmpty() || output.contains(QStringLiteral("fail"), Qt::CaseInsensitive) ||
       output.contains(QStringLiteral("error"), Qt::CaseInsensitive) ||
       output.contains(QStringLiteral("not available"), Qt::CaseInsensitive))
    {
        setStatus(tr("Write to page %1 failed. See the RawCommand output.").arg(page), true);
        return false;
    }

    QString verify;
    if(!readPageData(page, &verify))
        return false;
    setRowData(page, verify);
    if(verify != value)
    {
        setStatus(tr("Page %1 write returned success but verification differs (wrote %2, read %3).")
                  .arg(page).arg(value, verify), true);
        return false;
    }
    return true;
}

void NtagTab::writeOnePage()
{
    const int page = pageSpin->value();
    if(writePageData(page, dataEdit->text(), true))
        setStatus(tr("Page %1 written and verified.").arg(page));
}

void NtagTab::writeSelectedPages()
{
    const QList<QTableWidgetItem *> selected = table->selectedItems();
    QSet<int> pages;
    for(QTableWidgetItem *item : selected)
    {
        if(isWritable(item->row()))
            pages.insert(item->row());
    }
    if(pages.isEmpty())
    {
        setStatus(tr("Select one or more writable page rows first."), true);
        return;
    }

    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this, tr("Write selected NTAG/Ultralight pages"),
        tr("This will write %1 page(s).\nThis is an experimental operation. Continue?").arg(pages.size()),
        QMessageBox::Yes | QMessageBox::No);
    if(choice != QMessageBox::Yes)
        return;

    int success = 0;
    for(int page : pages)
    {
        if(writePageData(page, rowData(page), false))
            success++;
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    setStatus(tr("Wrote and verified %1 of %2 pages.").arg(success).arg(pages.size()), success != pages.size());
}

void NtagTab::verifyOnePage()
{
    const int page = pageSpin->value();
    QString data;
    if(!readPageData(page, &data))
        return;
    const QString expected = rowData(page);
    setRowData(page, data);
    if(expected.isEmpty())
    {
        setStatus(tr("Page %1 read; no previous value was stored to compare.").arg(page));
        return;
    }
    setStatus(expected == data
              ? tr("Page %1 verification OK.").arg(page)
              : tr("Page %1 verification mismatch: expected %2, read %3.").arg(page).arg(expected, data),
              expected != data);
}
