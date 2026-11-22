#include "ntagtab.h"

#include "common/util.h"
#include "core/clientsession.h"
#include "core/logger.h"
#include "ntag/ndefbuilder.h"

#include <QCheckBox>
#include <QClipboard>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QSplitter>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>

namespace {
const char *kPageDataPattern = "(\\d+)/0x[0-9A-Fa-f]+\\s*\\|\\s*((?:[0-9A-Fa-f]{2}\\s*){4})";

bool isDarkTheme()
{
    static const bool dark = []() {
        QSettings settings(QStringLiteral("GUIsettings.ini"), QSettings::IniFormat);
        const QString theme = settings.value(QStringLiteral("UI/Theme_Name")).toString();
        if(theme == QLatin1String("qdss_dark"))
            return true;
        if(theme == QLatin1String("qdss_light"))
            return false;
        return QApplication::palette().color(QPalette::Window).lightness() < 128;
    }();
    return dark;
}
}

NtagTab::NtagTab(Util *util, ClientSession *session, QWidget *parent)
    : QWidget(parent), util(util), session(session)
{
    buildUi();
    if(session)
    {
        connect(session, &ClientSession::commandFinished,
                this, &NtagTab::onReadCommandFinished);
        connect(session, &ClientSession::commandFinished,
                this, &NtagTab::onWriteCommandFinished);
        connect(session, &ClientSession::newOutput,
                this, &NtagTab::onSimulationOutput);
        connect(session, &ClientSession::detachedChanged,
                this, &NtagTab::onSimulationDetachedChanged);
    }
}

NtagTab::~NtagTab()
{
    m_readJob.finish();
    m_writeJob.finish();
    m_dumpWorkspace.clearDump();
}

void NtagTab::buildUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QGroupBox *cardGroup = new QGroupBox(tr("Card"), this);
    QGridLayout *cardLayout = new QGridLayout(cardGroup);

    QPushButton *detectButton = new QPushButton(tr("Detect / Read Info"), cardGroup);
    detectButton->setObjectName(QStringLiteral("NTAG_detectButton"));
    connect(detectButton, &QPushButton::clicked, this, &NtagTab::detectCard);
    cardLayout->addWidget(detectButton, 0, 0, 1, 2);

    cardLayout->addWidget(new QLabel(tr("UID:"), cardGroup), 1, 0);
    uidLabel = new QLabel(QStringLiteral("-"), cardGroup);
    uidLabel->setObjectName(QStringLiteral("NTAG_uidLabel"));
    uidLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cardLayout->addWidget(uidLabel, 1, 1);

    cardLayout->addWidget(new QLabel(tr("Type:"), cardGroup), 2, 0);
    typeLabel = new QLabel(tr("Not detected"), cardGroup);
    typeLabel->setObjectName(QStringLiteral("NTAG_typeLabel"));
    typeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cardLayout->addWidget(typeLabel, 2, 1);

    cardLayout->addWidget(new QLabel(tr("Pages:"), cardGroup), 3, 0);
    pagesLabel = new QLabel(QStringLiteral("-"), cardGroup);
    pagesLabel->setObjectName(QStringLiteral("NTAG_pagesLabel"));
    cardLayout->addWidget(pagesLabel, 3, 1);

    cardLayout->addWidget(new QLabel(tr("Model:"), cardGroup), 4, 0);
    modelLabel = new QLabel(tr("Not detected"), cardGroup);
    modelLabel->setObjectName(QStringLiteral("NTAG_modelLabel"));
    modelLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cardLayout->addWidget(modelLabel, 4, 1);

    cardLayout->addWidget(new QLabel(tr("GET_VERSION:"), cardGroup), 5, 0);
    versionLabel = new QLabel(QStringLiteral("-"), cardGroup);
    versionLabel->setObjectName(QStringLiteral("NTAG_versionLabel"));
    versionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cardLayout->addWidget(versionLabel, 5, 1);

    cardLayout->addWidget(new QLabel(tr("Capability Container:"), cardGroup), 6, 0);
    ccLabel = new QLabel(QStringLiteral("-"), cardGroup);
    ccLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cardLayout->addWidget(ccLabel, 6, 1);

    cardLayout->addWidget(new QLabel(tr("Capacity:"), cardGroup), 7, 0);
    capacityLabel = new QLabel(QStringLiteral("-"), cardGroup);
    capacityLabel->setObjectName(QStringLiteral("NTAG_capacityLabel"));
    cardLayout->addWidget(capacityLabel, 7, 1);

    cardLayout->addWidget(new QLabel(tr("Key (optional):"), cardGroup), 14, 0);
    keyEdit = new QLineEdit(cardGroup);
    keyEdit->setPlaceholderText(tr("UL-C/AES 16 bytes or NTAG/EV1 4 bytes, hex"));
    cardLayout->addWidget(keyEdit, 14, 1);

    cardLayout->addWidget(new QLabel(tr("ATQA:"), cardGroup), 9, 0);
    atqaLabel = new QLabel(QStringLiteral("-"), cardGroup);
    atqaLabel->setObjectName(QStringLiteral("NTAG_atqaLabel"));
    atqaLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cardLayout->addWidget(atqaLabel, 9, 1);
    cardLayout->addWidget(new QLabel(tr("SAK:"), cardGroup), 10, 0);
    sakLabel = new QLabel(QStringLiteral("-"), cardGroup);
    sakLabel->setObjectName(QStringLiteral("NTAG_sakLabel"));
    sakLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    cardLayout->addWidget(sakLabel, 10, 1);
    cardLayout->addWidget(new QLabel(tr("Pages read:"), cardGroup), 11, 0);
    readCountLabel = new QLabel(QStringLiteral("-"), cardGroup);
    readCountLabel->setObjectName(QStringLiteral("NTAG_readCountLabel"));
    cardLayout->addWidget(readCountLabel, 11, 1);
    cardLayout->addWidget(new QLabel(tr("Writable pages:"), cardGroup), 12, 0);
    writableCountLabel = new QLabel(QStringLiteral("-"), cardGroup);
    writableCountLabel->setObjectName(QStringLiteral("NTAG_writableCountLabel"));
    cardLayout->addWidget(writableCountLabel, 12, 1);
    cardLayout->addWidget(new QLabel(tr("Lock state:"), cardGroup), 13, 0);
    lockStateLabel = new QLabel(QStringLiteral("-"), cardGroup);
    lockStateLabel->setObjectName(QStringLiteral("NTAG_lockStateLabel"));
    lockStateLabel->setWordWrap(true);
    cardLayout->addWidget(lockStateLabel, 13, 1);
    mainLayout->addWidget(cardGroup);

    QGroupBox *pagesGroup = new QGroupBox(tr("Pages"), this);
    QVBoxLayout *pagesLayout = new QVBoxLayout(pagesGroup);

    table = new QTableWidget(0, 9, pagesGroup);
    table->setObjectName(QStringLiteral("NTAG_table"));
    table->setHorizontalHeaderLabels({tr("Page"), tr("B0"), tr("B1"), tr("B2"),
                                      tr("B3"), tr("ASCII"), tr("Page type"),
                                      tr("Access"), tr("State")});
    table->verticalHeader()->setVisible(false);
    // Fixed/interactive widths: ResizeToContents recalculates every item in
    // the table after each cell update and made 32-page updates take seconds.
    table->horizontalHeader()->setStretchLastSection(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table->setColumnWidth(0, 55);
    for(int column = 1; column <= 4; column++)
    {
        table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Interactive);
        table->setColumnWidth(column, 44);
    }
    table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Interactive);
    table->setColumnWidth(6, 120);
    table->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Interactive);
    table->setColumnWidth(7, 150);
    table->horizontalHeader()->setSectionResizeMode(8, QHeaderView::Interactive);
    table->setColumnWidth(8, 100);
    table->setSelectionBehavior(QAbstractItemView::SelectItems);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table->installEventFilter(this);

    QSplitter *editorSplit = new QSplitter(Qt::Horizontal, pagesGroup);
    editorSplit->addWidget(table);
    QGroupBox *detailGroup = new QGroupBox(tr("Selected page"), editorSplit);
    QVBoxLayout *detailLayout = new QVBoxLayout(detailGroup);
    QFormLayout *detailForm = new QFormLayout();
    detailPageLabel = new QLabel(QStringLiteral("-"), detailGroup);
    detailPageLabel->setObjectName(QStringLiteral("NTAG_detailPageLabel"));
    detailTypeLabel = new QLabel(QStringLiteral("-"), detailGroup);
    detailTypeLabel->setObjectName(QStringLiteral("NTAG_detailTypeLabel"));
    detailOriginalLabel = new QLabel(QStringLiteral("-"), detailGroup);
    detailOriginalLabel->setObjectName(QStringLiteral("NTAG_detailOriginalLabel"));
    detailOriginalLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailEditedLabel = new QLabel(QStringLiteral("-"), detailGroup);
    detailEditedLabel->setObjectName(QStringLiteral("NTAG_detailEditedLabel"));
    detailEditedLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailWritableLabel = new QLabel(QStringLiteral("-"), detailGroup);
    detailWritableLabel->setObjectName(QStringLiteral("NTAG_detailWritableLabel"));
    detailLockLabel = new QLabel(QStringLiteral("-"), detailGroup);
    detailLockLabel->setObjectName(QStringLiteral("NTAG_detailLockLabel"));
    detailLastWriteLabel = new QLabel(QStringLiteral("-"), detailGroup);
    detailLastWriteLabel->setObjectName(QStringLiteral("NTAG_detailLastWriteLabel"));
    detailLastReadLabel = new QLabel(QStringLiteral("-"), detailGroup);
    detailLastReadLabel->setObjectName(QStringLiteral("NTAG_detailLastReadLabel"));
    detailForm->addRow(tr("Page:"), detailPageLabel);
    detailForm->addRow(tr("Type:"), detailTypeLabel);
    detailForm->addRow(tr("Original:"), detailOriginalLabel);
    detailForm->addRow(tr("Edited:"), detailEditedLabel);
    detailForm->addRow(tr("Write access:"), detailWritableLabel);
    detailForm->addRow(tr("Lock state:"), detailLockLabel);
    detailForm->addRow(tr("Last write:"), detailLastWriteLabel);
    detailForm->addRow(tr("Last read:"), detailLastReadLabel);
    detailLayout->addLayout(detailForm);
    detailNoteLabel = new QLabel(detailGroup);
    detailNoteLabel->setWordWrap(true);
    detailNoteLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    detailLayout->addWidget(detailNoteLabel);
    showSecurityCheck = new QCheckBox(tr("Show PWD/PACK data"), detailGroup);
    showSecurityCheck->setObjectName(QStringLiteral("NTAG_showSecurityCheck"));
    connect(showSecurityCheck, &QCheckBox::toggled, this, &NtagTab::showSecurityToggled);
    detailLayout->addWidget(showSecurityCheck);
    detailLayout->addStretch();
    editorSplit->addWidget(detailGroup);
    editorSplit->setStretchFactor(0, 1);
    editorSplit->setStretchFactor(1, 0);
    pagesLayout->addWidget(editorSplit, 1);

    QHBoxLayout *readLayout = new QHBoxLayout;
    QPushButton *readAllButton = new QPushButton(tr("Read All"), pagesGroup);
    readAllButton->setObjectName(QStringLiteral("NTAG_readAllButton"));
    QPushButton *refreshButton = new QPushButton(tr("Refresh"), pagesGroup);
    refreshButton->setObjectName(QStringLiteral("NTAG_refreshButton"));
    QPushButton *readSelectedButton = new QPushButton(tr("Read Selected"), pagesGroup);
    readSelectedButton->setObjectName(QStringLiteral("NTAG_readSelectedButton"));
    QPushButton *readPageButton = new QPushButton(tr("Read Page"), pagesGroup);
    readPageButton->setObjectName(QStringLiteral("NTAG_readPageButton"));
    pageSpin = new QSpinBox(pagesGroup);
    pageSpin->setObjectName(QStringLiteral("NTAG_pageSpin"));
    pageSpin->setRange(0, 255);
    pageSpin->setPrefix(tr("Page "));
    readLayout->addWidget(readAllButton);
    readLayout->addWidget(refreshButton);
    readLayout->addWidget(readSelectedButton);
    readLayout->addWidget(pageSpin);
    readLayout->addWidget(readPageButton);
    undoButton = new QPushButton(tr("Undo"), pagesGroup);
    undoButton->setObjectName(QStringLiteral("NTAG_undoButton"));
    redoButton = new QPushButton(tr("Redo"), pagesGroup);
    redoButton->setObjectName(QStringLiteral("NTAG_redoButton"));
    discardButton = new QPushButton(tr("Discard Edits"), pagesGroup);
    discardButton->setObjectName(QStringLiteral("NTAG_discardButton"));
    exportButton = new QPushButton(tr("Export..."), pagesGroup);
    exportButton->setObjectName(QStringLiteral("NTAG_exportButton"));
    importButton = new QPushButton(tr("Import..."), pagesGroup);
    importButton->setObjectName(QStringLiteral("NTAG_importButton"));
    cancelReadButton = new QPushButton(tr("Cancel"), pagesGroup);
    cancelReadButton->setObjectName(QStringLiteral("NTAG_cancelReadButton"));
    cancelReadButton->setEnabled(false);
    connect(cancelReadButton, &QPushButton::clicked, this, &NtagTab::cancelActiveJob);
    readLayout->addWidget(undoButton);
    readLayout->addWidget(redoButton);
    readLayout->addWidget(discardButton);
    readLayout->addWidget(exportButton);
    readLayout->addWidget(importButton);
    readLayout->addWidget(cancelReadButton);
    readLayout->addStretch();
    pagesLayout->addLayout(readLayout);

    QHBoxLayout *writeLayout = new QHBoxLayout;
    dataEdit = new QLineEdit(pagesGroup);
    dataEdit->setObjectName(QStringLiteral("NTAG_dataEdit"));
    dataEdit->setPlaceholderText(tr("Page data: 8 hex digits"));
    QPushButton *writePageButton = new QPushButton(tr("Write Page"), pagesGroup);
    writePageButton->setObjectName(QStringLiteral("NTAG_writePageButton"));
    QPushButton *writeSelectedButton = new QPushButton(tr("Write Selected"), pagesGroup);
    writeSelectedButton->setObjectName(QStringLiteral("NTAG_writeSelectedButton"));
    writeDirtyButton = new QPushButton(tr("Write Dirty"), pagesGroup);
    writeDirtyButton->setObjectName(QStringLiteral("NTAG_writeDirtyButton"));
    QPushButton *verifyButton = new QPushButton(tr("Verify Page"), pagesGroup);
    verifyButton->setObjectName(QStringLiteral("NTAG_verifyButton"));
    verifyWrittenButton = new QPushButton(tr("Verify Written"), pagesGroup);
    verifyWrittenButton->setObjectName(QStringLiteral("NTAG_verifyWrittenButton"));
    writeLayout->addWidget(new QLabel(tr("Data:"), pagesGroup));
    writeLayout->addWidget(dataEdit, 1);
    writeLayout->addWidget(writePageButton);
    writeLayout->addWidget(writeSelectedButton);
    writeLayout->addWidget(writeDirtyButton);
    writeLayout->addWidget(verifyButton);
    writeLayout->addWidget(verifyWrittenButton);
    eraseButton = new QPushButton(tr("Erase Tag..."), pagesGroup);
    eraseButton->setObjectName(QStringLiteral("NTAG_eraseButton"));
    eraseButton->setToolTip(tr("Clear writable user data pages only; UID, CC, "
                               "lock, OTP, configuration and password pages are "
                               "never modified."));
    connect(eraseButton, &QPushButton::clicked, this, &NtagTab::eraseTag);
    writeLayout->addWidget(eraseButton);
    pagesLayout->addLayout(writeLayout);

    statusLabel = new QLabel(
        tr("States: Read / Modified / Written / Verified / Read failed / Not read. "
           "Double-click a byte to edit; Ctrl+C/V copies and pastes hex; Ctrl+Z/Y "
           "undoes. Only writable user pages can be changed."), pagesGroup);
    statusLabel->setObjectName(QStringLiteral("NTAG_statusLabel"));
    statusLabel->setWordWrap(true);
    pagesLayout->addWidget(statusLabel);

    QGroupBox *ndefGroup = new QGroupBox(tr("NDEF"), this);
    QVBoxLayout *ndefLayout = new QVBoxLayout(ndefGroup);
    ndefView = new QPlainTextEdit(ndefGroup);
    ndefView->setObjectName(QStringLiteral("NTAG_ndefView"));
    ndefView->setReadOnly(true);
    ndefView->setPlaceholderText(tr("Read the tag to parse CC / TLV / NDEF records."));
    ndefLayout->addWidget(ndefView);

    QGroupBox *quickGroup = new QGroupBox(tr("Quick Launch (NDEF)"), this);
    QVBoxLayout *quickLayout = new QVBoxLayout(quickGroup);
    QFormLayout *quickForm = new QFormLayout();
    quickType = new QComboBox(quickGroup);
    quickType->setObjectName(QStringLiteral("NTAG_quickType"));
    quickType->addItem(tr("Web link (https://...)"), int(Ndef::LaunchType::Url));
    quickType->addItem(tr("Phone number (tel:)"), int(Ndef::LaunchType::Phone));
    quickType->addItem(tr("SMS (sms:)"), int(Ndef::LaunchType::Sms));
    quickType->addItem(tr("E-mail (mailto:)"), int(Ndef::LaunchType::Email));
    quickType->addItem(tr("Custom URI"), int(Ndef::LaunchType::CustomUri));
    quickType->addItem(tr("Text"), int(Ndef::LaunchType::Text));
    quickType->addItem(tr("MIME"), int(Ndef::LaunchType::Mime));
    quickType->addItem(tr("vCard"), int(Ndef::LaunchType::VCard));
    quickType->addItem(tr("Wi-Fi"), int(Ndef::LaunchType::Wifi));
    quickType->addItem(tr("Bluetooth OOB"), int(Ndef::LaunchType::Bluetooth));
    quickType->addItem(tr("OpenPrintTag"), int(Ndef::LaunchType::OpenPrintTag));
    quickType->addItem(tr("External / AAR"), int(Ndef::LaunchType::External));
    quickForm->addRow(tr("Launch type:"), quickType);

    quickPrimaryLabel = new QLabel(tr("URL:"), quickGroup);
    quickPrimary = new QLineEdit(quickGroup);
    quickPrimary->setObjectName(QStringLiteral("NTAG_quickPrimary"));
    quickForm->addRow(quickPrimaryLabel, quickPrimary);

    quickSubjectLabel = new QLabel(tr("Subject:"), quickGroup);
    quickSubject = new QLineEdit(quickGroup);
    quickForm->addRow(quickSubjectLabel, quickSubject);

    quickBodyLabel = new QLabel(tr("Body:"), quickGroup);
    quickBody = new QLineEdit(quickGroup);
    quickForm->addRow(quickBodyLabel, quickBody);

    quickExtraLabel = new QLabel(tr("Extra:"), quickGroup);
    quickExtra = new QLineEdit(quickGroup);
    quickForm->addRow(quickExtraLabel, quickExtra);
    quickLayout->addLayout(quickForm);

    quickInfoLabel = new QLabel(quickGroup);
    quickInfoLabel->setWordWrap(true);
    quickInfoLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    quickLayout->addWidget(quickInfoLabel);

    quickCapacityLabel = new QLabel(tr("Detect a tag to see the NDEF capacity."), quickGroup);
    quickCapacityLabel->setObjectName(QStringLiteral("NTAG_quickCapacityLabel"));
    quickCapacityLabel->setWordWrap(true);
    quickLayout->addWidget(quickCapacityLabel);

    quickPreview = new QPlainTextEdit(quickGroup);
    quickPreview->setObjectName(QStringLiteral("NTAG_quickPreview"));
    quickPreview->setReadOnly(true);
    quickPreview->setMinimumHeight(120);
    quickPreview->setPlaceholderText(tr("The generated NDEF message and TLV appear here."));
    quickLayout->addWidget(quickPreview);

    QHBoxLayout *quickButtons = new QHBoxLayout();
    quickWriteButton = new QPushButton(tr("Preview and Write..."), quickGroup);
    quickWriteButton->setObjectName(QStringLiteral("NTAG_quickWriteButton"));
    quickWriteButton->setEnabled(false);
    connect(quickWriteButton, &QPushButton::clicked, this, &NtagTab::writeQuickLaunch);
    quickButtons->addWidget(quickWriteButton);
    quickSimButton = new QPushButton(tr("Simulate"), quickGroup);
    quickSimButton->setObjectName(QStringLiteral("NTAG_quickSimButton"));
    quickSimButton->setEnabled(false);
    quickSimButton->setToolTip(tr("Load the same NDEF bytes into the Proxmark3 emulator "
                                  "memory and start `hf mfu sim`. The tag is not written."));
    connect(quickSimButton, &QPushButton::clicked, this, &NtagTab::toggleQuickSimulation);
    quickButtons->addWidget(quickSimButton);
    quickButtons->addStretch();
    quickLayout->addLayout(quickButtons);

    quickStatusLabel = new QLabel(quickGroup);
    quickStatusLabel->setWordWrap(true);
    quickLayout->addWidget(quickStatusLabel);

    // NDEF display and Quick Launch both operate on the same NDEF area.
    // Group them under one entry; the two pages and their logic are unchanged.
    QTabWidget *ndefTabs = new QTabWidget(this);
    ndefTabs->setObjectName(QStringLiteral("NTAG_ndefTabs"));
    ndefTabs->addTab(ndefGroup, tr("NDEF"));
    // Quick Launch has many rows; a scroll area keeps every control reachable
    // at high DPI or when the dock is short.
    QScrollArea *quickScroll = new QScrollArea(ndefTabs);
    quickScroll->setObjectName(QStringLiteral("NTAG_quickScroll"));
    quickScroll->setWidgetResizable(true);
    quickScroll->setFrameShape(QFrame::NoFrame);
    quickScroll->setWidget(quickGroup);
    ndefTabs->addTab(quickScroll, tr("Quick Launch"));

    viewTabs = new QTabWidget(this);
    viewTabs->setObjectName(QStringLiteral("NTAG_viewTabs"));
    viewTabs->addTab(pagesGroup, tr("Memory"));
    viewTabs->addTab(ndefTabs, tr("NDEF / Quick Launch"));
    mainLayout->addWidget(viewTabs);

    m_busyButtons = {readAllButton, refreshButton, readSelectedButton, readPageButton,
                     undoButton, redoButton, discardButton, exportButton, importButton,
                     writePageButton, writeSelectedButton, writeDirtyButton,
                     verifyButton, verifyWrittenButton, eraseButton};
    m_busyButtons << detectButton;
    m_busyButtons << quickWriteButton;
    m_busyButtons << quickSimButton;

    connect(readAllButton, &QPushButton::clicked, this, &NtagTab::readAllPages);
    connect(refreshButton, &QPushButton::clicked, this, &NtagTab::readAllPages);
    connect(readSelectedButton, &QPushButton::clicked, this, &NtagTab::readSelectedPages);
    connect(readPageButton, &QPushButton::clicked, this, &NtagTab::readOnePage);
    connect(writePageButton, &QPushButton::clicked, this, &NtagTab::writeOnePage);
    connect(writeSelectedButton, &QPushButton::clicked, this, &NtagTab::writeSelectedPages);
    connect(writeDirtyButton, &QPushButton::clicked, this, &NtagTab::writeDirtyPages);
    connect(verifyButton, &QPushButton::clicked, this, &NtagTab::verifyOnePage);
    connect(verifyWrittenButton, &QPushButton::clicked, this, &NtagTab::verifyWritten);
    connect(undoButton, &QPushButton::clicked, this, &NtagTab::undoEdit);
    connect(redoButton, &QPushButton::clicked, this, &NtagTab::redoEdit);
    connect(discardButton, &QPushButton::clicked, this, &NtagTab::discardEdits);
    connect(exportButton, &QPushButton::clicked, this, &NtagTab::exportMemory);
    connect(importButton, &QPushButton::clicked, this, &NtagTab::importMemory);
    connect(table, &QTableWidget::itemChanged, this, &NtagTab::onTableItemChanged);
    connect(table, &QTableWidget::itemSelectionChanged,
            this, &NtagTab::updateDetailForSelection);
    connect(quickType, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &NtagTab::quickTypeChanged);
    connect(quickPrimary, &QLineEdit::textChanged, this, &NtagTab::updateQuickPreview);
    connect(quickSubject, &QLineEdit::textChanged, this, &NtagTab::updateQuickPreview);
    connect(quickBody, &QLineEdit::textChanged, this, &NtagTab::updateQuickPreview);
    connect(quickExtra, &QLineEdit::textChanged, this, &NtagTab::updateQuickPreview);
    quickTypeChanged();
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
    pagesLabel->setText(m_pages > 0
                        ? tr("%1 (4 bytes/page)").arg(m_pages)
                        : tr("unknown"));
    return true;
}

void NtagTab::populateTable()
{
    if(m_mem.pageCount() != m_pages)
        m_mem.configure(m_cap);

    m_updatingTable = true;
    table->setRowCount(m_pages);
    for(int page = 0; page < m_pages; page++)
    {
        QTableWidgetItem *pageItem = new QTableWidgetItem(QString::number(page));
        pageItem->setFlags(pageItem->flags() & ~Qt::ItemIsEditable);
        QTableWidgetItem *asciiItem = new QTableWidgetItem(QString());
        asciiItem->setFlags(asciiItem->flags() & ~Qt::ItemIsEditable);
        QTableWidgetItem *typeItem = new QTableWidgetItem(QString());
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        QTableWidgetItem *accessItem = new QTableWidgetItem(QString());
        accessItem->setFlags(accessItem->flags() & ~Qt::ItemIsEditable);
        QTableWidgetItem *stateItem = new QTableWidgetItem(QString());
        stateItem->setFlags(stateItem->flags() & ~Qt::ItemIsEditable);

        table->setItem(page, 0, pageItem);
        for(int byte = 0; byte < 4; byte++)
        {
            QTableWidgetItem *item = new QTableWidgetItem(QString());
            item->setTextAlignment(Qt::AlignCenter);
            table->setItem(page, 1 + byte, item);
        }
        table->setItem(page, 5, asciiItem);
        table->setItem(page, 6, typeItem);
        table->setItem(page, 7, accessItem);
        table->setItem(page, 8, stateItem);
        updateRow(page);
    }
    m_updatingTable = false;

    if(m_pages > 0)
        pageSpin->setMaximum(m_pages - 1);
    else
        pageSpin->setMaximum(255);
    updateCounts();
    updateLockState();
    updateDirtyButtons();
    updateDetail();
}

void NtagTab::repopulateRow(int page)
{
    m_updatingTable = true;
    updateRow(page);
    m_updatingTable = false;
    updateCounts();
    updateDirtyButtons();
    updateDetail();
}

void NtagTab::updateRow(int page)
{
    if(page < 0 || page >= table->rowCount())
        return;
    const Ntag::MemoryPage *entry = m_mem.page(page);
    if(!entry)
        return;

    const bool previousUpdating = m_updatingTable;
    m_updatingTable = true;
    // Programmatic item updates must not re-enter itemChanged().
    const QSignalBlocker blocker(table);
    const bool mask = entry->pageClass == Ntag::PageClass::Security &&
            !showSecurityCheck->isChecked();
    const bool readable = entry->readable();
    const QByteArray data = readable ? entry->edited : QByteArray();

    for(int byte = 0; byte < 4; byte++)
    {
        QTableWidgetItem *item = table->item(page, 1 + byte);
        if(!item)
            continue;
        item->setText(!readable ? QStringLiteral("--")
                                : mask ? QStringLiteral("**")
                                       : QStringLiteral("%1").arg(
                                             static_cast<quint8>(data.at(byte)),
                                             2, 16, QLatin1Char('0')).toUpper());
        if(readable && entry->writable && !mask)
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        else
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    }

    QTableWidgetItem *asciiItem = table->item(page, 5);
    if(asciiItem)
        asciiItem->setText(readable && !mask
                           ? asciiFor(QString::fromLatin1(data.toHex()))
                           : QString());
    QTableWidgetItem *typeItem = table->item(page, 6);
    if(typeItem)
    {
        typeItem->setText(pageClassText(entry->pageClass));
        typeItem->setToolTip(regionText(page));
    }
    QTableWidgetItem *accessItem = table->item(page, 7);
    if(accessItem)
        accessItem->setText(entry->writable ? tr("writable") : tr("read-only"));
    updateRowState(page);

    QColor background;
    QColor foreground;
    bool useForeground = false;
    const bool dark = isDarkTheme();
    if(dark)
    {
        // Dark theme: one base background, status through text colour and the
        // State column.  Near-white per-state fills are unreadable here.
        if(entry->state == Ntag::PageState::Failed ||
           entry->writeState == Ntag::WriteState::Failed)
        {
            foreground = QColor(255, 130, 130);
            useForeground = true;
        }
        else if(entry->dirty())
        {
            foreground = QColor(235, 205, 90);
            useForeground = true;
        }
        else if(entry->verified)
        {
            foreground = QColor(130, 220, 130);
            useForeground = true;
        }
        else if(!entry->readable())
        {
            foreground = QColor(150, 150, 150);
            useForeground = true;
        }
        else if(!entry->writable)
        {
            foreground = QColor(170, 170, 170);
            useForeground = true;
        }
    }
    else
    {
        if(entry->state == Ntag::PageState::Failed)
            background = QColor(255, 225, 225);
        else if(entry->dirty())
            background = QColor(255, 245, 205);
        else if(entry->verified)
            background = QColor(220, 245, 220);
        else if(entry->writeState == Ntag::WriteState::Failed)
            background = QColor(255, 225, 225);
        else if(!entry->writable)
            background = QColor(240, 240, 240);
    }
    for(int column = 0; column < table->columnCount(); column++)
    {
        QTableWidgetItem *item = table->item(page, column);
        if(!item)
            continue;
        if(dark)
            item->setData(Qt::BackgroundRole, QVariant());
        else if(background.isValid())
            item->setBackground(background);
        else
            item->setData(Qt::BackgroundRole, QVariant());
        if(useForeground)
            item->setForeground(foreground);
        else if(dark)
            item->setData(Qt::ForegroundRole, QVariant());
        else
            item->setForeground(QColor(25, 35, 45)); // #19232D, readable on the light fills
    }
    m_updatingTable = previousUpdating;
}

void NtagTab::updateRowState(int page)
{
    if(page < 0 || page >= table->rowCount())
        return;
    if(QTableWidgetItem *item = table->item(page, 8))
    {
        item->setText(stateText(page));
        const Ntag::MemoryPage *entry = m_mem.page(page);
        item->setToolTip(entry ? entry->error : QString());
    }
}

QString NtagTab::stateText(int page) const
{
    const Ntag::MemoryPage *entry = m_mem.page(page);
    if(!entry)
        return tr("Not read");
    if(entry->state == Ntag::PageState::Failed)
        return tr("Read failed");
    if(entry->state == Ntag::PageState::Unavailable)
        return tr("Unavailable");
    if(!entry->readable())
        return tr("Not read");
    if(entry->dirty())
        return tr("Modified");
    if(entry->verified)
        return tr("Verified");
    if(entry->writeState == Ntag::WriteState::Failed)
        return tr("Write failed");
    if(entry->writeState == Ntag::WriteState::Written)
        return tr("Written");
    return tr("Read");
}

QString NtagTab::pageClassNameFor(int page) const
{
    const Ntag::MemoryPage *entry = m_mem.page(page);
    return entry ? pageClassText(entry->pageClass) : tr("Unknown");
}

QString NtagTab::regionText(int page) const
{
    const QString region = Ntag::regionName(m_cap, page);
    if(region == QLatin1String("Manufacturer / UID0-2 / BCC0")) return tr("Manufacturer / UID0-2 / BCC0");
    if(region == QLatin1String("UID3-6")) return tr("UID3-6");
    if(region == QLatin1String("BCC1 / Internal / Lock")) return tr("BCC1 / Internal / Lock");
    if(region == QLatin1String("Capability Container")) return tr("Capability Container");
    if(region == QLatin1String("Dynamic lock")) return tr("Dynamic lock");
    if(region == QLatin1String("Configuration 0")) return tr("Configuration 0");
    if(region == QLatin1String("Configuration 1")) return tr("Configuration 1");
    if(region == QLatin1String("PWD")) return tr("PWD");
    if(region == QLatin1String("PACK")) return tr("PACK");
    if(region == QLatin1String("User data")) return tr("User data");
    if(region == QLatin1String("Vendor / reserved")) return tr("Vendor / reserved");
    if(region == QLatin1String("Out of range")) return tr("Out of range");
    return region;
}

bool NtagTab::isWritable(int page) const
{
    if(!m_knownLayout || m_pages <= 0)
        return false;
    if(m_cap.model != Ntag::Model::Unknown && m_cap.pages > 0)
        return Ntag::classifyPage(m_cap, page) == Ntag::PageClass::Writable;
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
    const QString value = normalizePageData(data);
    if(value.isEmpty())
    {
        m_mem.markReadFailed(page, tr("page data is empty"));
        updateRow(page);
        return;
    }
    QString error;
    if(!m_mem.setPageData(page, QByteArray::fromHex(value.toLatin1()), &error))
    {
        setStatus(error, true);
        return;
    }
    updateRow(page);
}

QString NtagTab::rowData(int page) const
{
    const Ntag::MemoryPage *entry = m_mem.page(page);
    if(!entry || !entry->readable())
        return QString();
    return QString::fromLatin1(entry->edited.toHex()).toUpper();
}


void NtagTab::updateCounts()
{
    int read = 0;
    int writable = 0;
    for(int page = 0; page < m_mem.pageCount(); page++)
    {
        const Ntag::MemoryPage *entry = m_mem.page(page);
        if(entry->readable())
            read++;
        if(entry->writable)
            writable++;
    }
    readCountLabel->setText(m_pages > 0
                            ? tr("%1 / %2").arg(read).arg(m_pages)
                            : QStringLiteral("-"));
    writableCountLabel->setText(m_pages > 0 ? QString::number(writable)
                                            : QStringLiteral("-"));
}

void NtagTab::updateLockState()
{
    if(m_pages <= 0 || !m_knownLayout)
    {
        lockStateLabel->setText(QStringLiteral("-"));
        return;
    }
    QStringList parts;
    const Ntag::MemoryPage *lockPage = m_mem.page(2);
    if(lockPage && lockPage->original.size() == 4)
    {
        const QByteArray lock = lockPage->original.mid(2, 2);
        parts << tr("static %1").arg(QString::fromLatin1(lock.toHex(' ')).toUpper());
    }
    else
    {
        parts << tr("static unknown");
    }
    if(m_cap.lockPage >= 0)
    {
        const Ntag::MemoryPage *dynamic = m_mem.page(m_cap.lockPage);
        if(dynamic && dynamic->original.size() == 4)
            parts << tr("dynamic %1").arg(
                         QString::fromLatin1(dynamic->original.left(3).toHex(' ')).toUpper());
        else
            parts << tr("dynamic unknown");
    }
    lockStateLabel->setText(parts.join(QStringLiteral("; ")));
}

void NtagTab::updateDirtyButtons()
{
    const QList<int> dirty = m_mem.dirtyPages();
    if(undoButton)
        undoButton->setEnabled(!m_undoStack.isEmpty());
    if(redoButton)
        redoButton->setEnabled(!m_redoStack.isEmpty());
    if(discardButton)
        discardButton->setEnabled(!dirty.isEmpty());
    if(writeDirtyButton)
        writeDirtyButton->setEnabled(!dirty.isEmpty() && m_knownLayout);
    if(verifyWrittenButton)
        verifyWrittenButton->setEnabled(!m_lastWriteTargets.isEmpty() || !dirty.isEmpty());
}

QMap<int, QByteArray> NtagTab::editedSnapshot(const QList<int> &pages) const
{
    QMap<int, QByteArray> snapshot;
    for(int page : pages)
    {
        const Ntag::MemoryPage *entry = m_mem.page(page);
        if(entry && entry->readable())
            snapshot.insert(page, entry->edited);
    }
    return snapshot;
}

void NtagTab::snapshotEdit(const QList<int> &pages, const QMap<int, QByteArray> &before)
{
    MemoryEdit edit;
    edit.before = before;
    edit.after = editedSnapshot(pages);
    m_undoStack.append(edit);
    while(m_undoStack.size() > 50)
        m_undoStack.removeFirst();
    m_redoStack.clear();
    updateDirtyButtons();
}

void NtagTab::applyEditedSnapshot(const QMap<int, QByteArray> &snapshot)
{
    for(auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it)
    {
        Ntag::MemoryPage *entry = m_mem.page(it.key());
        if(!entry || it.value().size() != 4)
            continue;
        entry->edited = it.value();
        entry->writeState = Ntag::WriteState::None;
        entry->verified = false;
        entry->error.clear();
        updateRow(it.key());
    }
    updateCounts();
    updateDirtyButtons();
    updateDetail();
}

bool NtagTab::confirmDiscardEdits(const QString &reason)
{
    if(m_mem.dirtyPages().isEmpty())
        return true;
    const QMessageBox::StandardButton choice = QMessageBox::question(
                this, tr("Uncommitted edits"),
                tr("%1\n%2 page(s) have uncommitted edits. Discard them?")
                .arg(reason).arg(m_mem.dirtyPages().size()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if(choice != QMessageBox::Yes)
        return false;
    m_mem.revertAll();
    m_undoStack.clear();
    m_redoStack.clear();
    for(int page = 0; page < m_pages; page++)
        updateRow(page);
    updateDirtyButtons();
    updateDetail();
    return true;
}

void NtagTab::undoEdit()
{
    if(deviceBusy())
        return;
    if(m_undoStack.isEmpty())
        return;
    const MemoryEdit edit = m_undoStack.takeLast();
    applyEditedSnapshot(edit.before);
    m_redoStack.append(edit);
    setStatus(tr("Undone."));
}

void NtagTab::redoEdit()
{
    if(deviceBusy())
        return;
    if(m_redoStack.isEmpty())
        return;
    const MemoryEdit edit = m_redoStack.takeLast();
    applyEditedSnapshot(edit.after);
    m_undoStack.append(edit);
    setStatus(tr("Redone."));
}

void NtagTab::discardEdits()
{
    if(deviceBusy())
        return;
    if(!confirmDiscardEdits(tr("Discard all uncommitted edits?")))
        return;
    setStatus(tr("Uncommitted edits discarded."));
}

void NtagTab::showSecurityToggled()
{
    for(int page = 0; page < m_pages; page++)
    {
        const Ntag::MemoryPage *entry = m_mem.page(page);
        if(entry && entry->pageClass == Ntag::PageClass::Security)
            updateRow(page);
    }
    updateDetail();
}

void NtagTab::updateDetailForSelection()
{
    updateDetail();
}

void NtagTab::updateDetail()
{
    if(!detailPageLabel)
        return;
    const int page = table->currentRow();
    const Ntag::MemoryPage *entry = m_mem.page(page);
    if(!entry)
    {
        detailPageLabel->setText(QStringLiteral("-"));
        detailTypeLabel->setText(QStringLiteral("-"));
        detailOriginalLabel->setText(QStringLiteral("-"));
        detailEditedLabel->setText(QStringLiteral("-"));
        detailWritableLabel->setText(QStringLiteral("-"));
        detailLockLabel->setText(QStringLiteral("-"));
        detailLastWriteLabel->setText(QStringLiteral("-"));
        detailLastReadLabel->setText(QStringLiteral("-"));
        detailNoteLabel->clear();
        return;
    }

    const bool mask = entry->pageClass == Ntag::PageClass::Security &&
            !showSecurityCheck->isChecked();
    auto hexText = [mask](const QByteArray &data) {
        if(data.size() != 4)
            return QStringLiteral("-");
        if(mask)
            return QStringLiteral("** ** ** **");
        return QString::fromLatin1(data.toHex(' ')).toUpper();
    };

    detailPageLabel->setText(QString::number(page));
    detailTypeLabel->setText(pageClassText(entry->pageClass) +
                             QStringLiteral(" / ") + regionText(page));
    detailOriginalLabel->setText(hexText(entry->original));
    detailEditedLabel->setText(hexText(entry->edited));
    detailWritableLabel->setText(entry->writable ? tr("writable") : tr("read-only"));
    detailLockLabel->setText(entry->pageClass == Ntag::PageClass::Lock
                             ? tr("lock bytes (read-only)") : tr("not a lock page"));
    detailLastWriteLabel->setText(entry->writeState == Ntag::WriteState::Written
                                  ? tr("written")
                                  : entry->writeState == Ntag::WriteState::Failed
                                    ? tr("failed: %1").arg(entry->error)
                                    : QStringLiteral("-"));
    detailLastReadLabel->setText(entry->readable()
                                 ? tr("read")
                                 : stateText(page) +
                                   (entry->error.isEmpty()
                                    ? QString()
                                    : QStringLiteral(": ") + entry->error));

    QString note;
    switch(entry->pageClass)
    {
    case Ntag::PageClass::Manufacturer:
        note = tr("Manufacturer / UID page: never editable, never written.");
        break;
    case Ntag::PageClass::UID:
        note = tr("UID page: never editable, never written.");
        break;
    case Ntag::PageClass::Lock:
        note = tr("Lock page: protected; erase/write never touches it.");
        break;
    case Ntag::PageClass::Config:
        note = tr("Capability Container / configuration: read-only here.");
        break;
    case Ntag::PageClass::Security:
        note = tr("PWD/PACK: masked by default and never written.");
        break;
    case Ntag::PageClass::Writable:
        note = tr("Writable user page. Edits stay local until Write Selected/Dirty.");
        break;
    default:
        note = tr("Not applicable or unknown for the detected layout.");
        break;
    }
    detailNoteLabel->setText(note);
}

void NtagTab::onTableItemChanged(QTableWidgetItem *item)
{
    // updateRow() changes item text/flags, which emits itemChanged again.
    // This guard must be checked before any other branch, otherwise a read job
    // (readBusy) turns that signal into updateRow() -> itemChanged -> ... and
    // overflows the GUI stack.
    if(m_updatingTable || !item)
        return;
    if(deviceBusy())
        return;
    const int page = item->row();
    const int column = item->column();
    if(column < 1 || column > 4)
        return;
    Ntag::MemoryPage *entry = m_mem.page(page);
    if(!entry)
        return;

    const QString text = item->text().trimmed().toUpper();
    bool ok = text.size() == 2;
    const int value = text.toInt(&ok, 16);
    if(!ok)
    {
        setStatus(tr("A byte must be exactly two hex digits."), true);
        updateRow(page);
        return;
    }
    if(!entry->readable() || !entry->writable)
    {
        setStatus(tr("Page %1 is not editable.").arg(page), true);
        updateRow(page);
        return;
    }
    if(static_cast<quint8>(entry->edited.at(column - 1)) == static_cast<quint8>(value))
        return;

    const QMap<int, QByteArray> before = editedSnapshot(QList<int>({page}));
    QString error;
    if(!m_mem.editByte(page, column - 1, static_cast<quint8>(value), &error))
    {
        setStatus(error, true);
        updateRow(page);
        return;
    }
    snapshotEdit(QList<int>({page}), before);
    table->setCurrentCell(page, column);
    updateRow(page);
    updateDetail();
    setStatus(tr("Page %1 edited (not written).").arg(page));
}

void NtagTab::copySelection()
{
    QList<QTableWidgetItem *> items = table->selectedItems();
    std::sort(items.begin(), items.end(), [](QTableWidgetItem *a, QTableWidgetItem *b) {
        return a->row() == b->row() ? a->column() < b->column() : a->row() < b->row();
    });
    QStringList bytes;
    for(QTableWidgetItem *item : items)
    {
        if(item->column() < 1 || item->column() > 4)
            continue;
        const QString text = item->text();
        if(text == QLatin1String("--") || text == QLatin1String("**"))
            continue;
        bytes << text;
    }
    if(bytes.isEmpty())
    {
        setStatus(tr("Select hex byte cells to copy."), true);
        return;
    }
    QApplication::clipboard()->setText(bytes.join(QLatin1Char(' ')));
    setStatus(tr("Copied %1 byte(s).").arg(bytes.size()));
}

void NtagTab::pasteHex()
{
    if(deviceBusy())
        return;
    QByteArray bytes;
    QString error;
    if(!Ntag::parseHexBytes(QApplication::clipboard()->text(), &bytes, &error))
    {
        setStatus(tr("Clipboard is not hex data: %1").arg(error), true);
        return;
    }

    int page = table->currentRow();
    int offset = 0;
    QTableWidgetItem *current = table->currentItem();
    if(current)
    {
        page = current->row();
        if(current->column() >= 1 && current->column() <= 4)
            offset = current->column() - 1;
    }
    if(page < 0)
    {
        setStatus(tr("Select a starting page first."), true);
        return;
    }

    QList<int> pages;
    for(int i = 0; i < bytes.size(); i++)
    {
        const int target = page + (offset + i) / 4;
        if(!pages.contains(target))
            pages.append(target);
    }
    const QMap<int, QByteArray> before = editedSnapshot(pages);
    if(!m_mem.setBytes(page, offset, bytes, &error))
    {
        setStatus(tr("Paste refused: %1").arg(error), true);
        return;
    }
    snapshotEdit(pages, before);
    for(int target : pages)
        updateRow(target);
    updateDetail();
    setStatus(tr("Pasted %1 byte(s). Not written yet.").arg(bytes.size()));
}

void NtagTab::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if(event->type() == QEvent::PaletteChange ||
       event->type() == QEvent::StyleChange)
    {
        for(int page = 0; page < m_pages; page++)
            updateRow(page);
    }
}

bool NtagTab::eventFilter(QObject *watched, QEvent *event)
{
    if(watched == table && event->type() == QEvent::KeyPress)
    {
        QKeyEvent *key = static_cast<QKeyEvent *>(event);
        if(key->matches(QKeySequence::Copy))
        {
            copySelection();
            return true;
        }
        if(key->matches(QKeySequence::Paste))
        {
            pasteHex();
            return true;
        }
        if(key->matches(QKeySequence::Undo))
        {
            undoEdit();
            return true;
        }
        if(key->matches(QKeySequence::Redo))
        {
            redoEdit();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QByteArray NtagTab::dumpVersion() const
{
    if(m_version.raw.size() >= 7)
        return m_version.raw;
    switch(m_cap.model)
    {
    case Ntag::Model::NTAG213: return QByteArray::fromHex("0004040201000F03");
    case Ntag::Model::NTAG215: return QByteArray::fromHex("0004040201001103");
    case Ntag::Model::NTAG216: return QByteArray::fromHex("0004040201001303");
    default: return QByteArray(8, '\0');
    }
}

bool NtagTab::loadDumpIntoMemory(const QByteArray &file)
{
    QList<QByteArray> pages;
    const Ntag::Dump dump = Ntag::parseDump(file);
    if(dump.valid && !dump.pages.isEmpty())
    {
        pages = dump.pages;
    }
    else if(file.size() % 4 == 0 && !file.isEmpty())
    {
        for(int offset = 0; offset < file.size(); offset += 4)
            pages.append(file.mid(offset, 4));
    }
    else
    {
        setStatus(tr("Import failed: not a dump file and not a multiple of 4 bytes."), true);
        return false;
    }

    QList<int> targets;
    int skipped = 0;
    for(int page = 0; page < pages.size() && page < m_pages; page++)
    {
        const Ntag::MemoryPage *entry = m_mem.page(page);
        if(!entry || !entry->readable() || !entry->writable)
        {
            skipped++;
            continue;
        }
        targets.append(page);
    }
    if(targets.isEmpty())
    {
        setStatus(tr("Import failed: no writable, already-read pages matched."), true);
        return false;
    }

    const QMap<int, QByteArray> before = editedSnapshot(targets);
    for(int page : targets)
    {
        Ntag::MemoryPage *entry = m_mem.page(page);
        entry->edited = pages.at(page);
        entry->writeState = Ntag::WriteState::None;
        entry->verified = false;
        entry->error.clear();
    }
    snapshotEdit(targets, before);
    for(int page : targets)
        updateRow(page);
    updateDetail();
    setStatus(tr("Imported %1 page(s); skipped %2 read-only/out-of-range page(s). "
                 "Nothing was written to the tag.").arg(targets.size()).arg(skipped));
    return true;
}

void NtagTab::importMemory()
{
    if(m_pages <= 0 || m_mem.readablePages().isEmpty())
    {
        setStatus(tr("Detect and read the tag before importing a memory image."), true);
        return;
    }
    if(!m_mem.dirtyPages().isEmpty())
    {
        const QMessageBox::StandardButton choice = QMessageBox::question(
                    this, tr("Import memory image"),
                    tr("Importing replaces the edited values of matching pages. "
                       "Continue with the current edits?"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if(choice != QMessageBox::Yes)
            return;
    }
    const QString path = QFileDialog::getOpenFileName(
                this, tr("Import memory image"), QDir::homePath(),
                tr("Dump files (*.bin *.dump *.json);;All files (*)"));
    if(path.isEmpty())
        return;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        setStatus(tr("Cannot open %1").arg(path), true);
        return;
    }
    loadDumpIntoMemory(file.readAll());
}

void NtagTab::exportMemory()
{
    if(m_pages <= 0 || m_mem.readablePages().size() != m_pages)
    {
        setStatus(tr("Read all pages before exporting an image."), true);
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
                this, tr("Export NTAG memory image"),
                QDir::homePath() + QStringLiteral("/ntag-memory.bin"),
                tr("PM3 dump (*.bin)"));
    if(path.isEmpty())
        return;
    QList<QByteArray> pages;
    for(int page = 0; page < m_pages; page++)
        pages.append(m_mem.page(page)->edited);
    const QByteArray dump = Ntag::buildDump(dumpVersion(), pages);
    if(dump.isEmpty())
    {
        setStatus(tr("Cannot build the dump file."), true);
        return;
    }
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(dump) != dump.size())
    {
        setStatus(tr("Cannot write %1").arg(path), true);
        return;
    }
    file.close();
    setStatus(tr("Exported %1 bytes to %2 (edited values, not read back).")
              .arg(dump.size()).arg(path));
}

void NtagTab::detectCard()
{
    if(deviceBusy())
    {
        setStatus(tr("Another NTAG read/write operation is running."), true);
        return;
    }
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
        m_cap = Ntag::Capability();
        m_version = Ntag::Version();
        m_cc = Ntag::CC();
        m_mem.clear();
        m_undoStack.clear();
        m_redoStack.clear();
        m_lastWriteTargets.clear();
        table->setRowCount(0);
        uidLabel->setText(QStringLiteral("-"));
        typeLabel->setText(tr("Not detected"));
        pagesLabel->setText(QStringLiteral("-"));
        modelLabel->setText(tr("Not detected"));
        versionLabel->setText(QStringLiteral("-"));
        ccLabel->setText(QStringLiteral("-"));
        capacityLabel->setText(QStringLiteral("-"));
        atqaLabel->setText(QStringLiteral("-"));
        sakLabel->setText(QStringLiteral("-"));
        readCountLabel->setText(QStringLiteral("-"));
        writableCountLabel->setText(QStringLiteral("-"));
        lockStateLabel->setText(QStringLiteral("-"));
        setStatus(tr("No response from `hf mfu info`. No NTAG/Ultralight tag was detected."), true);
        return;
    }

    if(!parseInfo(output))
    {
        m_pages = 0;
        m_knownLayout = false;
        m_mem.clear();
        m_undoStack.clear();
        m_redoStack.clear();
        m_lastWriteTargets.clear();
        table->setRowCount(0);
        atqaLabel->setText(QStringLiteral("-"));
        sakLabel->setText(QStringLiteral("-"));
        setStatus(tr("Tag information did not contain UID/TYPE. See the RawCommand output for details."), true);
        return;
    }

    const QRegularExpressionMatch atqaMatch = QRegularExpression(
                QStringLiteral("ATQA:\\s*((?:[0-9A-Fa-f]{2}\\s*)+)")).match(output);
    atqaLabel->setText(atqaMatch.hasMatch()
                       ? atqaMatch.captured(1).trimmed().toUpper()
                       : tr("(not reported)"));
    const QRegularExpressionMatch sakMatch = QRegularExpression(
                QStringLiteral("SAK:\\s*([0-9A-Fa-f]{2})")).match(output);
    sakLabel->setText(sakMatch.hasMatch() ? sakMatch.captured(1).toUpper()
                                          : tr("(not reported)"));

    // `hf mfu info` does not print ATQA/SAK on this client.  A second reader
    // command also confirms that the same tag is still on the antenna.
    const QString isoOutput = util->execCMDWithOutput(
                QStringLiteral("hf 14a info"), 10000, true);
    const QRegularExpressionMatch isoAtqa = QRegularExpression(
                QStringLiteral("ATQA:\\s*((?:[0-9A-Fa-f]{2}\\s*)+)")).match(isoOutput);
    const QRegularExpressionMatch isoSak = QRegularExpression(
                QStringLiteral("SAK:\\s*([0-9A-Fa-f]{2})")).match(isoOutput);
    if(isoAtqa.hasMatch())
        atqaLabel->setText(isoAtqa.captured(1).trimmed().toUpper());
    if(isoSak.hasMatch())
        sakLabel->setText(isoSak.captured(1).toUpper());
    const QRegularExpressionMatch isoUid = QRegularExpression(
                QStringLiteral("UID:\\s*([0-9A-Fa-f ]+)")).match(isoOutput);
    if(isoUid.hasMatch())
    {
        const QString uid = isoUid.captured(1).remove(
                    QRegularExpression(QStringLiteral("[^0-9A-Fa-f]"))).toUpper();
        const QString firstUid = uidLabel->text().remove(
                    QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")));
        if(!uid.isEmpty() && !firstUid.isEmpty() && uid != firstUid)
        {
            m_pages = 0;
            m_knownLayout = false;
            m_mem.clear();
            table->setRowCount(0);
            setStatus(tr("The tag UID changed between identification steps; "
                         "nothing was read or written."), true);
            return;
        }
    }

    // GET_VERSION (0x60) refines the model even when the client reports
    // "NTAG UNKNOWN" for compatible/clone chips.
    QByteArray versionRaw;
    const QString versionOut = util->execCMDWithOutput(
                QStringLiteral("hf 14a apdu -k -d 6000000000"), 8000, true);
    const QRegularExpressionMatch versionMatch = QRegularExpression(
                QStringLiteral("<<<\\s*([0-9A-Fa-f]+)")).match(versionOut);
    if(versionMatch.hasMatch())
        versionRaw = QByteArray::fromHex(versionMatch.captured(1).toLatin1());
    m_version = Ntag::parseVersion(versionRaw);
    versionLabel->setText(versionRaw.isEmpty()
                          ? tr("(not supported)")
                          : QString::fromLatin1(versionRaw.toHex(' ')).toUpper());

    QByteArray ccRaw;
    QRegularExpressionMatch ccMatch = QRegularExpression(
                QStringLiteral("Capability Container:\\s*([0-9A-Fa-f]{8})")).match(output);
    if(!ccMatch.hasMatch())
        ccMatch = QRegularExpression(
                QStringLiteral("OTP:\\s*((?:[0-9A-Fa-f]{2}\\s*){4})")).match(output);
    if(ccMatch.hasMatch())
    {
        QString hex = ccMatch.captured(1);
        hex.remove(QRegularExpression(QStringLiteral("[^0-9A-Fa-f]")));
        if(hex.size() >= 8)
            ccRaw = QByteArray::fromHex(hex.left(8).toLatin1());
    }
    m_cc = Ntag::parseCC(ccRaw);
    ccLabel->setText(ccRaw.isEmpty()
                     ? tr("(not reported)")
                     : QStringLiteral("%1  -  %2 B")
                       .arg(QString::fromLatin1(ccRaw.toHex(' ')).toUpper())
                       .arg(m_cc.ndefCapacity));

    m_cap = Ntag::identify(m_version, m_cc, m_type);
    if(m_cap.pages > 0)
    {
        m_pages = m_cap.pages;
        m_configStart = m_cap.cfg0;
        m_knownLayout = true;
    }
    pagesLabel->setText(m_pages > 0
                        ? tr("%1 (4 bytes/page)").arg(m_pages)
                        : tr("unknown"));
    m_mem.clear();
    m_undoStack.clear();
    m_redoStack.clear();
    m_lastWriteTargets.clear();
    modelLabel->setText(QStringLiteral("%1 (%2)")
                        .arg(Ntag::modelName(m_cap.model),
                             Ntag::confidenceName(m_cap.confidence)));
    modelLabel->setToolTip(m_cap.basis);
    if(m_pages > 0)
    {
        const int userFirst = 4;
        const int userLast = m_cap.userLast >= 0 ? m_cap.userLast : m_pages - 1;
        capacityLabel->setText(tr("%1 pages (0..%2), user %3..%4, %5 B")
                               .arg(m_pages).arg(m_pages - 1)
                               .arg(userFirst).arg(userLast)
                               .arg((userLast - userFirst + 1) * 4));
    }
    else
    {
        capacityLabel->setText(tr("unknown"));
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

QString NtagTab::pageForceSuffix(int page)
{
    // The client autodetects a maximum block from the tag type.  Clones and
    // tags without GET_VERSION are often misdetected as a 41-page tag even
    // when the CC/GET_VERSION layout says NTAG215/216.  `--force` skips only
    // that client-side address check; the command still targets the requested
    // physical page, and the GUI has already checked the layout.
    return page > 41 ? QStringLiteral(" --force") : QString();
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
    cmd += pageForceSuffix(page);
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

QString NtagTab::dumpPath() const
{
    // Work files live in a per-instance temporary directory.  The client
    // writes a .json sidecar next to every dump, so a fixed path inside the
    // application directory accumulated one JSON per read/write forever.
    return m_dumpWorkspace.path();
}

bool NtagTab::readAllFromDump()
{
    const int chunk = 32;
    int readPages = 0;
    bool anySuccess = false;

    for(int start = 0; start < m_pages; start += chunk)
    {
        const int count = qMin(chunk, m_pages - start);
        setStatus(tr("Reading pages %1..%2 of %3...")
                  .arg(start).arg(start + count - 1).arg(m_pages - 1));
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        const QString path = dumpPath();
        m_dumpWorkspace.clearDump();
        QString cmd = QStringLiteral("hf mfu dump -p %1 -q %2 -f \"%3\"")
                .arg(start).arg(count).arg(path);
        QString key;
        if(checkKey(&key) && !key.isEmpty())
            cmd += QStringLiteral(" -k ") + key;
        util->execCMDWithOutput(cmd, 60000, true);

        QFile file(path);
        if(!file.exists() || !file.open(QIODevice::ReadOnly))
        {
            m_dumpWorkspace.clearDump();
            for(int page = start; page < start + count; page++)
            {
                m_mem.markReadFailed(page, tr("read failed"));
                updateRow(page);
            }
            continue;
        }
        const QByteArray bytes = file.readAll();
        file.close();
        m_dumpWorkspace.clearDump();

        const Ntag::Dump dump = Ntag::parseDump(bytes);
        if(!dump.valid)
        {
            m_dumpWorkspace.clearDump();
            for(int page = start; page < start + count; page++)
            {
                m_mem.markReadFailed(page, tr("read failed"));
                updateRow(page);
            }
            continue;
        }
        if(!m_version.valid)
        {
            m_version = Ntag::parseVersion(dump.versionRaw);
            if(m_version.valid)
                versionLabel->setText(QString::fromLatin1(m_version.raw.toHex(' ')).toUpper());
        }
        for(int i = 0; i < count; i++)
        {
            const int page = start + i;
            if(i < dump.pages.size())
            {
                setRowData(page, QString::fromLatin1(dump.pages.at(i).toHex(' ')).toUpper());
                readPages++;
                anySuccess = true;
            }
            else
            {
                m_mem.markReadFailed(page, tr("read failed"));
                updateRow(page);
            }
        }
    }

    setStatus(tr("Dump read %1 of %2 pages.").arg(readPages).arg(m_pages),
              readPages != m_pages);
    refreshCcFromTable();
    updateCounts();
    updateLockState();
    updateDirtyButtons();
    updateDetail();
    return anySuccess;
}

void NtagTab::fillNdef()
{
    if(!ndefView)
        return;
    QStringList lines;
    if(m_cc.valid)
    {
        lines << tr("Capability Container: %1").arg(
                     QString::fromLatin1(m_cc.raw.toHex(' ')).toUpper());
        lines << tr("  NDEF capacity: %1 bytes").arg(m_cc.ndefCapacity);
        lines << tr("  Access: %1").arg(m_cc.readWrite ? tr("read/write") : tr("read-only"));
        lines << tr("  Multiple block read: %1").arg(m_cc.multiBlock ? tr("supported") : tr("not supported"));
    }
    else
    {
        lines << tr("No valid Capability Container was read.");
    }

    const int first = 4;
    int last = (m_cap.userLast >= 0) ? m_cap.userLast : m_pages - 1;
    if(m_cc.valid && m_cc.ndefCapacity > 0)
        last = qMin(last, first + (m_cc.ndefCapacity + 3) / 4 - 1);

    QByteArray area;
    bool complete = true;
    for(int page = first; page <= last && page < m_pages; page++)
    {
        const Ntag::MemoryPage *entry = m_mem.page(page);
        if(entry && entry->readable())
            area += entry->original;
        else
            complete = false;
    }

    if(area.isEmpty())
    {
        lines << tr("NDEF: no user data was read.");
        ndefView->setPlainText(lines.join(QLatin1Char('\n')));
        return;
    }
    if(!complete)
        lines << tr("NDEF: some user pages are missing; parsing may be incomplete.");

    const Ntag::NdefResult result = Ntag::parseNdef(area);
    if(result.ok)
    {
        lines << tr("NDEF message: %1 bytes, %2 record(s)")
                 .arg(result.message.size()).arg(result.records.size());
        int index = 1;
        for(const Ntag::NdefRecord &record : result.records)
        {
            lines << QStringLiteral("%1. %2  TNF %3  type '%4'")
                     .arg(index++).arg(record.typeName, record.tnf, record.type);
            if(!record.uri.isEmpty())
                lines << QStringLiteral("   URI: %1").arg(record.uri);
            else if(!record.text.isEmpty())
                lines << QStringLiteral("   Text: %1").arg(record.text);
            if(!record.mime.isEmpty())
                lines << QStringLiteral("   MIME: %1").arg(record.mime);
            if(!record.payload.isEmpty())
                lines << QStringLiteral("   Payload: %1")
                         .arg(QString::fromLatin1(record.payload.left(64).toHex(' ')).toUpper());
        }
    }
    else
    {
        lines << tr("NDEF parse failed: %1").arg(result.error);
        lines << tr("Raw user data: %1")
                 .arg(QString::fromLatin1(area.left(64).toHex(' ')).toUpper());
    }
    ndefView->setPlainText(lines.join(QLatin1Char('\n')));
}

int NtagTab::availableNdefCapacity() const
{
    if(m_cc.valid && m_cc.ndefCapacity > 0)
        return m_cc.ndefCapacity;
    if(m_cap.pages > 0 && m_cap.userLast >= m_cap.userFirst)
        return qMax(0, (m_cap.userLast - m_cap.userFirst + 1) * 4 - 4);
    return 0;
}

QString NtagTab::pageClassText(Ntag::PageClass pageClass) const
{
    switch(pageClass)
    {
    case Ntag::PageClass::Writable: return tr("Writable");
    case Ntag::PageClass::ReadOnly: return tr("ReadOnly");
    case Ntag::PageClass::Manufacturer: return tr("Manufacturer");
    case Ntag::PageClass::UID: return tr("UID");
    case Ntag::PageClass::Lock: return tr("Lock");
    case Ntag::PageClass::Config: return tr("Config");
    case Ntag::PageClass::Security: return tr("Security");
    case Ntag::PageClass::NotApplicable: return tr("NotApplicable");
    default: return tr("Unknown");
    }
}

QString NtagTab::verdictText(Ntag::PageVerdict verdict) const
{
    switch(verdict)
    {
    case Ntag::PageVerdict::Match: return tr("match");
    case Ntag::PageVerdict::Mismatch: return tr("MISMATCH");
    case Ntag::PageVerdict::Preserved: return tr("preserved");
    case Ntag::PageVerdict::Unchecked: return tr("not checkable");
    default: return tr("not applicable");
    }
}

void NtagTab::refreshCcFromTable()
{
    if(m_pages <= 3 || table->rowCount() <= 3)
        return;
    const QString hex = rowData(3);
    if(hex.size() != 8)
    {
        // Page 3 could not be read: the NDEF area is no longer trustworthy.
        m_cc = Ntag::CC();
        ccLabel->setText(tr("(not read)"));
        return;
    }
    const QByteArray raw = QByteArray::fromHex(hex.toLatin1());
    m_cc = Ntag::parseCC(raw);
    ccLabel->setText(QStringLiteral("%1  -  %2 B")
                     .arg(QString::fromLatin1(raw.toHex(' ')).toUpper())
                     .arg(m_cc.ndefCapacity));
}

bool NtagTab::readPagesForVerify(QList<QByteArray> *pages, QVector<Ntag::PageState> *states){
    pages->clear();
    states->clear();
    if(m_pages <= 0)
        return false;
    for(int i = 0; i < m_pages; i++)
        pages->append(QByteArray(4, '\0'));
    states->fill(Ntag::PageState::Unknown, m_pages);

    const QString path = dumpPath();
    m_dumpWorkspace.clearDump();
    QString cmd = QStringLiteral("hf mfu dump -p 0 -q %1 -f \"%2\"").arg(m_pages).arg(path);
    QString key;
    if(checkKey(&key) && !key.isEmpty())
        cmd += QStringLiteral(" -k ") + key;
    util->execCMDWithOutput(cmd, 120000, true);

    QFile file(path);
    if(file.exists() && file.open(QIODevice::ReadOnly))
    {
        const QByteArray bytes = file.readAll();
        file.close();
        m_dumpWorkspace.clearDump();
        const Ntag::Dump dump = Ntag::parseDump(bytes);
        if(dump.valid && !dump.pages.isEmpty())
        {
            for(int i = 0; i < m_pages && i < dump.pages.size(); i++)
            {
                (*pages)[i] = dump.pages.at(i);
                (*states)[i] = Ntag::PageState::Read;
            }
            return true;
        }
    }
    else
    {
        m_dumpWorkspace.clearDump();
    }

    // Fallback: per-page reads.  The client caps rdbl at page 41, so pages
    // beyond that stay Unknown and the report says Incomplete instead of
    // pretending they passed.
    int read = 0;
    const int limit = qMin(m_pages, 42);
    for(int page = 0; page < limit; page++)
    {
        QString data;
        if(readPageData(page, &data))
        {
            (*pages)[page] = QByteArray::fromHex(data.toLatin1());
            (*states)[page] = Ntag::PageState::Read;
            read++;
        }
        else
        {
            (*states)[page] = Ntag::PageState::Failed;
        }
    }
    return read > 0;
}

void NtagTab::verifyWrittenPages(const QMap<int, QByteArray> &expected,
                                 const QString &title)
{
    if(expected.isEmpty())
        return;
    QList<QByteArray> pages;
    QVector<Ntag::PageState> states;
    if(!readPagesForVerify(&pages, &states))
    {
        setStatus(tr("The tag could not be read back, so the write result is unknown."), true);
        QMessageBox::warning(this, title,
                             tr("The tag could not be read back. The write result "
                                "cannot be verified."));
        return;
    }

    const Ntag::VerifyReport report = Ntag::verifyWrite(m_cap, expected, pages, states);
    for(const Ntag::PageCheck &check : report.checks)
    {
        if(check.actual.size() != 4)
            continue;
        Ntag::MemoryPage *entry = m_mem.page(check.page);
        if(!entry)
            continue;
        // The read-back is the new truth for the original snapshot; keep the
        // edited target so the dirty indicator still reflects a mismatch.
        entry->original = check.actual;
        entry->verified = check.verdict == Ntag::PageVerdict::Match;
        if(entry->verified)
            entry->writeState = Ntag::WriteState::Written;
        updateRow(check.page);
    }
    updateCounts();
    updateDirtyButtons();
    showVerifyReport(report, title,
                     tr("Only pages targeted by the write are compared. "
                        "Manufacturer, UID, lock, configuration and security pages "
                        "cannot cause a failure."));
}

void NtagTab::showVerifyReport(const Ntag::VerifyReport &report, const QString &title,
                               const QString &context)
{
    QString status;
    switch(report.status)
    {
    case Ntag::VerifyStatus::Passed:
        status = tr("Passed: every writable target page matches.");
        break;
    case Ntag::VerifyStatus::PassedWithExceptions:
        status = tr("Passed with Exceptions: every writable target page matches, "
                    "but some pages were read-only or could not be checked.");
        break;
    case Ntag::VerifyStatus::Failed:
        status = tr("Failed: at least one writable target page differs.");
        break;
    default:
        status = tr("Incomplete: not enough pages were read to decide.");
        break;
    }

    QString summary = status + QLatin1Char('\n') +
            tr("Writable targets: %1, passed: %2, failed: %3, not checkable: %4.")
            .arg(report.writableTargets).arg(report.writablePassed)
            .arg(report.writableFailed).arg(report.unchecked);
    if(!context.isEmpty())
        summary += QLatin1Char('\n') + context;

    QStringList rows;
    rows << tr("Page\tClass\tExpected\tActual\tResult");
    for(const Ntag::PageCheck &check : report.checks)
    {
        rows << QStringLiteral("%1\t%2\t%3\t%4\t%5")
                .arg(check.page)
                .arg(pageClassText(check.pageClass))
                .arg(check.expected.isEmpty()
                     ? QStringLiteral("--")
                     : QString::fromLatin1(check.expected.toHex(' ')).toUpper())
                .arg(check.actual.isEmpty()
                     ? tr("(not readable)")
                     : QString::fromLatin1(check.actual.toHex(' ')).toUpper())
                .arg(verdictText(check.verdict) +
                     (check.note.isEmpty() ? QString() : QStringLiteral(" (") + check.note + QLatin1Char(')')));
    }

    QMessageBox box(this);
    box.setWindowTitle(title);
    box.setText(summary);
    box.setDetailedText(rows.join(QLatin1Char('\n')));
    if(report.status == Ntag::VerifyStatus::Passed ||
       report.status == Ntag::VerifyStatus::PassedWithExceptions)
        box.setIcon(QMessageBox::Information);
    else
        box.setIcon(QMessageBox::Warning);
    box.exec();

    const bool ok = report.status == Ntag::VerifyStatus::Passed ||
            report.status == Ntag::VerifyStatus::PassedWithExceptions;
    setStatus(tr("Verification: %1 (passed %2/%3 writable pages).")
              .arg(Ntag::verifyStatusName(report.status))
              .arg(report.writablePassed).arg(report.writableTargets), !ok);
}

void NtagTab::setReadBusy(bool busy)
{
    for(QPushButton *button : m_busyButtons)
        if(button)
            button->setEnabled(!busy);
    if(cancelReadButton)
        cancelReadButton->setEnabled(busy);
    table->setEditTriggers(busy ? QAbstractItemView::NoEditTriggers
                                : (QAbstractItemView::DoubleClicked |
                                   QAbstractItemView::EditKeyPressed));
    if(!busy)
        updateDirtyButtons();
}

bool NtagTab::parseReadPageOutput(const QString &output, QString *data)
{
    const QRegularExpressionMatch match = QRegularExpression(
                QString::fromLatin1(kPageDataPattern)).match(output);
    if(!match.hasMatch())
        return false;
    const QString value = normalizePageData(match.captured(2));
    if(value.isEmpty())
        return false;
    if(data)
        *data = value;
    return true;
}

void NtagTab::startReadJob(Ntag::ReadJobMode mode, const QList<int> &pages)
{
    if(m_readJob.busy)
    {
        setStatus(tr("A read is already running."));
        return;
    }
    if(m_pages <= 0)
    {
        setStatus(tr("Detect a card first; page count is unknown."), true);
        return;
    }
    if(!session)
    {
        setStatus(tr("No client session available for asynchronous reads."), true);
        return;
    }
    if(mode == Ntag::ReadJobMode::DumpChunks && !m_dumpWorkspace.isValid())
    {
        setStatus(tr("No writable temporary directory is available for the dump."), true);
        return;
    }
    const int total = mode == Ntag::ReadJobMode::DumpChunks ? m_pages : pages.size();
    if(!m_readJob.start(mode, total))
    {
        setStatus(tr("A read is already running."));
        return;
    }
    readPages = pages;
    readPageIndex = 0;
    readChunkStart = 0;
    readPath = dumpPath();
    m_dumpWorkspace.clearDump();
    m_readJobId++;
    Logger::info(QStringLiteral("NtagRead"),
                 QStringLiteral("job #%1 start mode=%2 total=%3 uid=%4")
                 .arg(m_readJobId)
                 .arg(mode == Ntag::ReadJobMode::DumpChunks
                      ? QStringLiteral("dump") : QStringLiteral("pages"))
                 .arg(total).arg(uidLabel->text()));
    setReadBusy(true);
    setStatus(mode == Ntag::ReadJobMode::DumpChunks
              ? tr("Reading %1 pages...").arg(total)
              : tr("Reading %1 selected page(s)...").arg(total));
    issueNextReadCommand();
}

void NtagTab::issueNextReadCommand()
{
    if(!m_readJob.busy)
        return;
    if(m_readJob.cancelled)
    {
        finishReadJob(true);
        return;
    }

    QString key;
    if(!checkKey(&key))
    {
        finishReadJob(true);
        return;
    }

    if(m_readJob.mode == Ntag::ReadJobMode::DumpChunks)
    {
        if(readChunkStart >= m_pages)
        {
            finishReadJob(false);
            return;
        }
        const int count = qMin(32, m_pages - readChunkStart);
        m_dumpWorkspace.clearDump();
        QString cmd = QStringLiteral("hf mfu dump -p %1 -q %2 -f \"%3\"")
                .arg(readChunkStart).arg(count).arg(readPath);
        if(!key.isEmpty())
            cmd += QStringLiteral(" -k ") + key;
        readCommandId = session->executeCommand(cmd, 60000, true);
        Logger::info(QStringLiteral("NtagRead"),
                     QStringLiteral("job #%1 command #%2 dump pages %3..%4")
                     .arg(m_readJobId).arg(readCommandId)
                     .arg(readChunkStart).arg(readChunkStart + count - 1));
        setStatus(tr("Reading pages %1..%2 of %3 (async)...")
                  .arg(readChunkStart).arg(readChunkStart + count - 1).arg(m_pages - 1));
    }
    else if(m_readJob.mode == Ntag::ReadJobMode::Pages)
    {
        if(readPageIndex >= readPages.size())
        {
            finishReadJob(false);
            return;
        }
        const int page = readPages.at(readPageIndex);
        QString cmd = QStringLiteral("hf mfu rdbl -b %1").arg(page);
        cmd += pageForceSuffix(page);
        if(!key.isEmpty())
            cmd += QStringLiteral(" -k ") + key;
        readCommandId = session->executeCommand(cmd, 8000, true);
        Logger::info(QStringLiteral("NtagRead"),
                     QStringLiteral("job #%1 command #%2 page %3")
                     .arg(m_readJobId).arg(readCommandId).arg(page));
        setStatus(tr("Reading page %1 (%2 of %3)...")
                  .arg(page).arg(readPageIndex + 1).arg(readPages.size()));
    }
    else
    {
        finishReadJob(false);
        return;
    }

    if(readCommandId == 0)
        finishReadJob(false);
}

void NtagTab::onReadCommandFinished(quint64 id, const QString &output, bool success,
                                    bool timedOut, const QString &error)
{
    if(!m_readJob.busy || id != readCommandId)
        return;

    if(m_readJob.mode == Ntag::ReadJobMode::DumpChunks)
    {
        const int start = readChunkStart;
        const int count = qMin(32, m_pages - start);
        int failed = 0;
        bool chunkValid = false;
        if(timedOut || !success)
        {
            failed = count;
        }
        else
        {
            QFile file(readPath);
            if(file.exists() && file.open(QIODevice::ReadOnly))
            {
                const QByteArray bytes = file.readAll();
                file.close();
                const Ntag::Dump dump = Ntag::parseDump(bytes);
                if(dump.valid && !dump.pages.isEmpty())
                {
                    chunkValid = true;
                    if(!m_version.valid)
                    {
                        m_version = Ntag::parseVersion(dump.versionRaw);
                        if(m_version.valid)
                            versionLabel->setText(QString::fromLatin1(
                                                      m_version.raw.toHex(' ')).toUpper());
                    }
                    for(int i = 0; i < count; i++)
                    {
                        const int page = start + i;
                        if(i < dump.pages.size())
                        {
                            setRowData(page, QString::fromLatin1(
                                           dump.pages.at(i).toHex(' ')).toUpper());
                            m_readJob.success();
                        }
                        else
                        {
                            m_mem.markReadFailed(page, tr("read failed"));
                            updateRow(page);
                            failed++;
                        }
                    }
                }
                else
                {
                    failed = count;
                }
            }
            else
            {
                failed = count;
            }
            m_dumpWorkspace.clearDump();
        }
        if(!chunkValid)
        {
            m_readJob.failure(count);
            for(int page = start; page < start + count; page++)
            {
                m_mem.markReadFailed(page, error.isEmpty() ? tr("read failed") : error);
                updateRow(page);
            }
        }
        else if(failed > 0)
        {
            m_readJob.failure(failed);
        }
        readChunkStart += count;
        if(timedOut)
        {
            // The client is restarting after a timeout; never queue the next
            // chunk against an unknown client state.
            finishReadJob(false);
            return;
        }
        if(m_readJob.cancelled)
            finishReadJob(true);
        else
            issueNextReadCommand();
        return;
    }

    if(m_readJob.mode == Ntag::ReadJobMode::Pages)
    {
        const int page = readPages.at(readPageIndex);
        QString data;
        if(timedOut || !success || !parseReadPageOutput(output, &data))
        {
            m_readJob.failure();
            m_mem.markReadFailed(page, error.isEmpty() ? tr("read failed") : error);
            updateRow(page);
        }
        else
        {
            QString value = normalizePageData(data);
            if(!value.isEmpty())
            {
                setRowData(page, value);
                m_readJob.success();
            }
            else
            {
                m_readJob.failure();
                m_mem.markReadFailed(page, tr("read failed"));
                updateRow(page);
            }
        }
        readPageIndex++;
        if(timedOut)
        {
            finishReadJob(false);
            return;
        }
        if(m_readJob.cancelled)
            finishReadJob(true);
        else
            issueNextReadCommand();
        return;
    }

    finishReadJob(false);
}

void NtagTab::finishReadJob(bool cancelled)
{
    if(!m_readJob.busy)
        return;
    const int done = m_readJob.done;
    const int total = m_readJob.total;
    const int failures = m_readJob.failures;
    Logger::info(QStringLiteral("NtagRead"),
                 QStringLiteral("job #%1 finish cancelled=%2 done=%3 total=%4 failures=%5")
                 .arg(m_readJobId).arg(cancelled).arg(done).arg(total).arg(failures));
    m_readJob.finish();
    readCommandId = 0;
    readPages.clear();
    m_dumpWorkspace.clearDump();
    setReadBusy(false);
    refreshCcFromTable();
    fillNdef();
    updateCounts();
    updateLockState();
    updateDirtyButtons();
    updateDetail();
    updateQuickPreview();

    if(cancelled)
        setStatus(tr("Read cancelled: %1 of %2 pages read, %3 failed.")
                  .arg(done).arg(total).arg(failures), failures > 0);
    else if(failures > 0)
        setStatus(tr("Read finished: %1 of %2 pages read, %3 failed.")
                  .arg(done).arg(total).arg(failures), true);
    else
        setStatus(tr("Read finished: %1 of %2 pages.").arg(done).arg(total));
}

void NtagTab::cancelActiveJob()
{
    if(m_readJob.busy)
    {
        m_readJob.requestCancel();
        cancelReadButton->setEnabled(false);
        setStatus(tr("Cancelling after the current command..."));
        return;
    }
    if(m_writeJob.busy)
    {
        m_writeJob.requestCancel();
        cancelReadButton->setEnabled(false);
        // A page write that is already in flight is always followed by its
        // read-back before the job stops.
        setStatus(m_writeJob.phase == Ntag::WriteJobPhase::ReadBack
                  ? tr("Finishing the current page verification, then stopping...")
                  : tr("Cancelling after the current command..."));
    }
}

void NtagTab::readAllPages()
{
    if(deviceBusy())
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return;
    }
    if(m_pages <= 0)
    {
        setStatus(tr("Detect a card first; page count is unknown."), true);
        return;
    }
    if(!confirmDiscardEdits(tr("A full read replaces the current memory view.")))
        return;

    m_undoStack.clear();
    m_redoStack.clear();
    m_lastWriteTargets.clear();

    if(session)
    {
        startReadJob(Ntag::ReadJobMode::DumpChunks, QList<int>());
        return;
    }

    if(m_cap.pages > 0)
    {
        setStatus(tr("Reading %1 pages with `hf mfu dump`...").arg(m_pages));
        if(readAllFromDump())
        {
            fillNdef();
            updateCounts();
            updateLockState();
            updateDirtyButtons();
            updateDetail();
            return;
        }
        setStatus(tr("Dump failed; falling back to per-page reads with --force..."), true);
    }

    int success = 0;
    for(int page = 0; page < m_pages; page++)
    {
        setStatus(tr("Reading page %1 of %2...").arg(page).arg(m_pages - 1));
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QString data;
        if(readPageData(page, &data))
        {
            setRowData(page, data);
            success++;
        }
        else
        {
            m_mem.markReadFailed(page, tr("read failed"));
            updateRow(page);
        }
    }
    fillNdef();
    updateCounts();
    updateLockState();
    updateDirtyButtons();
    updateDetail();
    setStatus(tr("Read %1 of %2 pages (per-page).")
              .arg(success).arg(m_pages), success != m_pages);
}

void NtagTab::readSelectedPages()
{
    if(deviceBusy())
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return;
    }
    const QList<QTableWidgetItem *> selected = table->selectedItems();
    QSet<int> pages;
    for(QTableWidgetItem *item : selected)
        pages.insert(item->row());
    if(pages.isEmpty())
    {
        setStatus(tr("Select one or more page rows first."), true);
        return;
    }

    QList<int> toRead;
    for(int page : pages)
    {
        const Ntag::MemoryPage *entry = m_mem.page(page);
        if(entry && entry->dirty())
        {
            const QMessageBox::StandardButton choice = QMessageBox::question(
                        this, tr("Uncommitted edits"),
                        tr("Page %1 has uncommitted edits. Re-read it and discard "
                           "those edits?").arg(page),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if(choice != QMessageBox::Yes)
                continue;
            m_mem.revertPage(page);
            updateRow(page);
        }
        toRead.append(page);
    }
    if(toRead.isEmpty())
        return;

    if(session)
    {
        startReadJob(Ntag::ReadJobMode::Pages, toRead);
        return;
    }

    int success = 0;
    for(int page : toRead)
    {
        QString data;
        if(readPageData(page, &data))
        {
            setRowData(page, data);
            success++;
        }
    }
    updateCounts();
    updateDirtyButtons();
    updateDetail();
    setStatus(tr("Read %1 of %2 selected pages.").arg(success).arg(toRead.size()),
              success != toRead.size());
}

void NtagTab::readOnePage()
{
    if(deviceBusy())
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return;
    }
    const int page = pageSpin->value();
    const Ntag::MemoryPage *entry = m_mem.page(page);
    if(entry && entry->dirty())
    {
        const QMessageBox::StandardButton choice = QMessageBox::question(
                    this, tr("Uncommitted edits"),
                    tr("Page %1 has uncommitted edits. Re-read it and discard those "
                       "edits?").arg(page),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if(choice != QMessageBox::Yes)
            return;
        m_mem.revertPage(page);
        updateRow(page);
    }
    if(session)
    {
        startReadJob(Ntag::ReadJobMode::Pages, QList<int>({page}));
        return;
    }
    QString data;
    if(!readPageData(page, &data))
        return;
    setRowData(page, data);
    updateCounts();
    updateDirtyButtons();
    updateDetail();
    setStatus(tr("Page %1 read.").arg(page));
}

bool NtagTab::writePageData(int page, const QString &data, bool confirm)
{
    m_lastWriteError.clear();
    const QString value = normalizePageData(data);
    if(value.isEmpty())
    {
        m_lastWriteError = tr("page data must be 8 hex digits");
        setStatus(tr("Page data must be 8 hex digits."), true);
        return false;
    }
    if(!isWritable(page))
    {
        m_lastWriteError = tr("page %1 is not writable under the detected layout").arg(page);
        setStatus(tr("Page %1 is not writable under the detected layout.").arg(page), true);
        return false;
    }

    QString key;
    if(!checkKey(&key))
    {
        m_lastWriteError = tr("key format is invalid");
        setStatus(tr("Key must be 8 or 32 hex digits."), true);
        return false;
    }

    if(confirm)
    {
        const QMessageBox::StandardButton choice = QMessageBox::warning(
            this, tr("Write NTAG/Ultralight page"),
            tr("Write %1 to page %2?").arg(value).arg(page),
            QMessageBox::Yes | QMessageBox::No);
        if(choice != QMessageBox::Yes)
            return false;
    }

    QString cmd = QStringLiteral("hf mfu wrbl -b %1 -d %2").arg(page).arg(value);
    cmd += pageForceSuffix(page);
    if(!key.isEmpty())
        cmd += QStringLiteral(" -k ") + key;
    const QString output = util->execCMDWithOutput(cmd, 10000, true);
    if(output.isEmpty() || output.contains(QStringLiteral("fail"), Qt::CaseInsensitive) ||
       output.contains(QStringLiteral("error"), Qt::CaseInsensitive) ||
       output.contains(QStringLiteral("not available"), Qt::CaseInsensitive) ||
       output.contains(QStringLiteral("timeout"), Qt::CaseInsensitive))
    {
        m_lastWriteError = tr("client write failed for page %1").arg(page);
        m_mem.markWritten(page, false, m_lastWriteError);
        updateRow(page);
        setStatus(tr("Write to page %1 failed. See the RawCommand output.").arg(page), true);
        return false;
    }

    QString verify;
    if(!readPageData(page, &verify))
    {
        m_lastWriteError = tr("page %1 was written but could not be read back").arg(page);
        m_mem.markWritten(page, false, m_lastWriteError);
        updateRow(page);
        return false;
    }
    Ntag::MemoryPage *entry = m_mem.page(page);
    if(verify != value)
    {
        if(entry)
        {
            entry->original = QByteArray::fromHex(verify.toLatin1());
            entry->writeState = Ntag::WriteState::Failed;
            entry->verified = false;
            entry->error = tr("wrote %1, read %2").arg(value, verify);
        }
        m_lastWriteError = entry ? entry->error : tr("verification differs");
        updateRow(page);
        setStatus(tr("Page %1 write returned success but verification differs (wrote %2, read %3).")
                  .arg(page).arg(value, verify), true);
        return false;
    }
    if(entry)
    {
        entry->original = QByteArray::fromHex(verify.toLatin1());
        entry->edited = entry->original;
        entry->writeState = Ntag::WriteState::Written;
        entry->verified = true;
        entry->error.clear();
    }
    updateRow(page);
    updateDetail();
    return true;
}

void NtagTab::writeOnePage()
{
    if(deviceBusy())
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return;
    }
    const int page = pageSpin->value();
    const QString value = normalizePageData(dataEdit->text());
    if(value.isEmpty())
    {
        setStatus(tr("Page data must be 8 hex digits."), true);
        return;
    }
    const Ntag::MemoryPage *entry = m_mem.page(page);
    if(!entry || !entry->readable())
    {
        setStatus(tr("Read page %1 before writing it.").arg(page), true);
        return;
    }
    if(!isWritable(page))
    {
        setStatus(tr("Page %1 is not writable under the detected layout.").arg(page), true);
        return;
    }

    const QMessageBox::StandardButton choice = QMessageBox::warning(
        this, tr("Write NTAG/Ultralight page"),
        tr("Write %1 to page %2?").arg(value).arg(page),
        QMessageBox::Yes | QMessageBox::No);
    if(choice != QMessageBox::Yes)
        return;

    QMap<int, QByteArray> expected;
    expected.insert(page, QByteArray::fromHex(value.toLatin1()));
    m_lastWriteTargets.clear();
    m_lastWriteError.clear();
    startWriteJob(Ntag::WriteJobMode::WritePages, QList<int>({page}), expected,
                  tr("Write verification"), false);
}

void NtagTab::writeSelectedPages()
{
    QList<int> pages;
    const QList<QTableWidgetItem *> selected = table->selectedItems();
    for(QTableWidgetItem *item : selected)
        if(!pages.contains(item->row()))
            pages.append(item->row());
    std::sort(pages.begin(), pages.end());
    if(pages.isEmpty())
    {
        setStatus(tr("Select one or more writable page rows first."), true);
        return;
    }
    writePages(pages, tr("Write selected pages"));
}

void NtagTab::writeDirtyPages()
{
    const QList<int> dirty = m_mem.dirtyPages();
    if(dirty.isEmpty())
    {
        setStatus(tr("There are no uncommitted edits to write."));
        return;
    }
    writePages(dirty, tr("Write dirty pages"));
}

bool NtagTab::writePages(const QList<int> &requested, const QString &title)
{
    if(deviceBusy())
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return false;
    }
    QList<int> targets;
    for(int page : requested)
    {
        const Ntag::MemoryPage *entry = m_mem.page(page);
        if(!entry || !entry->readable())
        {
            setStatus(tr("Page %1 has not been read; write cancelled.").arg(page), true);
            return false;
        }
        if(!entry->writable)
        {
            setStatus(tr("Page %1 is read-only; write cancelled.").arg(page), true);
            return false;
        }
        if(entry->dirty())
            targets.append(page);
    }
    if(targets.isEmpty())
    {
        setStatus(tr("The selected pages have no uncommitted changes."));
        return false;
    }

    QStringList pageText;
    for(int page : targets)
        pageText << QString::number(page);
    const QMessageBox::StandardButton choice = QMessageBox::warning(
                this, title,
                tr("Write %1 page(s)?\n%2\n\nOnly these user pages are written; "
                   "UID, CC, lock, OTP, configuration and PWD/PACK are never "
                   "modified. The result is verified by reading each page back.")
                .arg(targets.size())
                .arg(pageText.size() <= 24 ? pageText.join(QStringLiteral(", "))
                                           : pageText.first() + QStringLiteral("..") + pageText.last()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if(choice != QMessageBox::Yes)
    {
        setStatus(tr("Write cancelled."));
        return false;
    }

    QMap<int, QByteArray> planned;
    for(int page : targets)
    {
        const Ntag::MemoryPage *entry = m_mem.page(page);
        planned.insert(page, entry->edited);
    }
    m_lastWriteTargets.clear();
    m_lastWriteError.clear();
    m_writeFailedPages.clear();
    startWriteJob(Ntag::WriteJobMode::WritePages, targets, planned,
                  tr("Write verification"), true);
    return true;
}

void NtagTab::verifyWritten()
{
    if(deviceBusy())
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return;
    }
    QMap<int, QByteArray> expected = m_lastWriteTargets;
    if(expected.isEmpty())
    {
        const QList<int> dirty = m_mem.dirtyPages();
        expected = editedSnapshot(dirty);
    }
    if(expected.isEmpty())
    {
        setStatus(tr("No written or edited pages to verify."));
        return;
    }
    QList<int> targets = expected.keys();
    std::sort(targets.begin(), targets.end());
    startWriteJob(Ntag::WriteJobMode::Verify, targets, expected,
                  tr("Verify written pages"), false);
}

void NtagTab::verifyOnePage()
{
    if(deviceBusy())
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return;
    }
    const int page = pageSpin->value();
    const Ntag::MemoryPage *entry = m_mem.page(page);
    if(!entry || !entry->readable())
    {
        setStatus(tr("Read page %1 before verifying it.").arg(page), true);
        return;
    }
    QMap<int, QByteArray> expected;
    expected.insert(page, entry->edited);
    startWriteJob(Ntag::WriteJobMode::Verify, QList<int>({page}), expected,
                  tr("Page verification"), false, true);
}

void NtagTab::eraseTag()
{
    if(deviceBusy())
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return;
    }
    if(!m_knownLayout || m_cap.model == Ntag::Model::Unknown || m_pages <= 0)
    {
        setStatus(tr("Refusing to erase: the model or memory layout is not known."), true);
        QMessageBox::warning(this, tr("Erase NTAG"),
                             tr("The tag model or its memory layout is not known, so no "
                                "page range can be assumed. Detect the tag again or use "
                                "manual page operations."));
        return;
    }
    if(m_cc.valid && !m_cc.readWrite)
    {
        setStatus(tr("Refusing to erase: the Capability Container reports read-only access."), true);
        QMessageBox::warning(this, tr("Erase NTAG"),
                             tr("The Capability Container marks the tag read-only. "
                                "Nothing was written."));
        return;
    }
    if(!session)
    {
        setStatus(tr("No client session is available for an asynchronous erase."), true);
        return;
    }
    if(!m_dumpWorkspace.isValid())
    {
        setStatus(tr("No writable temporary directory is available for the erase snapshot."), true);
        return;
    }
    if(!confirmDiscardEdits(tr("Erase reads the tag again and replaces the current view.")))
        return;

    m_undoStack.clear();
    m_redoStack.clear();
    m_lastWriteTargets.clear();
    m_lastWriteError.clear();
    m_writeFailedPages.clear();

    if(!m_writeJob.start(Ntag::WriteJobMode::Erase, 1))
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return;
    }
    writeEraseFlow = true;
    writeVerifyPageOnly = false;
    writeAllowRetry = false;
    writeTitle = tr("Erase NTAG");
    writeTargets.clear();
    writePlanned.clear();
    writeExpected.clear();
    writeIndex = 0;
    m_writeJob.phase = Ntag::WriteJobPhase::Snapshot;
    m_writeJobId++;

    setReadBusy(true);
    setStatus(tr("Reading every page before building the erase plan..."));

    m_dumpWorkspace.clearDump();
    QString cmd = QStringLiteral("hf mfu dump -p 0 -q %1 -f \"%2\"")
            .arg(m_pages).arg(dumpPath());
    QString key;
    if(checkKey(&key) && !key.isEmpty())
        cmd += QStringLiteral(" -k ") + key;
    writeCommandId = session->executeCommand(cmd, 120000, true);
    Logger::info(QStringLiteral("NtagWrite"),
                 QStringLiteral("job #%1 erase snapshot command #%2 pages 0..%3")
                 .arg(m_writeJobId).arg(writeCommandId).arg(m_pages - 1));
    if(writeCommandId == 0)
        finishWriteJob(false, tr("Could not start the erase snapshot command."), true);
}

void NtagTab::startWriteJob(Ntag::WriteJobMode mode, const QList<int> &targets,
                            const QMap<int, QByteArray> &expected,
                            const QString &title, bool allowRetry,
                            bool verifyPageOnly)
{
    if(deviceBusy())
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return;
    }
    if(!session)
    {
        setStatus(tr("No client session is available for asynchronous writes."), true);
        return;
    }
    if(!m_dumpWorkspace.isValid())
    {
        setStatus(tr("No writable temporary directory is available for verification."), true);
        return;
    }
    if(targets.isEmpty())
        return;
    if(!m_writeJob.start(mode, targets.size()))
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return;
    }

    writeTargets = targets;
    writeExpected.clear();
    writePlanned.clear();
    writeIndex = 0;
    writeAllowRetry = allowRetry;
    writeVerifyPageOnly = verifyPageOnly;
    writeEraseFlow = false;
    writeTitle = title;
    writePageHex.clear();
    m_lastWriteError.clear();
    m_lastWriteTargets.clear();
    m_writeFailedPages.clear();
    m_writeJobId++;

    Logger::info(QStringLiteral("NtagWrite"),
                 QStringLiteral("job #%1 start mode=%2 pages=%3 retry=%4")
                 .arg(m_writeJobId)
                 .arg(mode == Ntag::WriteJobMode::Verify ? QStringLiteral("verify")
                      : mode == Ntag::WriteJobMode::Erase ? QStringLiteral("erase")
                                                          : QStringLiteral("write"))
                 .arg(targets.size()).arg(allowRetry));

    setReadBusy(true);

    if(mode == Ntag::WriteJobMode::Verify)
    {
        writeExpected = expected;
        if(verifyPageOnly)
        {
            const int page = targets.first();
            m_writeJob.beginPage(page);
            m_writeJob.phase = Ntag::WriteJobPhase::ReadBack;
            submitWriteReadBack(page);
        }
        else
        {
            m_writeJob.phase = Ntag::WriteJobPhase::Verifying;
            submitWriteVerifyDump();
        }
        return;
    }

    writePlanned = expected;
    m_writeJob.phase = Ntag::WriteJobPhase::Writing;
    setStatus(tr("Writing %1 page(s)...").arg(targets.size()));
    issueNextWriteCommand();
}

void NtagTab::issueNextWriteCommand()
{
    if(!m_writeJob.busy)
        return;
    if(m_writeJob.cancelled)
    {
        const bool partial = m_writeJob.written > 0;
        finishWriteJob(true,
                       partial
                       ? tr("Write cancelled: %1 of %2 page(s) were written and verified.")
                         .arg(m_writeJob.written).arg(m_writeJob.total)
                       : tr("Write cancelled before any page was written."));
        return;
    }
    const int stepCount = writeSequence.isEmpty() ? writeTargets.size()
                                                  : writeSequence.size();
    if(writeIndex >= stepCount)
    {
        if(writeExpected.isEmpty())
        {
            finishWriteJob(false, tr("No page was written, so there is nothing to verify."),
                           true);
            return;
        }
        submitWriteVerifyDump();
        return;
    }

    const int page = writeSequence.isEmpty() ? writeTargets.at(writeIndex)
                                             : writeSequence.at(writeIndex).first;
    const QByteArray value = writeSequence.isEmpty() ? writePlanned.value(page)
                                                     : writeSequence.at(writeIndex).second;
    if(value.size() != 4)
    {
        writeAttemptFailed(tr("no target data for page %1").arg(page));
        return;
    }

    QString key;
    if(!checkKey(&key))
    {
        writeAttemptFailed(tr("key format is invalid"));
        return;
    }

    writePageHex = QString::fromLatin1(value.toHex()).toUpper();
    m_writeJob.beginPage(page);
    m_writeJob.phase = Ntag::WriteJobPhase::Writing;
    const int shownIndex = writeIndex + 1;
    setStatus(writeEraseFlow
              ? tr("Clearing page %1 (%2 of %3)...")
                .arg(page).arg(shownIndex).arg(stepCount)
              : writeQuickFlow && writeIndex == 0 && writeSequence.size() > 1
                && page == m_cap.userFirst
                ? tr("Writing page %1 with TLV length 0 (%2 of %3)...")
                  .arg(page).arg(shownIndex).arg(stepCount)
                : tr("Writing page %1 (%2 of %3)...")
                  .arg(page).arg(shownIndex).arg(stepCount));

    QString cmd = QStringLiteral("hf mfu wrbl -b %1 -d %2").arg(page).arg(writePageHex);
    cmd += pageForceSuffix(page);
    if(!key.isEmpty())
        cmd += QStringLiteral(" -k ") + key;

    writeCommandId = session->executeCommand(cmd, 10000, true);
    Logger::info(QStringLiteral("NtagWrite"),
                 QStringLiteral("job #%1 command #%2 %3 page %4 value %5")
                 .arg(m_writeJobId).arg(writeCommandId)
                 .arg(writeEraseFlow ? QStringLiteral("clear") : QStringLiteral("write"))
                 .arg(page).arg(writePageHex));
    if(writeCommandId == 0)
        writeAttemptFailed(tr("the client command could not be queued"));
}

void NtagTab::submitWriteReadBack(int page)
{
    QString key;
    if(!checkKey(&key))
    {
        writeAttemptFailed(tr("key format is invalid"));
        return;
    }
    QString cmd = QStringLiteral("hf mfu rdbl -b %1").arg(page);
    cmd += pageForceSuffix(page);
    if(!key.isEmpty())
        cmd += QStringLiteral(" -k ") + key;
    writeCommandId = session->executeCommand(cmd, 8000, true);
    Logger::info(QStringLiteral("NtagWrite"),
                 QStringLiteral("job #%1 command #%2 read-back page %3")
                 .arg(m_writeJobId).arg(writeCommandId).arg(page));
    setStatus(tr("Reading page %1 back for verification...").arg(page));
    if(writeCommandId == 0)
        writeAttemptFailed(tr("the client read-back command could not be queued"));
}

void NtagTab::writeAttemptFailed(const QString &reason)
{
    const int page = m_writeJob.currentPage;
    if(writeAllowRetry && !m_writeJob.retryUsed)
    {
        m_writeJob.retryUsed = true;
        setStatus(tr("Retrying page %1 after: %2").arg(page).arg(reason), true);
        QTimer::singleShot(40, this, [this, page]() {
            if(!m_writeJob.busy || m_writeJob.currentPage != page)
                return;
            if(m_writeJob.phase != Ntag::WriteJobPhase::Writing &&
               m_writeJob.phase != Ntag::WriteJobPhase::ReadBack)
                return;
            if(m_writeJob.cancelled)
            {
                // The first attempt may have reached the tag even though it
                // failed or timed out.  Confirm the current page before
                // stopping instead of assuming it was not written.
                m_writeJob.phase = Ntag::WriteJobPhase::ReadBack;
                submitWriteReadBack(page);
                return;
            }
            QString key;
            if(!checkKey(&key))
            {
                writeAttemptFailed(tr("key format is invalid"));
                return;
            }
            m_writeJob.phase = Ntag::WriteJobPhase::Writing;
            QString cmd = QStringLiteral("hf mfu wrbl -b %1 -d %2")
                    .arg(page).arg(writePageHex);
            cmd += pageForceSuffix(page);
            if(!key.isEmpty())
                cmd += QStringLiteral(" -k ") + key;
            writeCommandId = session->executeCommand(cmd, 10000, true);
            if(writeCommandId == 0)
                writeAttemptFailed(tr("the client retry command could not be queued"));
        });
        return;
    }

    m_writeJob.failure();
    m_lastWriteError = reason;
    if(m_writeJob.mode == Ntag::WriteJobMode::Verify)
    {
        const int page = m_writeJob.currentPage;
        const bool cancelled = m_writeJob.cancelled;
        finishWriteJob(cancelled,
                       tr("Verification failed for page %1: %2").arg(page).arg(reason),
                       true);
        if(!cancelled)
        {
            QMessageBox::warning(this, writeTitle,
                                 tr("Page %1 could not be verified: %2")
                                 .arg(page).arg(reason));
        }
        return;
    }
    m_writeFailedPages << QString::number(page);
    Ntag::MemoryPage *entry = m_mem.page(page);
    if(entry)
    {
        entry->writeState = Ntag::WriteState::Failed;
        entry->verified = false;
        entry->error = reason;
    }
    m_mem.markWritten(page, false, reason);
    updateRow(page);
    updateDetail();

    const int written = m_writeJob.written;
    const int total = m_writeJob.total;
    const bool cancelled = m_writeJob.cancelled;
    const QString quickNote = writeQuickFlow
            ? tr(" The NDEF TLV may still have length 0; rewrite the record to "
                 "restore a valid NDEF message.")
            : QString();
    finishWriteJob(cancelled,
                   (cancelled
                    ? tr("Write cancelled at page %1: %2 (%3 of %4 page(s) written).")
                      .arg(page).arg(reason).arg(written).arg(total)
                    : tr("Write stopped at page %1: %2 (%3 of %4 page(s) written).")
                      .arg(page).arg(reason).arg(written).arg(total)) + quickNote,
                   true);

    if(!cancelled)
    {
        QMessageBox::warning(this, writeTitle,
                             tr("Write stopped at page %1: %2\n"
                                "%3 page(s) before it were written and verified. "
                                "No later page was attempted.%4")
                             .arg(page).arg(reason).arg(written).arg(quickNote));
    }
}

void NtagTab::commitWrittenPage(const QString &actual)
{
    const int page = m_writeJob.currentPage;
    Ntag::MemoryPage *entry = m_mem.page(page);
    if(entry)
    {
        entry->original = QByteArray::fromHex(actual.toLatin1());
        entry->edited = entry->original;
        entry->writeState = Ntag::WriteState::Written;
        entry->verified = true;
        entry->error.clear();
        if(!writeQuickFlow)
            writeExpected.insert(page, entry->original);
        m_lastWriteTargets = writeExpected;
    }
    m_mem.markWritten(page, true);
    m_mem.markVerified(page, true);
    updateRow(page);
    updateDetail();

    m_writeJob.success();
    writeIndex++;

    if(m_writeJob.cancelled)
    {
        finishWriteJob(true,
                       tr("Write cancelled: %1 of %2 page(s) were written and verified.")
                       .arg(m_writeJob.written).arg(m_writeJob.total));
        return;
    }

    if(writeIndex < (writeSequence.isEmpty() ? writeTargets.size()
                                             : writeSequence.size()))
    {
        const int nextPage = writeSequence.isEmpty() ? writeTargets.at(writeIndex)
                                                     : writeSequence.at(writeIndex).first;
        const int stepCount = writeSequence.isEmpty() ? writeTargets.size()
                                                      : writeSequence.size();
        setStatus(writeEraseFlow
                  ? tr("Clearing page %1 (%2 of %3)...")
                    .arg(nextPage)
                    .arg(m_writeJob.written + 1).arg(stepCount)
                  : tr("Writing page %1 (%2 of %3)...")
                    .arg(nextPage)
                    .arg(m_writeJob.written + 1).arg(stepCount));
    }
    issueNextWriteCommand();
}

void NtagTab::submitWriteVerifyDump()
{
    if(!m_writeJob.busy)
        return;
    if(!session)
    {
        finishWriteJob(false, tr("No client session is available for verification."), true);
        return;
    }
    m_writeJob.phase = Ntag::WriteJobPhase::Verifying;
    setStatus(tr("Reading the tag back to verify..."));
    m_dumpWorkspace.clearDump();

    QString cmd = QStringLiteral("hf mfu dump -p 0 -q %1 -f \"%2\"")
            .arg(m_pages).arg(dumpPath());
    QString key;
    if(checkKey(&key) && !key.isEmpty())
        cmd += QStringLiteral(" -k ") + key;
    writeCommandId = session->executeCommand(cmd, 120000, true);
    Logger::info(QStringLiteral("NtagWrite"),
                 QStringLiteral("job #%1 command #%2 verify dump pages 0..%3")
                 .arg(m_writeJobId).arg(writeCommandId).arg(m_pages - 1));
    if(writeCommandId == 0)
        finishWriteJob(false, tr("Could not start the verification read."), true);
}

void NtagTab::finishWriteJob(bool cancelled, const QString &message, bool error)
{
    if(!m_writeJob.busy)
        return;
    Logger::info(QStringLiteral("NtagWrite"),
                 QStringLiteral("job #%1 finish cancelled=%2 mode=%3 written=%4 total=%5 failures=%6")
                 .arg(m_writeJobId).arg(cancelled)
                 .arg(static_cast<int>(m_writeJob.mode))
                 .arg(m_writeJob.written).arg(m_writeJob.total)
                 .arg(m_writeJob.failures));
    m_writeJob.finish();
    writeCommandId = 0;
    writeTargets.clear();
    writeSequence.clear();
    writePlanned.clear();
    writeExpected.clear();
    writeVerifyPageOnly = false;
    writeEraseFlow = false;
    writeQuickFlow = false;
    writePageHex.clear();
    writeQuickDescription.clear();
    writeQuickWarnings.clear();
    writeQuickArea.clear();
    writeQuickFirstPage = -1;
    writeQuickLastPage = -1;
    m_dumpWorkspace.clearDump();
    setReadBusy(false);
    refreshCcFromTable();
    fillNdef();
    updateCounts();
    updateLockState();
    updateDirtyButtons();
    updateDetail();
    updateQuickPreview();
    setStatus(message, error);
}

void NtagTab::onWriteCommandFinished(quint64 id, const QString &output, bool success,
                                     bool timedOut, const QString &error)
{
    if(!m_writeJob.busy || id != writeCommandId)
        return;

    if(m_writeJob.phase == Ntag::WriteJobPhase::Snapshot)
    {
        const QString path = dumpPath();
        QByteArray bytes;
        QFile file(path);
        if(success && !timedOut && file.exists() && file.open(QIODevice::ReadOnly))
        {
            bytes = file.readAll();
            file.close();
        }
        m_dumpWorkspace.clearDump();

        const Ntag::Dump dump = Ntag::parseDump(bytes);
        if(!success || timedOut || !dump.valid || dump.pages.isEmpty())
        {
            finishWriteJob(false,
                           tr("Refusing to erase: the tag could not be read completely."),
                           true);
            QMessageBox::warning(this, tr("Erase NTAG"),
                                 tr("The tag could not be read completely, so the pages "
                                    "that must be preserved cannot be confirmed. "
                                    "Nothing was written."));
            return;
        }

        for(int page = 0; page < m_pages; page++)
        {
            if(page < dump.pages.size())
                setRowData(page, QString::fromLatin1(
                               dump.pages.at(page).toHex(' ')).toUpper());
            else
            {
                m_mem.markReadFailed(page, tr("read failed"));
                updateRow(page);
            }
        }
        if(!m_version.valid)
        {
            m_version = Ntag::parseVersion(dump.versionRaw);
            if(m_version.valid)
                versionLabel->setText(QString::fromLatin1(m_version.raw.toHex(' ')).toUpper());
        }
        refreshCcFromTable();
        if(m_cc.valid && !m_cc.readWrite)
        {
            finishWriteJob(false,
                           tr("Refusing to erase: the Capability Container reports read-only access."),
                           true);
            QMessageBox::warning(this, tr("Erase NTAG"),
                                 tr("The Capability Container marks the tag read-only. "
                                    "Nothing was written."));
            return;
        }

        QList<QByteArray> pages;
        for(int page = 0; page < m_pages; page++)
        {
            const Ntag::MemoryPage *entry = m_mem.page(page);
            pages.append(entry && entry->readable() ? entry->original : QByteArray());
        }

        m_erasePlan = Ntag::planErase(m_cap, pages);
        if(!m_erasePlan.valid)
        {
            const QString planError = m_erasePlan.error;
            finishWriteJob(false, tr("Refusing to erase: %1.").arg(planError), true);
            QMessageBox::warning(this, tr("Erase NTAG"),
                                 tr("No safe erase plan could be built: %1\n"
                                    "Nothing was written.").arg(planError));
            return;
        }
        if(m_erasePlan.clearPages.isEmpty())
        {
            finishWriteJob(false,
                           tr("All writable user pages already contain 00 00 00 00."));
            QMessageBox::information(this, tr("Erase NTAG"),
                                     tr("All %1 writable user pages (4..%2) are already "
                                        "blank. Nothing was written.")
                                     .arg(m_erasePlan.blankPages.size())
                                     .arg(m_erasePlan.userLast));
            return;
        }
        if(m_writeJob.cancelled)
        {
            finishWriteJob(true, tr("Erase cancelled before any page was written."));
            return;
        }

        auto rangeText = [](int first, int last) {
            return first == last ? QString::number(first)
                                 : QStringLiteral("%1..%2").arg(first).arg(last);
        };
        auto pageList = [&rangeText](const QList<int> &pages) {
            if(pages.size() <= 24)
            {
                QStringList values;
                for(int page : pages)
                    values << QString::number(page);
                return values.join(QStringLiteral(", "));
            }
            return rangeText(pages.first(), pages.last());
        };
        const QString targetText = pageList(m_erasePlan.clearPages);
        QStringList preserved;
        preserved << rangeText(0, 3) + tr(" (UID / static lock / CC)");
        if(m_cap.lockPage >= 0)
            preserved << tr("dynamic lock page %1").arg(m_cap.lockPage);
        if(m_cap.cfg0 >= 0 || m_cap.cfg1 >= 0)
            preserved << tr("configuration pages %1")
                         .arg(rangeText(m_cap.cfg0, m_cap.cfg1));
        if(m_cap.pwd >= 0 || m_cap.pack >= 0)
            preserved << tr("PWD/PACK pages %1").arg(rangeText(m_cap.pwd, m_cap.pack));

        const QString lockWarning = m_erasePlan.lockBitsPresent
                ? tr("Lock bits are already set. Some user pages may be permanently "
                     "write-protected; they will be reported as not cleared and will "
                     "not be bypassed.")
                : tr("No lock bits are set in the current read.");

        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Erase NTAG"));
        box.setText(tr("Erase writable user data from this tag?"));
        box.setInformativeText(
            tr("Model: %1\nUID: %2\n\nPages to clear: %3 (%4 page(s) currently "
               "holding data; %5 blank page(s) will be skipped)\nPreserved: %6\n\n%7\n\n"
               "Only 00 00 00 00 is written to confirmed writable user pages. UID, CC, "
               "OTP, lock, configuration and PWD/PACK pages are never modified. This "
               "cannot be undone and the original data cannot be recovered. A locked "
               "tag cannot be restored to factory state by software.")
            .arg(Ntag::modelName(m_cap.model))
            .arg(uidLabel->text())
            .arg(targetText)
            .arg(m_erasePlan.clearPages.size()).arg(m_erasePlan.blankPages.size())
            .arg(preserved.join(QStringLiteral(", ")))
            .arg(lockWarning));
        box.setDetailedText(lockWarning);
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box.setDefaultButton(QMessageBox::No);
        if(box.exec() != QMessageBox::Yes)
        {
            finishWriteJob(true, tr("Erase cancelled."));
            return;
        }

        writeTargets = m_erasePlan.clearPages;
        writePlanned.clear();
        for(int page : writeTargets)
            writePlanned.insert(page, QByteArray(4, '\0'));
        writeIndex = 0;
        m_writeJob.total = writeTargets.size();
        m_writeJob.written = 0;
        m_writeJob.phase = Ntag::WriteJobPhase::Writing;
        setStatus(tr("Clearing %1 page(s)...").arg(writeTargets.size()));
        issueNextWriteCommand();
        return;
    }

    if(m_writeJob.phase == Ntag::WriteJobPhase::Writing)
    {
        const bool commandOk = success && !timedOut && !output.isEmpty()
                && !output.contains(QStringLiteral("fail"), Qt::CaseInsensitive)
                && !output.contains(QStringLiteral("error"), Qt::CaseInsensitive)
                && !output.contains(QStringLiteral("not available"), Qt::CaseInsensitive)
                && !output.contains(QStringLiteral("timeout"), Qt::CaseInsensitive);
        if(!commandOk)
        {
            const QString reason = timedOut
                    ? tr("the client command timed out")
                    : error.isEmpty()
                      ? tr("the client reported a write failure")
                      : error;
            writeAttemptFailed(reason);
            return;
        }
        m_writeJob.phase = Ntag::WriteJobPhase::ReadBack;
        submitWriteReadBack(m_writeJob.currentPage);
        return;
    }

    if(m_writeJob.phase == Ntag::WriteJobPhase::ReadBack)
    {
        QString data;
        if(timedOut || !success || !parseReadPageOutput(output, &data))
        {
            writeAttemptFailed(timedOut
                               ? tr("the read-back timed out")
                               : error.isEmpty()
                                 ? tr("the page could not be read back")
                                 : error);
            return;
        }
        const QString actual = normalizePageData(data);
        if(actual.isEmpty())
        {
            writeAttemptFailed(tr("the page read-back could not be parsed"));
            return;
        }

        if(writeVerifyPageOnly)
        {
            const int page = m_writeJob.currentPage;
            const QMap<int, QByteArray> expected = writeExpected;
            QList<QByteArray> pages;
            QVector<Ntag::PageState> states;
            const int count = qMax(m_pages, page + 1);
            for(int i = 0; i < count; i++)
                pages.append(QByteArray(4, '\0'));
            states.fill(Ntag::PageState::Unknown, count);
            pages[page] = QByteArray::fromHex(actual.toLatin1());
            states[page] = Ntag::PageState::Read;
            Ntag::MemoryPage *entry = m_mem.page(page);
            if(entry)
            {
                entry->original = pages.at(page);
                entry->verified = pages.at(page) == expected.value(page);
                updateRow(page);
            }
            finishWriteJob(false, tr("Page %1 verification finished.").arg(page));
            Ntag::VerifyReport report = Ntag::verifyWrite(m_cap, expected, pages, states);
            showVerifyReport(report, writeTitle,
                             tr("Only the selected page is compared with its edited value."));
            return;
        }

        const QByteArray wanted = writeSequence.isEmpty()
                ? writePlanned.value(m_writeJob.currentPage)
                : writeSequence.at(writeIndex).second;
        if(QByteArray::fromHex(actual.toLatin1()) != wanted)
        {
            writeAttemptFailed(tr("verification differs (wrote %1, read %2)")
                               .arg(writePageHex, actual));
            return;
        }
        commitWrittenPage(actual);
        return;
    }

    if(m_writeJob.phase == Ntag::WriteJobPhase::Verifying)
    {
        const QString path = dumpPath();
        QByteArray bytes;
        QFile file(path);
        if(success && !timedOut && file.exists() && file.open(QIODevice::ReadOnly))
        {
            bytes = file.readAll();
            file.close();
        }
        m_dumpWorkspace.clearDump();
        const Ntag::Dump dump = Ntag::parseDump(bytes);
        if(!success || timedOut || !dump.valid || dump.pages.isEmpty())
        {
            const bool cancelled = m_writeJob.cancelled;
            finishWriteJob(cancelled,
                           tr("The tag could not be read back, so the result is unknown."),
                           true);
            QMessageBox::warning(this, writeTitle,
                                 tr("The tag could not be read back. The result "
                                    "cannot be verified."));
            return;
        }

        QList<QByteArray> pages;
        QVector<Ntag::PageState> states;
        for(int i = 0; i < m_pages; i++)
            pages.append(QByteArray(4, '\0'));
        states.fill(Ntag::PageState::Unknown, m_pages);
        for(int i = 0; i < m_pages && i < dump.pages.size(); i++)
        {
            pages[i] = dump.pages.at(i);
            states[i] = Ntag::PageState::Read;
        }

        if(writeEraseFlow)
        {
            const bool cancelled = m_writeJob.cancelled;
            finishWriteJob(cancelled,
                           cancelled ? tr("Erase verification completed after cancel.")
                                     : tr("Erase finished."),
                           false);
            showEraseReport(pages, states, true);
            return;
        }

        const QMap<int, QByteArray> expected = writeExpected;
        const bool cancelled = m_writeJob.cancelled;

        if(writeQuickFlow)
        {
            const QByteArray quickArea = writeQuickArea;
            const QString description = writeQuickDescription;
            const QStringList warnings = writeQuickWarnings;
            const int firstPage = writeQuickFirstPage;
            const int lastPage = writeQuickLastPage;

            QByteArray area;
            bool complete = firstPage >= 0 && lastPage >= firstPage;
            for(int page = firstPage; complete && page <= lastPage; page++)
            {
                if(page >= states.size() || states.at(page) != Ntag::PageState::Read)
                {
                    complete = false;
                    break;
                }
                area += pages.at(page);
            }

            const bool rawMatch = complete && area == quickArea;
            const Ntag::NdefResult parsed = complete ? Ntag::parseNdef(area)
                                                     : Ntag::NdefResult();

            m_lastWriteTargets = expected;
            const Ntag::VerifyReport pageReport = Ntag::verifyWrite(m_cap, expected, pages, states);
            for(const Ntag::PageCheck &check : pageReport.checks)
            {
                if(check.actual.size() != 4)
                    continue;
                Ntag::MemoryPage *entry = m_mem.page(check.page);
                if(!entry)
                    continue;
                entry->original = check.actual;
                entry->verified = check.verdict == Ntag::PageVerdict::Match;
                if(entry->verified)
                    entry->writeState = Ntag::WriteState::Written;
                updateRow(check.page);
            }
            updateCounts();
            updateDirtyButtons();

            const QString result = rawMatch
                    ? tr("Write verified: the tag reads back the exact NDEF bytes.")
                    : tr("Verification FAILED: the NDEF bytes read back differ from "
                         "the data written.");

            QStringList detail;
            detail << tr("Record: %1").arg(description);
            detail << tr("Pages: %1..%2 (%3 bytes)").arg(firstPage).arg(lastPage)
                      .arg(quickArea.size());
            if(parsed.ok && !parsed.records.isEmpty())
            {
                const Ntag::NdefRecord &record = parsed.records.first();
                QString readBack = record.typeName;
                if(!record.uri.isEmpty())
                    readBack += QStringLiteral(" / ") + record.uri;
                else if(!record.text.isEmpty())
                    readBack += QStringLiteral(" / ") + record.text;
                detail << tr("Parsed read-back: %1").arg(readBack);
            }
            else
            {
                detail << tr("Parsed read-back: %1 (raw comparison is authoritative)")
                          .arg(parsed.error.isEmpty() ? tr("no NDEF record") : parsed.error);
            }
            if(!warnings.isEmpty())
                detail << tr("Warnings:\n%1").arg(warnings.join(QLatin1Char('\n')));
            detail << tr("Pages written and read back: %1").arg(pageReport.writablePassed);
            detail << tr("The TLV length was written as 0 first and as %1 last.")
                      .arg(quickArea.size() > 1
                           ? (quickArea.at(1) == static_cast<char>(0xFF)
                              ? (static_cast<quint8>(quickArea.at(2)) << 8)
                                | static_cast<quint8>(quickArea.at(3))
                              : static_cast<quint8>(quickArea.at(1)))
                           : 0);
            detail << tr("Capability Container and system pages were not modified.");

            finishWriteJob(cancelled,
                           cancelled
                           ? (rawMatch
                              ? tr("Quick Launch write cancelled after the final length; "
                                   "the data is complete and verified.")
                              : tr("Quick Launch write cancelled; the tag may hold a TLV "
                                   "with length 0."))
                           : (rawMatch ? tr("Quick Launch written and verified: %1")
                                         .arg(description)
                                       : result),
                           !rawMatch);

            QMessageBox report(this);
            report.setWindowTitle(tr("Quick Launch"));
            report.setText(result);
            report.setDetailedText(detail.join(QLatin1Char('\n')));
            report.setIcon(rawMatch ? QMessageBox::Information : QMessageBox::Warning);
            report.exec();
            return;
        }

        finishWriteJob(cancelled,
                       cancelled
                       ? tr("Write verification completed after cancel: %1 page(s).")
                         .arg(expected.size())
                       : tr("Write finished: %1 page(s) verified.").arg(expected.size()));
        showWriteReport(expected, pages, states, writeTitle);
        return;
    }
}

void NtagTab::showWriteReport(const QMap<int, QByteArray> &expected,
                              const QList<QByteArray> &pages,
                              const QVector<Ntag::PageState> &states,
                              const QString &title)
{
    m_lastWriteTargets = expected;
    const Ntag::VerifyReport report = Ntag::verifyWrite(m_cap, expected, pages, states);
    for(const Ntag::PageCheck &check : report.checks)
    {
        if(check.actual.size() != 4)
            continue;
        Ntag::MemoryPage *entry = m_mem.page(check.page);
        if(!entry)
            continue;
        entry->original = check.actual;
        entry->verified = check.verdict == Ntag::PageVerdict::Match;
        if(entry->verified)
            entry->writeState = Ntag::WriteState::Written;
        updateRow(check.page);
    }
    updateCounts();
    updateDirtyButtons();
    showVerifyReport(report, title,
                     tr("Only pages targeted by the write are compared. "
                        "Manufacturer, UID, lock, configuration and security pages "
                        "cannot cause a failure."));
}

void NtagTab::showEraseReport(const QList<QByteArray> &after,
                              const QVector<Ntag::PageState> &states, bool readOk)
{
    for(int page = 0; page < m_pages; page++)
    {
        if(page < states.size() && states.at(page) == Ntag::PageState::Read)
            setRowData(page, QString::fromLatin1(after.at(page).toHex(' ')).toUpper());
    }
    fillNdef();
    refreshCcFromTable();
    updateCounts();
    updateLockState();
    updateDirtyButtons();
    updateDetail();

    int cleared = 0;
    int remaining = 0;
    int unread = 0;
    QStringList remainingPages;
    for(int page : m_erasePlan.clearPages)
    {
        if(!readOk || page >= states.size() || states.at(page) != Ntag::PageState::Read)
        {
            unread++;
            continue;
        }
        if(after.at(page) == QByteArray(4, '\0'))
            cleared++;
        else
        {
            remaining++;
            remainingPages << tr("page %1: %2").arg(page)
                              .arg(QString::fromLatin1(after.at(page).toHex(' ')).toUpper());
        }
    }

    const int total = m_erasePlan.clearPages.size();
    QString result;
    if(!readOk)
        result = tr("Unknown: the tag could not be read back, so the erase result "
                    "cannot be confirmed.");
    else if(remaining == 0 && unread == 0)
        result = tr("Fully erased: all %1 targeted user page(s) now read 00 00 00 00.")
                .arg(total);
    else if(remaining == 0)
        result = tr("Unverified: %1 targeted page(s) were cleared but %2 page(s) "
                    "could not be read back.").arg(cleared).arg(unread);
    else if(cleared == 0)
        result = tr("Erase failed: none of the %1 targeted user page(s) could be "
                    "cleared.").arg(total);
    else
        result = tr("Partially erased: %1 of %2 targeted page(s) were cleared, "
                    "%3 still hold data and %4 could not be read.")
                .arg(cleared).arg(total).arg(remaining).arg(unread);

    auto rangeText = [](int first, int last) {
        return first == last ? QString::number(first)
                             : QStringLiteral("%1..%2").arg(first).arg(last);
    };
    auto pageList = [&rangeText](const QList<int> &pages) {
        if(pages.size() <= 24)
        {
            QStringList values;
            for(int page : pages)
                values << QString::number(page);
            return values.join(QStringLiteral(", "));
        }
        return rangeText(pages.first(), pages.last());
    };
    const QString targetText = pageList(m_erasePlan.clearPages);
    QStringList preserved;
    preserved << rangeText(0, 3) + tr(" (UID / static lock / CC)");
    if(m_cap.lockPage >= 0)
        preserved << tr("dynamic lock page %1").arg(m_cap.lockPage);
    if(m_cap.cfg0 >= 0 || m_cap.cfg1 >= 0)
        preserved << tr("configuration pages %1").arg(rangeText(m_cap.cfg0, m_cap.cfg1));
    if(m_cap.pwd >= 0 || m_cap.pack >= 0)
        preserved << tr("PWD/PACK pages %1").arg(rangeText(m_cap.pwd, m_cap.pack));
    const QString lockWarning = m_erasePlan.lockBitsPresent
            ? tr("Lock bits are already set. Some user pages may be permanently "
                 "write-protected; they will be reported as not cleared and will "
                 "not be bypassed.")
            : tr("No lock bits are set in the current read.");

    QStringList details;
    details << tr("Model: %1   UID: %2")
               .arg(Ntag::modelName(m_cap.model)).arg(uidLabel->text());
    details << tr("Target pages: %1 (%2)").arg(targetText).arg(total);
    details << tr("Cleared: %1   Still holding data: %2   Not readable: %3")
               .arg(cleared).arg(remaining).arg(unread);
    if(!m_writeFailedPages.isEmpty())
        details << tr("Write commands failed on: %1")
                   .arg(m_writeFailedPages.join(QStringLiteral(", ")));
    if(!remainingPages.isEmpty())
        details << tr("Not cleared (locked or write-protected):\n  %1")
                   .arg(remainingPages.join(QStringLiteral("\n  ")));
    details << tr("Preserved pages: %1").arg(preserved.join(QStringLiteral(", ")));
    details << lockWarning;
    details << tr("The Capability Container was not modified; an NDEF TLV is no "
                  "longer present because the user pages are blank.");

    QMessageBox report(this);
    report.setWindowTitle(tr("Erase NTAG"));
    report.setText(result);
    report.setDetailedText(details.join(QLatin1Char('\n')));
    report.setIcon((cleared == total) ? QMessageBox::Information : QMessageBox::Warning);
    report.exec();
    setStatus(result, cleared != total);
}

void NtagTab::quickTypeChanged()
{
    const Ndef::LaunchType type = static_cast<Ndef::LaunchType>(
                quickType->currentData().toInt());
    bool subject = false;
    bool body = false;
    bool extra = false;
    bool primary = true;
    QString primaryLabel = tr("URL:");
    QString primaryHint = tr("https://example.com");
    QString subjectLabel = tr("Subject:");
    QString subjectHint;
    QString bodyLabel = tr("Body:");
    QString bodyHint;
    QString extraLabel = tr("Extra:");
    QString extraHint;
    QString info;

    switch(type)
    {
    case Ndef::LaunchType::Phone:
        primaryLabel = tr("Phone number:");
        primaryHint = tr("+1 555 0100");
        info = tr("Writes a tel: URI. Most phones open the dialer without placing the call.");
        break;
    case Ndef::LaunchType::Sms:
        primaryLabel = tr("Phone number:");
        primaryHint = tr("+1 555 0100");
        body = true;
        bodyLabel = tr("Message:");
        bodyHint = tr("Optional message body");
        info = tr("Writes an sms: URI with an optional UTF-8 percent-encoded body.");
        break;
    case Ndef::LaunchType::Email:
        primaryLabel = tr("E-mail address:");
        primaryHint = tr("name@example.com");
        subject = true;
        body = true;
        bodyLabel = tr("Message:");
        bodyHint = tr("Optional message body");
        info = tr("Writes a mailto: URI; the mail app opens without sending automatically.");
        break;
    case Ndef::LaunchType::CustomUri:
        primaryLabel = tr("URI:");
        primaryHint = tr("https:// or another scheme:");
        info = tr("Any URI with a scheme is accepted and preserved as entered.");
        break;
    case Ndef::LaunchType::Text:
        primaryLabel = tr("Text:");
        primaryHint = tr("UTF-8 text");
        subject = true;
        subjectLabel = tr("Language:");
        subjectHint = tr("en");
        info = tr("Writes a Text record (TNF 1, type T) with the UTF-8 flag and language code.");
        break;
    case Ndef::LaunchType::Mime:
        primaryLabel = tr("MIME type:");
        primaryHint = tr("text/vcard");
        body = true;
        bodyLabel = tr("Payload text:");
        bodyHint = tr("UTF-8 payload");
        extra = true;
        extraLabel = tr("Payload hex (optional):");
        extraHint = tr("Hex bytes override the text payload");
        info = tr("Writes a MIME record (TNF 2). Non-ASCII MIME types are preserved with a warning.");
        break;
    case Ndef::LaunchType::VCard:
        primaryLabel = tr("Name:");
        primaryHint = tr("Contact name");
        subject = true;
        subjectLabel = tr("Organization:");
        subjectHint = tr("Company");
        body = true;
        bodyLabel = tr("Phone:");
        bodyHint = tr("+1 555 0100");
        extra = true;
        extraLabel = tr("E-mail:");
        extraHint = tr("name@example.com");
        info = tr("Writes a vCard 3.0 MIME record with CRLF line endings.");
        break;
    case Ndef::LaunchType::Wifi:
        primaryLabel = tr("SSID:");
        primaryHint = tr("Network name");
        subject = true;
        subjectLabel = tr("Password:");
        subjectHint = tr("WPA2 password (empty for OPEN)");
        body = true;
        bodyLabel = tr("Auth:");
        bodyHint = tr("WPA2 / WPA / OPEN");
        info = tr("Writes a Wi-Fi WSC MIME record (application/vnd.wfa.wsc).");
        break;
    case Ndef::LaunchType::Bluetooth:
        primaryLabel = tr("Bluetooth address:");
        primaryHint = tr("AA:BB:CC:DD:EE:FF");
        info = tr("Writes a Bluetooth OOB MIME record; the address is stored little-endian.");
        break;
    case Ndef::LaunchType::OpenPrintTag:
        primary = false;
        body = true;
        bodyLabel = tr("64-byte payload (hex):");
        bodyHint = tr("Exactly 64 bytes");
        info = tr("Writes an OpenPrintTag MIME record with the exact 64-byte payload.");
        break;
    case Ndef::LaunchType::External:
        primaryLabel = tr("External type:");
        primaryHint = tr("android.com:pkg");
        body = true;
        bodyLabel = tr("Payload text:");
        bodyHint = tr("UTF-8 payload");
        extra = true;
        extraLabel = tr("Payload hex (optional):");
        extraHint = tr("Hex bytes override the text payload");
        info = tr("Writes a TNF 4 External record. android.com:pkg is an Android Application Record.");
        break;
    default:
        info = tr("Writes an https/http URI record. Browser behavior depends on the phone.");
        break;
    }

    quickPrimaryLabel->setText(primaryLabel);
    quickPrimary->setPlaceholderText(primaryHint);
    quickSubjectLabel->setText(subjectLabel);
    quickSubject->setPlaceholderText(subjectHint);
    quickBodyLabel->setText(bodyLabel);
    quickBody->setPlaceholderText(bodyHint);
    quickExtraLabel->setText(extraLabel);
    quickExtra->setPlaceholderText(extraHint);

    quickPrimary->setEnabled(primary);
    quickSubjectLabel->setVisible(subject);
    quickSubject->setVisible(subject);
    quickBodyLabel->setVisible(body);
    quickBody->setVisible(body);
    quickExtraLabel->setVisible(extra);
    quickExtra->setVisible(extra);
    quickInfoLabel->setText(info);
    updateQuickPreview();
}

void NtagTab::updateQuickPreview()
{
    if(!quickPreview)
        return;
    Ndef::LaunchRequest request;
    request.type = static_cast<Ndef::LaunchType>(quickType->currentData().toInt());
    request.primary = quickPrimary->text();
    request.subject = quickSubject->text();
    request.body = quickBody->text();
    request.extra = quickExtra->text();

    const Ndef::LaunchPayload payload = Ndef::buildLaunch(request);
    const int capacity = availableNdefCapacity();
    const bool fits = payload.ok && capacity > 0 && payload.requiredBytes <= capacity;

    QStringList lines;
    if(!payload.ok)
    {
        lines << tr("Cannot build the NDEF data: %1").arg(payload.error);
    }
    else
    {
        if(!payload.summary.isEmpty())
            lines << tr("Record: %1").arg(payload.summary);
        if(!payload.uri.isEmpty())
            lines << tr("URI: %1").arg(payload.uri);
        for(const QString &warning : payload.warnings)
            lines << tr("Warning: %1").arg(warning);
        lines << tr("NDEF message: %1 bytes").arg(payload.message.size());
        lines << tr("Required space (TLV + terminator): %1 bytes").arg(payload.requiredBytes);
        lines << tr("Available NDEF space: %1 bytes").arg(capacity > 0
                                                        ? QString::number(capacity)
                                                        : tr("unknown"));
        lines << tr("Capacity used: %1%")
                 .arg(capacity > 0 ? payload.requiredBytes * 100 / capacity : 0);
        lines << tr("Fits: %1").arg(fits ? tr("yes") : tr("no"));
        lines << QString();
        lines << tr("NDEF message (hex):");
        const QByteArray message = payload.message;
        for(int offset = 0; offset < message.size(); offset += 16)
        {
            QString hex;
            for(int i = 0; i < 16 && offset + i < message.size(); i++)
                hex += QStringLiteral("%1 ")
                        .arg(static_cast<quint8>(message.at(offset + i)),
                             2, 16, QLatin1Char('0')).toUpper();
            lines << hex.trimmed();
        }
        lines << QString();
        lines << tr("TLV + terminator, padded to pages:");
        for(int offset = 0; offset < payload.userArea.size(); offset += 16)
        {
            QString hex;
            for(int i = 0; i < 16 && offset + i < payload.userArea.size(); i++)
                hex += QStringLiteral("%1 ")
                        .arg(static_cast<quint8>(payload.userArea.at(offset + i)),
                             2, 16, QLatin1Char('0')).toUpper();
            lines << hex.trimmed();
        }
    }
    quickPreview->setPlainText(lines.join(QLatin1Char('\n')));

    if(m_cap.pages > 0)
        quickCapacityLabel->setText(tr("Model: %1, user pages 4..%2, NDEF capacity %3 bytes "
                                       "(Capability Container %4)")
                                    .arg(Ntag::modelName(m_cap.model))
                                    .arg(m_cap.userLast)
                                    .arg(capacity)
                                    .arg(m_cc.valid ? tr("read") : tr("not read")));
    else
        quickCapacityLabel->setText(tr("Detect and read a tag to calculate the capacity."));

    const bool writable = payload.ok && fits && m_knownLayout && m_cc.valid &&
            m_cc.readWrite && capacity > 0;
    const bool canSimulate = payload.ok && m_knownLayout && m_pages > 0 &&
            m_mem.readablePages().size() == m_pages && !m_simActive;
    quickWriteButton->setEnabled(writable && !m_simActive);
    quickSimButton->setEnabled(canSimulate);
    quickStatusLabel->setStyleSheet((writable || canSimulate) && payload.warnings.isEmpty()
                                    ? QString()
                                    : payload.ok && !payload.warnings.isEmpty()
                                      ? QStringLiteral("color:rgb(180,120,0);")
                                      : QStringLiteral("color:rgb(200,20,20);"));
    if(payload.ok && !payload.warnings.isEmpty())
        quickStatusLabel->setText(payload.warnings.join(QStringLiteral(" ")));
    else
        quickStatusLabel->setText(payload.ok
                                  ? (fits ? tr("Ready to write or simulate: the data fits the tag.")
                                          : tr("Over capacity: writing and simulation are disabled."))
                                  : tr("Cannot build the payload: writing and simulation are disabled."));
}

void NtagTab::writeQuickLaunch()
{
    if(m_simActive)
    {
        QMessageBox::warning(this, tr("Quick Launch"),
                             tr("Stop the simulation before writing to the tag."));
        return;
    }
    if(deviceBusy())
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return;
    }
    Ndef::LaunchRequest request;
    request.type = static_cast<Ndef::LaunchType>(quickType->currentData().toInt());
    request.primary = quickPrimary->text();
    request.subject = quickSubject->text();
    request.body = quickBody->text();
    request.extra = quickExtra->text();

    const Ndef::LaunchPayload payload = Ndef::buildLaunch(request);
    if(!payload.ok)
    {
        QMessageBox::warning(this, tr("Quick Launch"), payload.error);
        return;
    }
    if(!m_knownLayout || m_cap.model == Ntag::Model::Unknown || m_pages <= 0)
    {
        QMessageBox::warning(this, tr("Quick Launch"),
                             tr("Detect and read the tag first; the model and memory "
                                "layout must be known."));
        return;
    }
    if(!m_cc.valid)
    {
        QMessageBox::warning(this, tr("Quick Launch"),
                             tr("No valid Capability Container was read. Writing is "
                                "refused because the NDEF area is unknown."));
        return;
    }
    if(!m_cc.readWrite)
    {
        QMessageBox::warning(this, tr("Quick Launch"),
                             tr("The Capability Container marks the tag read-only."));
        return;
    }
    const int capacity = availableNdefCapacity();
    if(capacity <= 0 || payload.requiredBytes > capacity)
    {
        QMessageBox::warning(this, tr("Quick Launch"),
                             tr("The NDEF data needs %1 bytes but only %2 are "
                                "available.").arg(payload.requiredBytes).arg(capacity));
        return;
    }

    // Refresh the full read so stale UI data cannot decide writability.
    if(!confirmDiscardEdits(tr("Quick Launch reads the tag again before writing.")))
        return;
    m_undoStack.clear();
    m_redoStack.clear();
    if(!readAllFromDump())
    {
        QMessageBox::warning(this, tr("Quick Launch"),
                             tr("The tag could not be read before writing."));
        return;
    }
    refreshCcFromTable();
    if(!m_cc.valid || !m_cc.readWrite)
    {
        QMessageBox::warning(this, tr("Quick Launch"),
                             tr("The Capability Container read back from the tag is "
                                "missing or read-only. Nothing was written."));
        return;
    }
    const int freshCapacity = availableNdefCapacity();
    if(freshCapacity <= 0 || payload.requiredBytes > freshCapacity)
    {
        QMessageBox::warning(this, tr("Quick Launch"),
                             tr("The NDEF data needs %1 bytes but only %2 are "
                                "available.").arg(payload.requiredBytes).arg(freshCapacity));
        return;
    }

    const int firstPage = m_cap.userFirst;
    const int pageCount = payload.userArea.size() / 4;
    const int lastPage = firstPage + pageCount - 1;
    if(lastPage > m_cap.userLast)
    {
        QMessageBox::warning(this, tr("Quick Launch"),
                             tr("The data needs pages %1..%2 but the tag only allows "
                                "user pages up to %3.")
                             .arg(firstPage).arg(lastPage).arg(m_cap.userLast));
        return;
    }

    const QString staticLock = rowData(2).mid(4, 4);
    const QString dynamicLock = m_cap.lockPage >= 0
            ? rowData(m_cap.lockPage).left(6) : QString();
    if(staticLock.size() != 4 ||
       (m_cap.lockPage >= 0 && dynamicLock.size() != 6))
    {
        QMessageBox::warning(this, tr("Quick Launch"),
                             tr("The static/dynamic lock pages could not be read, so "
                                "write protection cannot be checked. Nothing was "
                                "written."));
        return;
    }
    const QByteArray lock0 = QByteArray::fromHex(staticLock.toLatin1());
    // Static lock bit 0 protects the CC page only.  Any other static lock bit
    // protects a user page, so a write is refused rather than bypassing it.
    const bool staticUserLocked = lock0.size() == 2 &&
            ((static_cast<quint8>(lock0.at(0)) & 0xFE) != 0 ||
             static_cast<quint8>(lock0.at(1)) != 0);
    bool dynamicUserLocked = false;
    if(lastPage >= 16)
    {
        for(char byte : QByteArray::fromHex(dynamicLock.toLatin1()))
            if(byte != '\0')
                dynamicUserLocked = true;
    }
    if(staticUserLocked || dynamicUserLocked)
    {
        QMessageBox::warning(this, tr("Quick Launch"),
                             tr("Lock bits protect the target user pages (static %1, "
                                "dynamic %2). The write was refused instead of "
                                "bypassing the lock. No page was modified.")
                             .arg(staticLock, dynamicLock));
        return;
    }

    QString details = tr("Model: %1\nUID: %2\nRecord: %3\nPages: %4..%5 (%6 bytes)")
            .arg(Ntag::modelName(m_cap.model))
            .arg(uidLabel->text())
            .arg(payload.summary)
            .arg(firstPage).arg(lastPage).arg(payload.requiredBytes);
    if(!payload.uri.isEmpty())
        details += tr("\nURI: %1").arg(payload.uri);
    if(!payload.warnings.isEmpty())
        details += tr("\nWarnings:\n%1").arg(payload.warnings.join(QLatin1Char('\n')));
    details += tr("\n\nOnly user data pages are written. The Capability Container, UID, "
                  "lock, OTP, configuration and PWD/PACK pages are not modified. The "
                  "TLV length is written as 0 first and as the final length last.");

    QMessageBox confirm(this);
    confirm.setIcon(QMessageBox::Warning);
    confirm.setWindowTitle(tr("Quick Launch"));
    confirm.setText(tr("Write this NDEF record to the tag?"));
    confirm.setInformativeText(details);
    confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    confirm.setDefaultButton(QMessageBox::No);
    if(confirm.exec() != QMessageBox::Yes)
    {
        setStatus(tr("Quick Launch write cancelled."));
        return;
    }

    startQuickLaunchWrite(payload);
}

void NtagTab::startQuickLaunchWrite(const Ndef::LaunchPayload &payload)
{
    if(deviceBusy())
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return;
    }
    if(!session)
    {
        setStatus(tr("No client session is available for the NDEF write."), true);
        return;
    }
    if(!m_dumpWorkspace.isValid())
    {
        setStatus(tr("No writable temporary directory is available for verification."), true);
        return;
    }

    const int firstPage = m_cap.userFirst;
    const int pageCount = payload.userArea.size() / 4;
    const int lastPage = firstPage + pageCount - 1;
    if(firstPage < 0 || lastPage > m_cap.userLast || pageCount <= 0)
    {
        setStatus(tr("The NDEF data does not fit the detected user area."), true);
        return;
    }

    QByteArray initialArea = payload.userArea;
    if(payload.message.size() < 255)
        initialArea[1] = 0; // 03 00: length zero while the rest is written
    else
    {
        initialArea[1] = static_cast<char>(0xFF);
        initialArea[2] = 0;
        initialArea[3] = 0;
    }

    QMap<int, QByteArray> finalExpected;
    for(int page = firstPage; page <= lastPage; page++)
        finalExpected.insert(page, payload.userArea.mid((page - firstPage) * 4, 4));

    writeSequence.clear();
    const QByteArray initialPage = initialArea.mid(0, 4);
    const QByteArray finalPage = payload.userArea.mid(0, 4);
    const Ntag::MemoryPage *firstEntry = m_mem.page(firstPage);
    QList<int> changedMiddle;
    for(int page = firstPage + 1; page <= lastPage; page++)
    {
        const Ntag::MemoryPage *entry = m_mem.page(page);
        const QByteArray value = payload.userArea.mid((page - firstPage) * 4, 4);
        if(!entry || entry->edited != value)
            changedMiddle.append(page);
    }
    const bool firstNeedsWrite = !firstEntry || firstEntry->edited != finalPage;
    if(firstNeedsWrite || !changedMiddle.isEmpty())
    {
        if(!firstEntry || firstEntry->edited != initialPage)
            writeSequence.append(qMakePair(firstPage, initialPage));
        for(int page : changedMiddle)
            writeSequence.append(qMakePair(page,
                    payload.userArea.mid((page - firstPage) * 4, 4)));
        writeSequence.append(qMakePair(firstPage, finalPage));
    }

    if(writeSequence.isEmpty())
    {
        setStatus(tr("The tag already contains these NDEF bytes; nothing was written."));
        fillNdef();
        return;
    }
    if(!m_writeJob.start(Ntag::WriteJobMode::WritePages, writeSequence.size()))
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return;
    }

    writeTargets.clear();
    for(const QPair<int, QByteArray> &step : writeSequence)
        if(!writeTargets.contains(step.first))
            writeTargets.append(step.first);
    std::sort(writeTargets.begin(), writeTargets.end());
    writeExpected = finalExpected;
    writePlanned = finalExpected;
    writeIndex = 0;
    writeAllowRetry = true;
    writeVerifyPageOnly = false;
    writeEraseFlow = false;
    writeQuickFlow = true;
    writeTitle = tr("Quick Launch");
    writePageHex.clear();
    writeQuickDescription = payload.summary;
    writeQuickWarnings = payload.warnings;
    writeQuickArea = payload.userArea;
    writeQuickFirstPage = firstPage;
    writeQuickLastPage = lastPage;
    m_lastWriteError.clear();
    m_lastWriteTargets.clear();
    m_writeFailedPages.clear();
    m_writeJobId++;
    m_writeJob.phase = Ntag::WriteJobPhase::Writing;

    Logger::info(QStringLiteral("NtagWrite"),
                 QStringLiteral("job #%1 quick-launch start pages=%2..%3 steps=%4")
                 .arg(m_writeJobId).arg(firstPage).arg(lastPage)
                 .arg(writeSequence.size()));
    setReadBusy(true);
    setStatus(tr("Writing NDEF page %1 with length 0 first...").arg(firstPage));
    issueNextWriteCommand();
}

void NtagTab::toggleQuickSimulation()
{
    if(m_simActive)
    {
        stopQuickSimulation();
        return;
    }
    if(deviceBusy())
    {
        setStatus(tr("Another NTAG operation is running; wait for it to finish."), true);
        return;
    }

    Ndef::LaunchRequest request;
    request.type = static_cast<Ndef::LaunchType>(quickType->currentData().toInt());
    request.primary = quickPrimary->text();
    request.subject = quickSubject->text();
    request.body = quickBody->text();
    request.extra = quickExtra->text();
    const Ndef::LaunchPayload payload = Ndef::buildLaunch(request);
    if(!payload.ok)
    {
        QMessageBox::warning(this, tr("Simulate"), payload.error);
        return;
    }
    if(!m_knownLayout || m_pages <= 0)
    {
        QMessageBox::warning(this, tr("Simulate"),
                             tr("Detect and read the tag first; the memory layout must be known."));
        return;
    }
    if(m_mem.readablePages().size() != m_pages)
    {
        QMessageBox::warning(this, tr("Simulate"),
                             tr("Read all pages first. The simulator dump must contain "
                                "the complete tag memory."));
        return;
    }
    const int capacity = availableNdefCapacity();
    if(capacity <= 0 || payload.requiredBytes > capacity)
    {
        QMessageBox::warning(this, tr("Simulate"),
                             tr("The NDEF data needs %1 bytes but only %2 are available.")
                             .arg(payload.requiredBytes).arg(capacity));
        return;
    }

    QString detail = tr("Model: %1\nRecord: %2\nPages: %3..%4")
            .arg(Ntag::modelName(m_cap.model))
            .arg(payload.summary)
            .arg(m_cap.userFirst)
            .arg(m_cap.userFirst + payload.userArea.size() / 4 - 1);
    if(!payload.warnings.isEmpty())
        detail += tr("\nWarnings:\n%1").arg(payload.warnings.join(QLatin1Char('\n')));
    detail += tr("\n\nThe tag is not written. A temporary dump is loaded into the "
                 "Proxmark3 emulator memory and `hf mfu sim -t 7` is started. "
                 "Stop restarts the client.");

    QMessageBox confirm(this);
    confirm.setIcon(QMessageBox::Question);
    confirm.setWindowTitle(tr("Simulate"));
    confirm.setText(tr("Start the NDEF simulator with these bytes?"));
    confirm.setInformativeText(detail);
    confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    confirm.setDefaultButton(QMessageBox::No);
    if(confirm.exec() != QMessageBox::Yes)
        return;

    startQuickSimulation(payload);
}

bool NtagTab::buildSimulationDump(const Ndef::LaunchPayload &payload,
                                  QByteArray *dump, QString *error)
{
    if(!dump)
        return false;
    dump->clear();
    if(error)
        error->clear();
    if(!m_knownLayout || m_pages <= 0)
    {
        if(error)
            *error = tr("the tag layout is unknown; read the tag first");
        return false;
    }
    if(payload.userArea.size() % 4 != 0)
    {
        if(error)
            *error = tr("the NDEF area is not page aligned");
        return false;
    }

    QList<QByteArray> pages;
    for(int page = 0; page < m_pages; page++)
    {
        const Ntag::MemoryPage *entry = m_mem.page(page);
        if(!entry || !entry->readable())
        {
            if(error)
                *error = tr("page %1 has not been read").arg(page);
            return false;
        }
        pages.append(entry->edited);
    }

    const int firstPage = m_cap.userFirst;
    const int pageCount = payload.userArea.size() / 4;
    if(firstPage < 0 || firstPage + pageCount - 1 > m_cap.userLast)
    {
        if(error)
            *error = tr("the NDEF area is outside the detected user pages");
        return false;
    }
    for(int i = 0; i < pageCount; i++)
        pages[firstPage + i] = payload.userArea.mid(i * 4, 4);

    *dump = Ntag::buildDump(dumpVersion(), pages);
    if(dump->isEmpty())
    {
        if(error)
            *error = tr("cannot build a simulator dump for this tag");
        return false;
    }
    return true;
}

void NtagTab::startQuickSimulation(const Ndef::LaunchPayload &payload)
{
    if(!session)
    {
        setStatus(tr("No client session is available for simulation."), true);
        return;
    }
    if(!m_dumpWorkspace.isValid())
    {
        setStatus(tr("No writable temporary directory is available for simulation."), true);
        return;
    }

    QByteArray dump;
    QString error;
    if(!buildSimulationDump(payload, &dump, &error))
    {
        QMessageBox::warning(this, tr("Simulate"),
                             tr("Cannot build the simulator dump: %1").arg(error));
        return;
    }
    if(!m_dumpWorkspace.writeSimDump(dump, &error))
    {
        QMessageBox::warning(this, tr("Simulate"), error);
        m_dumpWorkspace.clearDump();
        return;
    }

    setStatus(tr("Loading the NDEF bytes into emulator memory..."));
    const QString path = QDir::toNativeSeparators(m_dumpWorkspace.simPath());
    const QString loadCommand = QStringLiteral("hf mfu eload -f \"%1\" -q %2")
            .arg(path).arg(m_pages);
    const QString loadOutput = util->execCMDWithOutput(loadCommand, 30000, true);
    if(loadOutput.isEmpty() || outputLooksUnsupported(loadOutput)
            || loadOutput.contains(QStringLiteral("fail"), Qt::CaseInsensitive)
            || loadOutput.contains(QStringLiteral("error"), Qt::CaseInsensitive)
            || loadOutput.contains(QStringLiteral("not available"), Qt::CaseInsensitive)
            || loadOutput.contains(QStringLiteral("timeout"), Qt::CaseInsensitive))
    {
        m_dumpWorkspace.clearDump();
        const QString message = outputLooksUnsupported(loadOutput)
                ? tr("This firmware does not support `hf mfu eload` for the Ultralight "
                     "dump format. Simulation was not started; the tag was not written.")
                : tr("Loading the simulator dump failed. Simulation was not started; "
                     "the tag was not written.");
        setStatus(message, true);
        Logger::warning(QStringLiteral("NtagSim"), message);
        QMessageBox::warning(this, tr("Simulate"),
                             message + QStringLiteral("\n\n") + loadOutput.left(1200));
        return;
    }

    // Set the state before the detached command so immediate client output is
    // attributed to this simulation and can be reported instead of ignored.
    m_simActive = true;
    if(!session->executeDetached(QStringLiteral("hf mfu sim -t 7")))
    {
        m_simActive = false;
        m_dumpWorkspace.clearDump();
        setStatus(tr("The client refused to start the simulation. The tag was not written."),
                  true);
        return;
    }

    setSimulationUiActive(true);
    setStatus(tr("Simulation running (hf mfu sim -t 7). The tag is not written. "
                 "Hold a phone near the antenna, then press Stop Simulation."));
    emit simulationStateChanged(true);
    Logger::info(QStringLiteral("NtagSim"),
                 QStringLiteral("simulation started pages=%1 dump=%2")
                 .arg(m_pages).arg(path));
}

void NtagTab::stopQuickSimulation(const QString &message)
{
    if(!m_simActive)
        return;
    m_simActive = false;
    m_dumpWorkspace.clearDump();
    setSimulationUiActive(false);
    setStatus(message.isEmpty() ? tr("Simulation stopped.") : message);
    emit simulationStateChanged(false);
    Logger::info(QStringLiteral("NtagSim"), QStringLiteral("simulation stop requested"));
    emit stopSimulationRequested();
}

void NtagTab::setSimulationUiActive(bool active)
{
    for(QPushButton *button : m_busyButtons)
    {
        if(!button || button == quickSimButton)
            continue;
        button->setEnabled(!active);
    }
    if(cancelReadButton)
        cancelReadButton->setEnabled(false);
    if(quickType)
        quickType->setEnabled(!active);
    if(quickPrimary)
        quickPrimary->setEnabled(!active);
    if(quickSubject)
        quickSubject->setEnabled(!active);
    if(quickBody)
        quickBody->setEnabled(!active);
    if(quickExtra)
        quickExtra->setEnabled(!active);
    if(quickSimButton)
    {
        quickSimButton->setEnabled(true);
        quickSimButton->setText(active ? tr("Stop Simulation") : tr("Simulate"));
    }
    table->setEditTriggers(active ? QAbstractItemView::NoEditTriggers
                                  : (QAbstractItemView::DoubleClicked |
                                     QAbstractItemView::EditKeyPressed));
    if(!active)
    {
        updateDirtyButtons();
        quickTypeChanged();
    }
}

bool NtagTab::outputLooksUnsupported(const QString &output)
{
    static const QStringList markers = {
        QStringLiteral("unknown command"),
        QStringLiteral("invalid command"),
        QStringLiteral("command not found"),
        QStringLiteral("not supported"),
        QStringLiteral("not implemented"),
        QStringLiteral("unsupported")
    };
    for(const QString &marker : markers)
        if(output.contains(marker, Qt::CaseInsensitive))
            return true;
    return false;
}

void NtagTab::onSimulationOutput(const QString &text)
{
    if(!m_simActive || text.isEmpty())
        return;
    if(!outputLooksUnsupported(text))
        return;
    m_simActive = false;
    m_dumpWorkspace.clearDump();
    setSimulationUiActive(false);
    const QString message = tr("This firmware does not support `hf mfu sim` for NTAG "
                               "emulation. Simulation was stopped; the tag was not written.");
    setStatus(message, true);
    Logger::warning(QStringLiteral("NtagSim"), message + QStringLiteral(": ") + text.left(500));
    emit simulationStateChanged(false);
    emit stopSimulationRequested();
    QMessageBox::warning(this, tr("Simulate"), message);
}

void NtagTab::onSimulationDetachedChanged(bool detached)
{
    if(detached || !m_simActive)
        return;
    m_simActive = false;
    m_dumpWorkspace.clearDump();
    setSimulationUiActive(false);
    emit simulationStateChanged(false);
    setStatus(tr("Simulation ended: the client is no longer running the detached command."));
    Logger::info(QStringLiteral("NtagSim"), QStringLiteral("detached command ended"));
}
