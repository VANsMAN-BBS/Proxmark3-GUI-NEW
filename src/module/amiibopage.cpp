#include "amiibopage.h"
#include "common/util.h"
#include "core/logger.h"
#include "core/clientsession.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
const int NTAG215_SIZE = 540;

QString hexDump(const QByteArray &data, int limit)
{
    QStringList lines;
    const int count = qMin(data.size(), limit);
    for(int offset = 0; offset < count; offset += 16)
    {
        QString hex;
        QString text;
        for(int i = 0; i < 16 && offset + i < count; i++)
        {
            const unsigned char value = static_cast<unsigned char>(data.at(offset + i));
            hex += QStringLiteral("%1 ").arg(value, 2, 16, QLatin1Char('0')).toUpper();
            text += (value >= 0x20 && value < 0x7F) ? QChar(value) : QLatin1Char('.');
        }
        lines << QStringLiteral("%1  %2 %3")
                 .arg(offset, 4, 16, QLatin1Char('0'))
                 .arg(hex, -48)
                 .arg(text);
    }
    if(data.size() > limit)
        lines << QStringLiteral("... (%1 / %2 bytes)").arg(limit).arg(data.size());
    return lines.join(QLatin1Char('\n'));
}
}

AmiiboPage::AmiiboPage(Util *util, ClientSession *session, QWidget *parent)
    : QWidget(parent)
    , util(util)
    , session(session)
    , net(nullptr)
    , connected(false)
    , busy(false)
    , emulating(false)
{
    buildUi();
    net = new QNetworkAccessManager(this);
    connect(net, &QNetworkAccessManager::finished, this, &AmiiboPage::onCatalogReply);
    QSettings settings(QStringLiteral("GUIsettings.ini"), QSettings::IniFormat);
    catalogUrlEdit->setText(settings.value(QStringLiteral("Amiibo/catalogUrl")).toString());
    setClientConnected(false);
}

AmiiboPage::~AmiiboPage()
{
    entries.clear();
    catalogEntries.clear();
    selectedCatalogBin.clear();
}

void AmiiboPage::buildUi()
{
    QHBoxLayout *root = new QHBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    root->addWidget(splitter);

    // ---------------- library ----------------
    QGroupBox *libraryBox = new QGroupBox(tr("Amiibo data"), splitter);
    QVBoxLayout *libraryLayout = new QVBoxLayout(libraryBox);
    library = new QListWidget(libraryBox);
    connect(library, &QListWidget::currentRowChanged, this, &AmiiboPage::onEntrySelected);
    libraryLayout->addWidget(library, 1);

    QHBoxLayout *fileButtons = new QHBoxLayout();
    sampleButton = new QPushButton(tr("Add sample"), libraryBox);
    importButton = new QPushButton(tr("Import file..."), libraryBox);
    saveButton = new QPushButton(tr("Save as..."), libraryBox);
    removeButton = new QPushButton(tr("Remove"), libraryBox);
    connect(sampleButton, &QPushButton::clicked, this, &AmiiboPage::addSyntheticSample);
    connect(importButton, &QPushButton::clicked, this, &AmiiboPage::importFile);
    connect(saveButton, &QPushButton::clicked, this, &AmiiboPage::saveAsFile);
    connect(removeButton, &QPushButton::clicked, this, &AmiiboPage::removeEntry);
    fileButtons->addWidget(sampleButton);
    fileButtons->addWidget(importButton);
    fileButtons->addWidget(saveButton);
    fileButtons->addWidget(removeButton);
    libraryLayout->addLayout(fileButtons);

    QLabel *note = new QLabel(
        tr("The built-in sample is synthetic test data, not a real Amiibo. "
           "Import your own NTAG215 dump (540 bytes) to inspect or write it."), libraryBox);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: palette(mid);"));
    libraryLayout->addWidget(note);

    // ---------------- details ----------------
    QWidget *right = new QWidget(splitter);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);

    QGroupBox *detailsBox = new QGroupBox(tr("Selected data"), right);
    QVBoxLayout *detailsLayout = new QVBoxLayout(detailsBox);
    QFormLayout *form = new QFormLayout();
    nameLabel = new QLabel(tr("(none)"), detailsBox);
    sourceLabel = new QLabel(QString(), detailsBox);
    sizeLabel = new QLabel(QString(), detailsBox);
    formatLabel = new QLabel(QString(), detailsBox);
    form->addRow(tr("Name:"), nameLabel);
    form->addRow(tr("Source:"), sourceLabel);
    form->addRow(tr("Size:"), sizeLabel);
    form->addRow(tr("Format:"), formatLabel);
    detailsLayout->addLayout(form);

    QHBoxLayout *detailRow = new QHBoxLayout();
    hexView = new QPlainTextEdit(detailsBox);
    hexView->setReadOnly(true);
    hexView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    hexView->setPlaceholderText(tr("Hexadecimal preview of the selected data"));
    hexView->setMinimumHeight(150);
    detailRow->addWidget(hexView, 1);
    imageLabel = new QLabel(detailsBox);
    imageLabel->setFixedSize(160, 160);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setStyleSheet(QStringLiteral("border: 1px solid palette(mid);"));
    imageLabel->setText(tr("No image"));
    detailRow->addWidget(imageLabel);
    detailsLayout->addLayout(detailRow);

    QGroupBox *opsBox = new QGroupBox(tr("Tag operations (NTAG215 / Type 2)"), right);
    QVBoxLayout *opsLayout = new QVBoxLayout(opsBox);
    QHBoxLayout *opsRow = new QHBoxLayout();
    detectButton = new QPushButton(tr("Detect tag"), opsBox);
    readButton = new QPushButton(tr("Read tag"), opsBox);
    writeButton = new QPushButton(tr("Write tag..."), opsBox);
    verifyButton = new QPushButton(tr("Verify tag"), opsBox);
    emulateButton = new QPushButton(tr("Start emulation"), opsBox);
    stopButton = new QPushButton(tr("Stop emulation"), opsBox);
    connect(detectButton, &QPushButton::clicked, this, &AmiiboPage::detectTag);
    connect(readButton, &QPushButton::clicked, this, &AmiiboPage::readTag);
    connect(writeButton, &QPushButton::clicked, this, &AmiiboPage::writeTag);
    connect(verifyButton, &QPushButton::clicked, this, &AmiiboPage::verifyTag);
    connect(emulateButton, &QPushButton::clicked, this, &AmiiboPage::startEmulation);
    connect(stopButton, &QPushButton::clicked, this, &AmiiboPage::stopEmulation);
    opsRow->addWidget(detectButton);
    opsRow->addWidget(readButton);
    opsRow->addWidget(writeButton);
    opsRow->addWidget(verifyButton);
    opsRow->addWidget(emulateButton);
    opsRow->addWidget(stopButton);
    opsLayout->addLayout(opsRow);

    QLabel *limits = new QLabel(
        tr("Writing overwrites the tag and is only started after confirmation; "
           "the data is read back afterwards. Lock/OTP/password changes are never "
           "performed. Emulation uses the client emulator memory (hf mfu eload + "
           "hf mfu sim -t 7)."), opsBox);
    limits->setWordWrap(true);
    limits->setStyleSheet(QStringLiteral("color: palette(mid);"));
    opsLayout->addWidget(limits);

    statusLabel = new QLabel(tr("Idle"), opsBox);
    statusLabel->setWordWrap(true);
    opsLayout->addWidget(statusLabel);

    QGroupBox *catalogBox = new QGroupBox(tr("Online catalog (metadata)"), right);
    QVBoxLayout *catalogLayout = new QVBoxLayout(catalogBox);
    QHBoxLayout *urlRow = new QHBoxLayout();
    urlRow->addWidget(new QLabel(tr("Catalog URL:"), catalogBox));
    catalogUrlEdit = new QLineEdit(catalogBox);
    catalogUrlEdit->setPlaceholderText(tr("https://example.org/amiibo.json"));
    urlRow->addWidget(catalogUrlEdit, 1);
    catalogLayout->addLayout(urlRow);

    QHBoxLayout *searchRow = new QHBoxLayout();
    catalogSearchEdit = new QLineEdit(catalogBox);
    catalogSearchEdit->setPlaceholderText(tr("Search name"));
    catalogSearchButton = new QPushButton(tr("Search"), catalogBox);
    catalogDownloadButton = new QPushButton(tr("Download data"), catalogBox);
    catalogDownloadButton->setEnabled(false);
    connect(catalogSearchButton, &QPushButton::clicked, this, &AmiiboPage::searchCatalog);
    connect(catalogDownloadButton, &QPushButton::clicked, this, &AmiiboPage::downloadCatalogItem);
    searchRow->addWidget(catalogSearchEdit, 1);
    searchRow->addWidget(catalogSearchButton);
    searchRow->addWidget(catalogDownloadButton);
    catalogLayout->addLayout(searchRow);

    catalogList = new QListWidget(catalogBox);
    catalogList->setMaximumHeight(110);
    connect(catalogList, &QListWidget::currentRowChanged,
            this, &AmiiboPage::onCatalogSelectionChanged);
    catalogLayout->addWidget(catalogList);
    catalogInfo = new QLabel(
        tr("No catalog is configured. Metadata browsing is optional and network "
           "access only happens when you press Search. Binary Amiibo data is not "
           "bundled or distributed by this program."), catalogBox);
    catalogInfo->setWordWrap(true);
    catalogInfo->setStyleSheet(QStringLiteral("color: palette(mid);"));
    catalogLayout->addWidget(catalogInfo);

    rightLayout->addWidget(detailsBox, 2);
    rightLayout->addWidget(opsBox, 0);
    rightLayout->addWidget(catalogBox, 1);

    splitter->addWidget(libraryBox);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
}

void AmiiboPage::setDeviceInfo(const QString &port, const QString &clientPath,
                               const QString &workingDir)
{
    this->port = port;
    this->clientPath = clientPath;
    this->workingDir = workingDir;
}

void AmiiboPage::setClientConnected(bool connected)
{
    this->connected = connected;
    const bool idle = connected && !busy;
    detectButton->setEnabled(idle);
    readButton->setEnabled(idle);
    writeButton->setEnabled(idle);
    verifyButton->setEnabled(idle);
    emulateButton->setEnabled(idle);
    stopButton->setEnabled(emulating);
}

bool AmiiboPage::isEmulating() const
{
    return emulating;
}

QString AmiiboPage::tempPath(const QString &name) const
{
    QString dir = workingDir;
    if(dir.isEmpty())
        dir = QDir::tempPath();
    QDir().mkpath(dir);
    return QDir(dir).filePath(name);
}

void AmiiboPage::setStatus(const QString &text)
{
    statusLabel->setText(text);
}

void AmiiboPage::setBusy(bool busy, const QString &text)
{
    this->busy = busy;
    const bool idle = connected && !busy;
    detectButton->setEnabled(idle);
    readButton->setEnabled(idle);
    writeButton->setEnabled(idle);
    verifyButton->setEnabled(idle);
    emulateButton->setEnabled(idle);
    stopButton->setEnabled(emulating);
    sampleButton->setEnabled(!busy);
    importButton->setEnabled(!busy);
    if(!text.isEmpty())
        setStatus(text);
}

void AmiiboPage::addEntry(const AmiiboEntry &entry)
{
    entries.append(entry);
    refreshList();
    library->setCurrentRow(entries.size() - 1);
}

void AmiiboPage::refreshList()
{
    library->clear();
    for(int i = 0; i < entries.size(); i++)
    {
        const AmiiboEntry &entry = entries.at(i);
        QString text = entry.name;
        if(!entry.series.isEmpty())
            text += QStringLiteral(" [%1]").arg(entry.series);
        QListWidgetItem *item = new QListWidgetItem(text, library);
        item->setToolTip(tr("Source: %1").arg(entry.source));
    }
}

void AmiiboPage::onEntrySelected()
{
    showEntry(library->currentRow());
}

void AmiiboPage::showEntry(int index)
{
    if(index < 0 || index >= entries.size())
    {
        nameLabel->setText(tr("(none)"));
        sourceLabel->clear();
        sizeLabel->clear();
        formatLabel->clear();
        hexView->clear();
        return;
    }
    const AmiiboEntry &entry = entries.at(index);
    nameLabel->setText(entry.name);
    sourceLabel->setText(entry.source);
    sizeLabel->setText(tr("%1 bytes").arg(entry.data.size()));
    formatLabel->setText(formatOf(entry.data));
    hexView->setPlainText(hexDump(entry.data, 512));
    if(entry.imageUrl.isEmpty())
    {
        imageLabel->setPixmap(QPixmap());
        imageLabel->setText(tr("No image"));
    }
}

QString AmiiboPage::formatOf(const QByteArray &data)
{
    if(data.size() == NTAG215_SIZE)
        return AmiiboPage::tr("NTAG215 image (540 bytes)");
    if(data.size() == 1024)
        return AmiiboPage::tr("NTAG213-sized image (1024 bytes)");
    if(data.size() == 2048)
        return AmiiboPage::tr("NTAG216-sized image (2048 bytes)");
    return AmiiboPage::tr("Unknown (%1 bytes)").arg(data.size());
}

QString AmiiboPage::uidOf(const QByteArray &data)
{
    if(data.size() < 7)
        return QString();
    return QString::fromLatin1(data.left(7).toHex().toUpper());
}

bool AmiiboPage::isNtag215(const QByteArray &data)
{
    return data.size() == NTAG215_SIZE;
}

void AmiiboPage::addSyntheticSample()
{
    // A clearly synthetic NTAG215 image: UID + CC + deterministic payload.
    // It is not Amiibo data and carries no copyrighted content.
    QByteArray data(NTAG215_SIZE, 0);
    const unsigned char uid[7] = {0x04, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    for(int i = 0; i < 7; i++)
        data[i] = char(uid[i]);
    data[7] = char(0x88 ^ uid[0] ^ uid[1] ^ uid[2]);
    data[8] = 0x48;
    data[9] = 0x00;
    data[10] = 0x00;
    data[11] = char(0xE1);
    data[12] = 0x10;
    data[13] = 0x3F;
    data[14] = 0x00;
    for(int i = 16; i < data.size(); i++)
        data[i] = char((i * 7 + 3) & 0xFF);

    AmiiboEntry entry;
    entry.name = tr("Synthetic NTAG215 sample");
    entry.source = tr("built-in");
    entry.series = tr("synthetic");
    entry.data = data;
    addEntry(entry);
    setStatus(tr("Synthetic sample added (not a real Amiibo)."));
}

void AmiiboPage::importFile()
{
    const QString path = QFileDialog::getOpenFileName(
                this, tr("Import NTAG215 dump"), QDir::homePath(),
                tr("Dump files (*.bin *.dump);;All files (*)"));
    if(path.isEmpty())
        return;
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        QMessageBox::warning(this, tr("Amiibo"),
                             tr("Cannot open the file:\n%1").arg(path));
        return;
    }
    const QByteArray data = file.readAll();
    file.close();
    if(data.size() != NTAG215_SIZE && data.size() != 1024 && data.size() != 2048)
    {
        QMessageBox::warning(this, tr("Amiibo"),
                             tr("Unsupported data length: %1 bytes.\n"
                                "An NTAG215 dump must be 540 bytes.").arg(data.size()));
        return;
    }
    AmiiboEntry entry;
    entry.name = QFileInfo(path).completeBaseName();
    entry.source = tr("file");
    entry.path = path;
    entry.data = data;
    addEntry(entry);
    setStatus(tr("Loaded %1 (%2 bytes).").arg(QFileInfo(path).fileName()).arg(data.size()));
}

void AmiiboPage::saveAsFile()
{
    const int index = library->currentRow();
    if(index < 0 || index >= entries.size())
    {
        QMessageBox::information(this, tr("Amiibo"), tr("Select data first."));
        return;
    }
    const AmiiboEntry &entry = entries.at(index);
    const QString path = QFileDialog::getSaveFileName(
                this, tr("Save Amiibo data"),
                QDir::homePath() + QStringLiteral("/%1.bin").arg(entry.name),
                tr("Binary dump (*.bin)"));
    if(path.isEmpty())
        return;
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(entry.data) != entry.data.size())
    {
        QMessageBox::warning(this, tr("Amiibo"),
                             tr("Failed to save the file:\n%1").arg(path));
        return;
    }
    file.close();
    setStatus(tr("Saved %1.").arg(path));
}

void AmiiboPage::removeEntry()
{
    const int index = library->currentRow();
    if(index < 0 || index >= entries.size())
        return;
    entries.removeAt(index);
    refreshList();
    if(!entries.isEmpty())
        library->setCurrentRow(0);
    else
        showEntry(-1);
}

void AmiiboPage::detectTag()
{
    if(!connected)
        return;
    setBusy(true, tr("Detecting tag..."));
    const QString output = util->execCMDWithOutput(QStringLiteral("hf mfu info"), 25000);
    setBusy(false);
    QString type;
    if(output.contains(QStringLiteral("NTAG 215")))
        type = QStringLiteral("NTAG215");
    else if(output.contains(QStringLiteral("NTAG")))
        type = QStringLiteral("NTAG");
    else if(output.contains(QStringLiteral("Ultralight")))
        type = QStringLiteral("MIFARE Ultralight");
    if(type.isEmpty())
    {
        setStatus(tr("No NTAG/Ultralight tag detected."));
        QMessageBox::information(this, tr("Amiibo"),
                                 tr("No NTAG/Ultralight tag answered.\n"
                                    "Place the tag on the antenna and try again."));
        return;
    }
    setStatus(tr("Detected: %1").arg(type));
    statusLabel->setToolTip(output.right(600));
}

bool AmiiboPage::readBackInto(QByteArray &out, const QString &why)
{
    const QString path = tempPath(QStringLiteral("pm3gui_amiibo_verify.bin"));
    QFile::remove(path);
    const QString output = util->execCMDWithOutput(
                QStringLiteral("hf mfu dump -f %1").arg(path), 120000);
    QFile file(path);
    if(!file.exists() || !file.open(QIODevice::ReadOnly))
    {
        Logger::warning(QStringLiteral("Amiibo"),
                        QStringLiteral("%1: dump produced no file").arg(why));
        setStatus(tr("Read-back failed."));
        return false;
    }
    out = file.readAll();
    file.close();
    QFile::remove(path);
    return true;
}

void AmiiboPage::readTag()
{
    if(!connected)
        return;
    setBusy(true, tr("Reading tag..."));
    QByteArray data;
    const bool ok = readBackInto(data, QStringLiteral("read"));
    setBusy(false);
    if(!ok || data.isEmpty())
    {
        QMessageBox::warning(this, tr("Amiibo"),
                             tr("The tag could not be read. Check that it is an "
                                "NTAG215 and stays on the antenna."));
        return;
    }
    AmiiboEntry entry;
    entry.name = tr("Tag %1").arg(uidOf(data));
    entry.source = tr("tag");
    entry.data = data;
    addEntry(entry);
    setStatus(tr("Read %1 bytes from the tag.").arg(data.size()));
}

void AmiiboPage::writeTag()
{
    if(!connected)
        return;
    const int index = library->currentRow();
    if(index < 0 || index >= entries.size())
    {
        QMessageBox::information(this, tr("Amiibo"), tr("Select data first."));
        return;
    }
    const AmiiboEntry entry = entries.at(index);
    if(!isNtag215(entry.data))
    {
        QMessageBox::warning(this, tr("Amiibo"),
                             tr("Writing requires a 540-byte NTAG215 image."));
        return;
    }
    const QMessageBox::StandardButton choice = QMessageBox::warning(
                this, tr("Write tag"),
                tr("This overwrites the NTAG215 tag with:\n%1 (%2 bytes).\n\n"
                   "Amiibo tags are usually locked and only accept writes on "
                   "rewritable (\"magic\") NTAG215 tags. Continue?")
                .arg(entry.name).arg(entry.data.size()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if(choice != QMessageBox::Yes)
        return;

    const QString path = tempPath(QStringLiteral("pm3gui_amiibo_write.bin"));
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(entry.data) != entry.data.size())
    {
        QMessageBox::warning(this, tr("Amiibo"), tr("Cannot write a temporary file."));
        return;
    }
    file.close();

    setBusy(true, tr("Writing tag..."));
    const QString output = util->execCMDWithOutput(
                QStringLiteral("hf mfu restore -f %1").arg(path), 180000);
    QFile::remove(path);
    setBusy(false);
    Logger::info(QStringLiteral("Amiibo"), QStringLiteral("restore command finished"));

    QByteArray check;
    if(!readBackInto(check, QStringLiteral("verify")))
    {
        QMessageBox::warning(this, tr("Amiibo"),
                             tr("Write finished but the read-back failed, so the "
                                "result cannot be verified."));
        return;
    }
    if(check == entry.data)
    {
        setStatus(tr("Write verified: the tag matches the data."));
        QMessageBox::information(this, tr("Amiibo"),
                                 tr("Write succeeded and was verified by reading "
                                    "the tag back."));
    }
    else
    {
        setStatus(tr("Write verification FAILED."));
        QMessageBox::warning(this, tr("Amiibo"),
                             tr("The tag does not match the written data "
                                "(read %1 bytes).").arg(check.size()));
        statusLabel->setToolTip(output.right(600));
    }
}

void AmiiboPage::verifyTag()
{
    if(!connected)
        return;
    const int index = library->currentRow();
    if(index < 0 || index >= entries.size())
    {
        QMessageBox::information(this, tr("Amiibo"), tr("Select data first."));
        return;
    }
    setBusy(true, tr("Reading the tag for verification..."));
    QByteArray data;
    const bool ok = readBackInto(data, QStringLiteral("verify"));
    setBusy(false);
    if(!ok)
    {
        QMessageBox::warning(this, tr("Amiibo"), tr("The tag could not be read."));
        return;
    }
    const QByteArray &expected = entries.at(index).data;
    if(data == expected)
    {
        setStatus(tr("Verification OK: tag matches the selected data."));
        QMessageBox::information(this, tr("Amiibo"), tr("The tag matches the data."));
        return;
    }
    int diff = -1;
    const int limit = qMin(data.size(), expected.size());
    for(int i = 0; i < limit; i++)
    {
        if(data.at(i) != expected.at(i))
        {
            diff = i;
            break;
        }
    }
    setStatus(tr("Verification FAILED (first difference at %1).").arg(diff));
    QMessageBox::warning(this, tr("Amiibo"),
                         tr("The tag differs from the data (first difference at "
                            "byte %1; read %2 bytes).").arg(diff).arg(data.size()));
}

void AmiiboPage::startEmulation()
{
    if(!connected)
        return;
    const int index = library->currentRow();
    if(index < 0 || index >= entries.size())
    {
        QMessageBox::information(this, tr("Amiibo"), tr("Select data first."));
        return;
    }
    const AmiiboEntry &entry = entries.at(index);
    if(!isNtag215(entry.data))
    {
        QMessageBox::warning(this, tr("Amiibo"),
                             tr("Emulation requires a 540-byte NTAG215 image."));
        return;
    }
    const QString path = tempPath(QStringLiteral("pm3gui_amiibo_sim.bin"));
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly) || file.write(entry.data) != entry.data.size())
    {
        QMessageBox::warning(this, tr("Amiibo"), tr("Cannot write a temporary file."));
        return;
    }
    file.close();

    setBusy(true, tr("Loading emulator memory..."));
    util->execCMDWithOutput(QStringLiteral("hf mfu eload -f %1").arg(path), 30000);
    QFile::remove(path);
    if(!session || !session->executeDetached(QStringLiteral("hf mfu sim -t 7")))
    {
        setBusy(false);
        QMessageBox::warning(this, tr("Amiibo"),
                             tr("The client refused to start the simulation. "
                                "Press Stop and retry."));
        return;
    }
    emulating = true;
    setStatus(tr("Emulating NTAG215 from the selected data. Press Stop to end."));
    stopButton->setEnabled(true);
    setBusy(false);
    detectButton->setEnabled(false);
    readButton->setEnabled(false);
    writeButton->setEnabled(false);
    verifyButton->setEnabled(false);
    emulateButton->setEnabled(false);
    stopButton->setEnabled(true);
    Logger::info(QStringLiteral("Amiibo"), QStringLiteral("emulation started"));
}

void AmiiboPage::stopEmulation()
{
    if(!emulating)
        return;
    emulating = false;
    setStatus(tr("Stopping emulation..."));
    stopButton->setEnabled(false);
    emit stopRequested();
    setBusy(false);
    setStatus(tr("Emulation stopped."));
    Logger::info(QStringLiteral("Amiibo"), QStringLiteral("emulation stopped"));
}

void AmiiboPage::searchCatalog()
{
    const QString urlText = catalogUrlEdit->text().trimmed();
    if(urlText.isEmpty())
    {
        catalogInfo->setText(tr("Set a catalog URL first. The catalog must return "
                                "JSON: {\"items\":[{\"name\",\"series\","
                                "\"image_url\",\"bin_url\"}]}."));
        return;
    }
    QUrl url(urlText);
    if(!url.isValid())
    {
        catalogInfo->setText(tr("The catalog URL is not valid."));
        return;
    }
    QSettings settings(QStringLiteral("GUIsettings.ini"), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Amiibo/catalogUrl"), urlText);

    catalogInfo->setText(tr("Searching..."));
    catalogList->clear();
    catalogEntries.clear();
    selectedCatalogBin.clear();
    catalogDownloadButton->setEnabled(false);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Proxmark3GUI/0.4.0"));
    QNetworkReply *reply = net->get(request);
    reply->setProperty("kind", QStringLiteral("catalog"));
    reply->setProperty("query", catalogSearchEdit->text().trimmed());
}

void AmiiboPage::onCatalogReply(QNetworkReply *reply)
{
    reply->deleteLater();
    const QString kind = reply->property("kind").toString();
    const QString query = reply->property("query").toString();
    const QByteArray body = reply->readAll();

    if(kind == QLatin1String("image"))
    {
        QPixmap pixmap;
        if(pixmap.loadFromData(body))
            imageLabel->setPixmap(pixmap.scaled(imageLabel->size(),
                                                Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
        return;
    }

    if(reply->error() != QNetworkReply::NoError)
    {
        catalogInfo->setText(tr("Catalog request failed: %1")
                             .arg(reply->errorString()));
        return;
    }

    if(kind == QLatin1String("bin"))
    {
        const QString target = tempPath(QStringLiteral("pm3gui_amiibo_catalog.bin"));
        if(body.size() != NTAG215_SIZE && body.size() != 1024 && body.size() != 2048)
        {
            catalogInfo->setText(tr("Downloaded data has an unsupported length (%1 bytes).")
                                 .arg(body.size()));
            return;
        }
        AmiiboEntry entry;
        entry.name = selectedCatalogName.isEmpty()
                ? tr("Downloaded data") : selectedCatalogName;
        entry.source = tr("online");
        entry.data = body;
        addEntry(entry);
        catalogInfo->setText(tr("Downloaded %1 bytes and added it to the library.")
                             .arg(body.size()));
        return;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &error);
    if(error.error != QJsonParseError::NoError)
    {
        catalogInfo->setText(tr("The catalog returned invalid JSON."));
        return;
    }
    QJsonArray items;
    if(doc.isObject())
        items = doc.object().value(QStringLiteral("items")).toArray();
    else if(doc.isArray())
        items = doc.array();

    catalogEntries.clear();
    catalogList->clear();
    for(const QJsonValue value : items)
    {
        const QJsonObject object = value.toObject();
        AmiiboEntry entry;
        entry.name = object.value(QStringLiteral("name")).toString();
        entry.series = object.value(QStringLiteral("series")).toString();
        entry.imageUrl = object.value(QStringLiteral("image_url")).toString();
        entry.binUrl = object.value(QStringLiteral("bin_url")).toString();
        if(query.isEmpty() || entry.name.contains(query, Qt::CaseInsensitive))
            catalogEntries.append(entry);
    }
    for(const AmiiboEntry &entry : catalogEntries)
        catalogList->addItem(entry.series.isEmpty()
                             ? entry.name
                             : QStringLiteral("%1 [%2]").arg(entry.name, entry.series));
    catalogInfo->setText(tr("%1 catalog entries.").arg(catalogEntries.size()));
}

void AmiiboPage::onCatalogSelectionChanged()
{
    const int index = catalogList->currentRow();
    const bool valid = index >= 0 && index < catalogEntries.size();
    catalogDownloadButton->setEnabled(valid && !catalogEntries.at(index).binUrl.isEmpty());
    if(!valid)
        return;
    const AmiiboEntry &entry = catalogEntries.at(index);
    selectedCatalogName = entry.name;
    imageLabel->setPixmap(QPixmap());
    imageLabel->setText(tr("No image"));
    if(entry.imageUrl.isEmpty())
        return;
    QNetworkRequest request(QUrl(entry.imageUrl));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Proxmark3GUI/0.4.0"));
    QNetworkReply *reply = net->get(request);
    reply->setProperty("kind", QStringLiteral("image"));
}

void AmiiboPage::downloadCatalogItem()
{
    const int index = catalogList->currentRow();
    if(index < 0 || index >= catalogEntries.size())
        return;
    const AmiiboEntry &entry = catalogEntries.at(index);
    if(entry.binUrl.isEmpty())
        return;
    catalogInfo->setText(tr("Downloading %1...").arg(entry.name));
    QNetworkRequest request(QUrl(entry.binUrl));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Proxmark3GUI/0.4.0"));
    QNetworkReply *reply = net->get(request);
    reply->setProperty("kind", QStringLiteral("bin"));
}

