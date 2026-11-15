#include "amiibopage.h"

#include "common/util.h"
#include "core/clientsession.h"
#include "core/logger.h"
#include "amiibo/amiibodata.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
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
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

AmiiboPage::AmiiboPage(Util *util, ClientSession *session, QWidget *parent)
    : QWidget(parent)
    , util(util)
    , session(session)
    , net(nullptr)
    , loaded(false)
    , connected(false)
    , busy(false)
    , emulating(false)
{
    buildUi();
    net = new QNetworkAccessManager(this);
    connect(net, &QNetworkAccessManager::finished, this, &AmiiboPage::onCatalogReply);
    QSettings settings(QStringLiteral("GUIsettings.ini"), QSettings::IniFormat);
    catalogUrlEdit->setText(settings.value(QStringLiteral("Amiibo/catalogUrl")).toString());
    const int modeIndex = simModeCombo->findData(
                settings.value(QStringLiteral("Amiibo/loadMode"), QStringLiteral("auto")));
    if(modeIndex >= 0)
        simModeCombo->setCurrentIndex(modeIndex);
    setClientConnected(false);
}

AmiiboPage::~AmiiboPage()
{
    entries.clear();
    catalogEntries.clear();
}

void AmiiboPage::buildUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);

    QLabel *intro = new QLabel(
        tr("Manage Amiibo files and simulate them through the Proxmark3."), this);
    intro->setStyleSheet(QStringLiteral("color: palette(mid);"));
    root->addWidget(intro);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    root->addWidget(splitter, 1);

    QGroupBox *libraryBox = new QGroupBox(tr("Amiibo library"), splitter);
    QVBoxLayout *libraryLayout = new QVBoxLayout(libraryBox);
    library = new QListWidget(libraryBox);
    library->setObjectName(QStringLiteral("Amiibo_library"));
    connect(library, &QListWidget::currentRowChanged, this, &AmiiboPage::onEntrySelected);
    libraryLayout->addWidget(library, 1);

    QHBoxLayout *fileButtons = new QHBoxLayout();
    importButton = new QPushButton(tr("Import BIN..."), libraryBox);
    importButton->setObjectName(QStringLiteral("Amiibo_importButton"));
    importFolderButton = new QPushButton(tr("Import folder..."), libraryBox);
    importFolderButton->setObjectName(QStringLiteral("Amiibo_importFolderButton"));
    removeButton = new QPushButton(tr("Remove"), libraryBox);
    removeButton->setObjectName(QStringLiteral("Amiibo_removeButton"));
    connect(importButton, &QPushButton::clicked, this, &AmiiboPage::importFile);
    connect(importFolderButton, &QPushButton::clicked, this, &AmiiboPage::importFolder);
    connect(removeButton, &QPushButton::clicked, this, &AmiiboPage::removeEntry);
    fileButtons->addWidget(importButton);
    fileButtons->addWidget(importFolderButton);
    fileButtons->addWidget(removeButton);
    fileButtons->addStretch();
    libraryLayout->addLayout(fileButtons);

    QWidget *right = new QWidget(splitter);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);

    QGroupBox *detailsBox = new QGroupBox(tr("Selected Amiibo"), right);
    QHBoxLayout *detailsLayout = new QHBoxLayout(detailsBox);
    imageLabel = new QLabel(detailsBox);
    imageLabel->setObjectName(QStringLiteral("Amiibo_imageLabel"));
    imageLabel->setFixedSize(110, 110);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setStyleSheet(QStringLiteral("border: 1px solid palette(mid);"));
    imageLabel->setText(tr("No image"));
    detailsLayout->addWidget(imageLabel);

    QFormLayout *form = new QFormLayout();
    nameLabel = new QLabel(tr("(none)"), detailsBox);
    nameLabel->setObjectName(QStringLiteral("Amiibo_nameLabel"));
    sourceLabel = new QLabel(QString(), detailsBox);
    sizeLabel = new QLabel(QString(), detailsBox);
    sizeLabel->setObjectName(QStringLiteral("Amiibo_sizeLabel"));
    hashLabel = new QLabel(QString(), detailsBox);
    hashLabel->setObjectName(QStringLiteral("Amiibo_hashLabel"));
    hashLabel->setWordWrap(true);
    hashLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    formatLabel = new QLabel(QString(), detailsBox);
    form->addRow(tr("Name:"), nameLabel);
    form->addRow(tr("Source:"), sourceLabel);
    form->addRow(tr("Size:"), sizeLabel);
    form->addRow(tr("SHA-256:"), hashLabel);
    form->addRow(tr("Format:"), formatLabel);
    detailsLayout->addLayout(form, 1);
    QTabWidget *rightTabs = new QTabWidget(right);
    rightTabs->setObjectName(QStringLiteral("Amiibo_rightTabs"));
    QWidget *simTab = new QWidget(rightTabs);
    QVBoxLayout *simLayout = new QVBoxLayout(simTab);
    simLayout->addWidget(detailsBox);

    QGroupBox *opsBox = new QGroupBox(tr("Simulation"), right);
    opsBox->setFixedHeight(220);
    QVBoxLayout *opsLayout = new QVBoxLayout(opsBox);

    QFormLayout *modeForm = new QFormLayout();
    simModeCombo = new QComboBox(opsBox);
    simModeCombo->setObjectName(QStringLiteral("Amiibo_simModeCombo"));
    simModeCombo->addItem(tr("Auto compatibility"), QStringLiteral("auto"));
    simModeCombo->addItem(tr("Standard MFU load"), QStringLiteral("standard"));
    simModeCombo->addItem(tr("Direct page load (compatibility)"), QStringLiteral("direct"));
    connect(simModeCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int) {
                QSettings s(QStringLiteral("GUIsettings.ini"), QSettings::IniFormat);
                s.setValue(QStringLiteral("Amiibo/loadMode"),
                           simModeCombo->currentData().toString());
            });
    simModeCombo->setToolTip(
        tr("Standard uses the client's MFU dump detection. Direct page load "
           "writes the 540-byte image into emulator memory without changing "
           "the BIN; it is used when the client rejects the dump header."));
    modeForm->addRow(tr("Load mode:"), simModeCombo);
    opsLayout->addLayout(modeForm);

    QHBoxLayout *opsRow = new QHBoxLayout();
    emulateButton = new QPushButton(tr("Start emulation"), opsBox);
    emulateButton->setObjectName(QStringLiteral("Amiibo_emulateButton"));
    stopButton = new QPushButton(tr("Stop emulation"), opsBox);
    stopButton->setObjectName(QStringLiteral("Amiibo_stopButton"));
    connect(emulateButton, &QPushButton::clicked, this, &AmiiboPage::startEmulation);
    connect(stopButton, &QPushButton::clicked, this, &AmiiboPage::stopEmulation);
    opsRow->addWidget(emulateButton);
    opsRow->addWidget(stopButton);
    opsRow->addStretch();
    opsLayout->addLayout(opsRow);

    stateLabel = new QLabel(tr("No Amiibo selected."), opsBox);
    stateLabel->setObjectName(QStringLiteral("Amiibo_stateLabel"));
    stateLabel->setWordWrap(true);
    stateLabel->setMaximumHeight(40);
    stateLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    opsLayout->addWidget(stateLabel);

    outputView = new QPlainTextEdit(opsBox);
    outputView->setObjectName(QStringLiteral("Amiibo_outputView"));
    outputView->setReadOnly(true);
    outputView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    outputView->setMaximumHeight(50);
    outputView->setPlaceholderText(tr("Client output"));
    opsLayout->addWidget(outputView);
    simLayout->addWidget(opsBox);
    simLayout->addStretch();

    QGroupBox *catalogBox = new QGroupBox(tr("Online catalog"), right);
    catalogBox->setMinimumHeight(170);
    QVBoxLayout *catalogLayout = new QVBoxLayout(catalogBox);
    QHBoxLayout *urlRow = new QHBoxLayout();
    urlRow->addWidget(new QLabel(tr("Catalog URL:"), catalogBox));
    catalogUrlEdit = new QLineEdit(catalogBox);
    catalogUrlEdit->setObjectName(QStringLiteral("Amiibo_catalogUrlEdit"));
    catalogUrlEdit->setPlaceholderText(tr("https://example.org/amiibo.json"));
    urlRow->addWidget(catalogUrlEdit, 1);
    catalogLayout->addLayout(urlRow);

    QHBoxLayout *searchRow = new QHBoxLayout();
    catalogSearchEdit = new QLineEdit(catalogBox);
    catalogSearchEdit->setPlaceholderText(tr("Search name"));
    catalogSearchButton = new QPushButton(tr("Search"), catalogBox);
    catalogSearchButton->setObjectName(QStringLiteral("Amiibo_catalogSearchButton"));
    catalogDownloadButton = new QPushButton(tr("Download BIN"), catalogBox);
    catalogDownloadButton->setObjectName(QStringLiteral("Amiibo_catalogDownloadButton"));
    catalogDownloadButton->setEnabled(false);
    connect(catalogSearchButton, &QPushButton::clicked, this, &AmiiboPage::searchCatalog);
    connect(catalogDownloadButton, &QPushButton::clicked,
            this, &AmiiboPage::downloadCatalogItem);
    searchRow->addWidget(catalogSearchEdit, 1);
    searchRow->addWidget(catalogSearchButton);
    searchRow->addWidget(catalogDownloadButton);
    catalogLayout->addLayout(searchRow);

    catalogList = new QListWidget(catalogBox);
    catalogList->setObjectName(QStringLiteral("Amiibo_catalogList"));
    catalogList->setMinimumHeight(70);
    catalogList->setMaximumHeight(90);
    connect(catalogList, &QListWidget::currentRowChanged,
            this, &AmiiboPage::onCatalogSelectionChanged);
    catalogLayout->addWidget(catalogList);

    catalogInfo = new QLabel(tr("No catalog request yet."), catalogBox);
    catalogInfo->setObjectName(QStringLiteral("Amiibo_catalogInfo"));
    catalogInfo->setWordWrap(true);
    catalogInfo->setStyleSheet(QStringLiteral("color: palette(mid);"));
    catalogLayout->addWidget(catalogInfo);
    rightTabs->addTab(simTab, tr("Amiibo"));
    rightTabs->addTab(catalogBox, tr("Online catalog"));
    rightLayout->addWidget(rightTabs);

    splitter->addWidget(libraryBox);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
}

void AmiiboPage::setDeviceInfo(const QString &port, const QString &clientPath,
                               const QString &workingDir)
{
    Q_UNUSED(port)
    Q_UNUSED(clientPath)
    this->workingDir = workingDir;
}

void AmiiboPage::setClientConnected(bool connected)
{
    if(!connected && emulating)
    {
        emulating = false;
        setStatus(tr("Device disconnected; emulation state is unknown."), true);
    }
    this->connected = connected;
    updateButtons();
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

void AmiiboPage::setStatus(const QString &text, bool error)
{
    stateLabel->setStyleSheet(error ? QStringLiteral("color:rgb(200,20,20);") : QString());
    stateLabel->setText(text);
}

void AmiiboPage::setBusy(bool busy)
{
    this->busy = busy;
    updateButtons();
    if(busy)
        QApplication::setOverrideCursor(Qt::WaitCursor);
    else
        QApplication::restoreOverrideCursor();
}

void AmiiboPage::updateButtons()
{
    importButton->setEnabled(!busy);
    importFolderButton->setEnabled(!busy);
    removeButton->setEnabled(!busy && library->currentRow() >= 0);
    emulateButton->setEnabled(connected && loaded && !busy && !emulating);
    stopButton->setEnabled(emulating && !busy);
    simModeCombo->setEnabled(!busy && !emulating);
}

void AmiiboPage::addEntry(const AmiiboEntry &entry)
{
    entries.append(entry);
    refreshList();
    library->setCurrentRow(entries.size() - 1);
}

void AmiiboPage::selectEntry(int index)
{
    if(index >= 0 && index < entries.size())
        library->setCurrentRow(index);
}

void AmiiboPage::refreshList()
{
    library->clear();
    for(const AmiiboEntry &entry : entries)
    {
        QString text = entry.name;
        if(!entry.series.isEmpty())
            text += QStringLiteral(" [%1]").arg(entry.series);
        QListWidgetItem *item = new QListWidgetItem(text, library);
        item->setToolTip(tr("Source: %1").arg(entry.source));
    }
}

void AmiiboPage::onEntrySelected()
{
    const int index = library->currentRow();
    showEntry(index);
    if(index < 0 || index >= entries.size())
    {
        filePath.clear();
        rawBytes.clear();
        rawHash.clear();
        loaded = false;
        setStatus(tr("No Amiibo selected."));
        updateButtons();
        return;
    }
    const AmiiboEntry &entry = entries.at(index);
    filePath = entry.path;
    rawBytes = entry.raw;
    rawHash = entry.hash;
    loaded = !entry.raw.isEmpty() || !entry.path.isEmpty();
    outputView->clear();
    setStatus(loaded ? tr("Ready.") : tr("The selected file is empty."), !loaded);
    updateButtons();
}

QString AmiiboPage::formatOf(const QByteArray &data)
{
    if(data.isEmpty())
        return tr("Empty");
    const Amiibo::Info info = Amiibo::classify(data);
    switch(info.kind)
    {
    case Amiibo::Kind::DumpFile:
        return tr("Dump file (%1 pages, %2 bytes)").arg(info.pages).arg(data.size());
    case Amiibo::Kind::RawImage:
        return tr("Raw image (%1 pages, %2 bytes)").arg(info.pages).arg(data.size());
    default:
        return tr("Unrecognised (%1 bytes)").arg(data.size());
    }
}

void AmiiboPage::showEntry(int index)
{
    if(index < 0 || index >= entries.size())
    {
        nameLabel->setText(tr("(none)"));
        sourceLabel->clear();
        sizeLabel->clear();
        hashLabel->clear();
        formatLabel->clear();
        imageLabel->setPixmap(QPixmap());
        imageLabel->setText(tr("No image"));
        return;
    }
    const AmiiboEntry &entry = entries.at(index);
    nameLabel->setText(entry.name);
    sourceLabel->setText(entry.source);
    sizeLabel->setText(tr("%1 bytes").arg(entry.raw.size()));
    hashLabel->setText(entry.hash.size() > 32
                       ? entry.hash.left(32) + QLatin1Char('\n') + entry.hash.mid(32)
                       : entry.hash);
    hashLabel->setToolTip(entry.hash);
    formatLabel->setText(formatOf(entry.raw));
    loadImageFor(entry);
}

QString AmiiboPage::localSidecarImage(const QString &binPath)
{
    const QFileInfo info(binPath);
    const QString base = info.absolutePath() + QLatin1Char('/') + info.completeBaseName();
    for(const QString &suffix : {QStringLiteral(".png"), QStringLiteral(".jpg"),
                                 QStringLiteral(".jpeg")})
    {
        if(QFileInfo::exists(base + suffix))
            return base + suffix;
    }
    return QString();
}

void AmiiboPage::loadImageFor(const AmiiboEntry &entry)
{
    imageLabel->setPixmap(QPixmap());
    imageLabel->setText(tr("No image"));
    imageLabel->setToolTip(tr("No image"));
    const QString local = entry.path.isEmpty() ? QString() : localSidecarImage(entry.path);
    if(!local.isEmpty())
    {
        QPixmap pixmap(local);
        if(!pixmap.isNull())
        {
            imageLabel->setPixmap(pixmap.scaled(imageLabel->size(),
                                                Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
            imageLabel->setToolTip(tr("Image: %1").arg(local));
            return;
        }
    }
    if(entry.imageUrl.isEmpty())
        return;
    imageLabel->setText(tr("Loading image..."));
    imageLabel->setToolTip(tr("Loading image..."));
    QNetworkRequest request(QUrl(entry.imageUrl));
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Proxmark3GUI/0.4.0"));
    QNetworkReply *reply = net->get(request);
    reply->setProperty("kind", QStringLiteral("image"));
}

void AmiiboPage::importFile()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
                this, tr("Import Amiibo BIN"), QDir::homePath(),
                tr("Amiibo BIN (*.bin);;All files (*)"));
    if(paths.isEmpty())
        return;
    importPaths(paths, paths.size() > 1);
}

void AmiiboPage::importFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(
                this, tr("Import Amiibo folder"), QDir::homePath());
    if(dir.isEmpty())
        return;

    setBusy(true);
    setStatus(tr("Scanning folder..."));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    QStringList paths;
    QDirIterator it(dir, QStringList() << QStringLiteral("*.bin")
                                      << QStringLiteral("*.BIN"),
                    QDir::Files, QDirIterator::Subdirectories);
    while(it.hasNext())
    {
        paths.append(it.next());
        if(paths.size() % 200 == 0)
        {
            setStatus(tr("Scanning folder... %1 file(s)").arg(paths.size()));
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }
    }
    setBusy(false);
    if(paths.isEmpty())
    {
        setStatus(tr("No BIN files were found in this folder."));
        QMessageBox::information(this, tr("Amiibo"),
                                 tr("No BIN files were found in this folder."));
        return;
    }
    setStatus(tr("Found %1 BIN file(s). Importing...").arg(paths.size()));
    importPaths(paths, true);
}

int AmiiboPage::importPaths(const QStringList &paths, bool showSummary)
{
    if(paths.isEmpty())
        return 0;

    QSet<QString> existingPaths;
    QHash<QString, QString> hashToName;
    for(const AmiiboEntry &entry : entries)
    {
        if(!entry.path.isEmpty())
        {
            QFileInfo info(entry.path);
            const QString canonical = info.canonicalFilePath();
            existingPaths.insert(QDir::cleanPath(
                                     canonical.isEmpty() ? info.absoluteFilePath()
                                                         : canonical).toLower());
        }
        if(!entry.hash.isEmpty())
            hashToName.insert(entry.hash, entry.name);
    }

    QList<AmiiboEntry> added;
    QStringList details;
    int success = 0;
    int skipped = 0;
    int failed = 0;
    setBusy(true);

    for(int i = 0; i < paths.size(); i++)
    {
        const QString path = paths.at(i);
        const QFileInfo info(path);
        const QString canonical = info.canonicalFilePath();
        const QString key = QDir::cleanPath(
                    canonical.isEmpty() ? info.absoluteFilePath()
                                        : canonical).toLower();
        setStatus(tr("Importing %1 of %2: %3")
                  .arg(i + 1).arg(paths.size()).arg(info.fileName()));
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

        if(existingPaths.contains(key))
        {
            skipped++;
            details << tr("SKIP  %1  (already in library)").arg(info.absoluteFilePath());
            continue;
        }

        QFile file(path);
        if(!file.open(QIODevice::ReadOnly))
        {
            failed++;
            details << tr("FAIL  %1  (cannot open)").arg(info.absoluteFilePath());
            continue;
        }
        const QByteArray data = file.readAll();
        file.close();
        if(data.isEmpty())
        {
            failed++;
            details << tr("FAIL  %1  (empty file)").arg(info.absoluteFilePath());
            continue;
        }

        AmiiboEntry entry;
        entry.name = info.completeBaseName();
        entry.source = tr("file");
        entry.path = info.absoluteFilePath();
        entry.raw = data;
        entry.hash = Amiibo::sha256(data);
        added.append(entry);
        existingPaths.insert(key);
        success++;
        details << tr("OK    %1  (%2 bytes, %3)")
                   .arg(entry.path).arg(data.size()).arg(entry.hash.left(12));
        if(hashToName.contains(entry.hash))
            details << tr("      note: same SHA-256 as %1")
                       .arg(hashToName.value(entry.hash));
        else
            hashToName.insert(entry.hash, entry.name);

        if(success % 20 == 0)
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    if(!added.isEmpty())
    {
        entries.append(added);
        refreshList();
        library->setCurrentRow(entries.size() - 1);
    }
    setBusy(false);

    if(showSummary || skipped > 0 || failed > 0)
    {
        QMessageBox box(this);
        box.setWindowTitle(tr("Amiibo import"));
        box.setText(tr("Import complete"));
        box.setInformativeText(tr("Success: %1\nSkipped: %2\nFailed: %3")
                               .arg(success).arg(skipped).arg(failed));
        box.setDetailedText(details.join(QLatin1Char('\n')));
        box.setIcon(failed > 0 ? QMessageBox::Warning : QMessageBox::Information);
        box.exec();
    }
    setStatus(tr("Imported %1 file(s), skipped %2, failed %3.")
              .arg(success).arg(skipped).arg(failed), failed > 0);
    Logger::info(QStringLiteral("Amiibo"),
                 QStringLiteral("batch import paths=%1 success=%2 skipped=%3 failed=%4")
                 .arg(paths.size()).arg(success).arg(skipped).arg(failed));
    return success;
}

void AmiiboPage::removeEntry()
{
    const int index = library->currentRow();
    if(index < 0 || index >= entries.size())
        return;
    // Only the library reference is removed.  The original file is untouched.
    entries.removeAt(index);
    refreshList();
    if(entries.isEmpty())
    {
        showEntry(-1);
        filePath.clear();
        rawBytes.clear();
        rawHash.clear();
        loaded = false;
        setStatus(tr("No Amiibo selected."));
    }
    else
    {
        library->setCurrentRow(qMin(index, entries.size() - 1));
    }
    updateButtons();
}

QString AmiiboPage::standardLoadProblem(const QString &output) const
{
    if(output.trimmed().isEmpty())
        return tr("The client returned no output.");
    if(output.contains(QStringLiteral("Failed to detected"), Qt::CaseInsensitive) ||
       output.contains(QStringLiteral("Failed convert"), Qt::CaseInsensitive) ||
       output.contains(QStringLiteral("File content error"), Qt::CaseInsensitive) ||
       output.contains(QStringLiteral("can't"), Qt::CaseInsensitive) ||
       output.contains(QStringLiteral("cannot"), Qt::CaseInsensitive) ||
       output.contains(QStringLiteral("not available"), Qt::CaseInsensitive) ||
       output.contains(QStringLiteral("timeout"), Qt::CaseInsensitive) ||
       output.contains(QStringLiteral("error"), Qt::CaseInsensitive))
        return tr("The client could not detect or convert the MFU dump format.");
    if(!output.contains(QStringLiteral("Detected ")) ||
       !output.contains(QStringLiteral("mfu dump format"), Qt::CaseInsensitive))
        return tr("The client did not confirm MFU dump format detection.");
    if(!output.contains(QStringLiteral("Loaded"), Qt::CaseInsensitive))
        return tr("The client did not confirm that the file was loaded.");
    return QString();
}

bool AmiiboPage::plausibleRaw540(const QByteArray &bytes, QString *reason)
{
    if(bytes.size() != 540)
    {
        if(reason)
            *reason = tr("Direct page load supports 540-byte images only.");
        return false;
    }
    if(static_cast<quint8>(bytes.at(12)) != 0xE1 ||
       static_cast<quint8>(bytes.at(13)) != 0x10 ||
       (static_cast<quint8>(bytes.at(14)) != 0x12 &&
        static_cast<quint8>(bytes.at(14)) != 0x3E &&
        static_cast<quint8>(bytes.at(14)) != 0x6D) ||
       static_cast<quint8>(bytes.at(15)) != 0x00)
    {
        if(reason)
            *reason = tr("The image has no valid NTAG Capability Container "
                         "(E1 10 12/3E/6D 00).");
        return false;
    }
    const QByteArray uidA = bytes.mid(0, 3);
    const QByteArray uidB = bytes.mid(4, 3);
    if(uidA == QByteArray(3, '\0') || uidB == QByteArray(3, '\0'))
    {
        if(reason)
            *reason = tr("The image has an empty UID; it is not a usable NTAG image.");
        return false;
    }
    return true;
}

bool AmiiboPage::loadStandard(const QString &clientPath, QString *log, QString *error)
{
    const QString command = QStringLiteral("hf mfu eload -f \"%1\"")
            .arg(QDir::toNativeSeparators(clientPath));
    const QString output = util->execCMDWithOutput(command, 30000);
    log->append(tr("[Standard MFU] %1").arg(command));
    log->append(output.trimmed());
    const QString problem = standardLoadProblem(output);
    if(!problem.isEmpty())
    {
        *error = problem;
        return false;
    }
    return true;
}

bool AmiiboPage::loadDirect(const QByteArray &bytes, QString *log, QString *error)
{
    QString reason;
    if(!plausibleRaw540(bytes, &reason))
    {
        *error = reason;
        return false;
    }

    auto hasFailure = [](const QString &out) {
        return out.contains(QStringLiteral("fail"), Qt::CaseInsensitive) ||
               out.contains(QStringLiteral("error"), Qt::CaseInsensitive) ||
               out.contains(QStringLiteral("can't"), Qt::CaseInsensitive) ||
               out.contains(QStringLiteral("cannot"), Qt::CaseInsensitive) ||
               out.contains(QStringLiteral("timeout"), Qt::CaseInsensitive) ||
               out.contains(QStringLiteral("not available"), Qt::CaseInsensitive);
    };
    auto run = [&](const QString &cmd, int timeout) {
        const QString out = util->execCMDWithOutput(cmd, timeout);
        log->append(QStringLiteral("[Direct] ") + cmd);
        log->append(out.trimmed());
        return out;
    };

    // Emulator header: version/pages only; the BIN itself is not modified.
    const QString header = QStringLiteral("00040402010011030000008600000000");
    const QString clear = run(QStringLiteral("hf mf eclr"), 10000);
    if(hasFailure(clear))
    {
        *error = tr("Could not clear emulator memory.");
        return false;
    }
    const QString headerOut = run(
                QStringLiteral("hf mf esetblk -b 0 -d %1").arg(header), 10000);
    if(hasFailure(headerOut))
    {
        *error = tr("Could not set the emulator header.");
        return false;
    }
    const QString view = run(QStringLiteral("hf mfu eview"), 10000);
    if(!view.contains(QStringLiteral("Max data page")) ||
       !view.contains(QStringLiteral("134")))
    {
        *error = tr("The emulator header was not accepted (page count mismatch).");
        return false;
    }

    for(int start = 0; start < 135; start += 16)
    {
        const int count = qMin(16, 135 - start);
        const QString data = QString::fromLatin1(
                    bytes.mid(start * 4, count * 4).toHex()).toUpper();
        const QString out = run(QStringLiteral("hf mfu esetblk --blk %1 -d %2")
                                .arg(start).arg(data), 15000);
        if(hasFailure(out) || !out.contains(QStringLiteral("Set ")))
        {
            *error = tr("Could not load emulator pages %1..%2.")
                    .arg(start).arg(start + count - 1);
            return false;
        }
    }
    return true;
}

QString AmiiboPage::clientPathFor(const QByteArray &bytes, const QString &hash,
                                  QString *temporaryFile)
{
    if(temporaryFile)
        temporaryFile->clear();
    if(!Amiibo::requiresAsciiClientPath(filePath))
        return filePath;

    // The Windows client opens paths as narrow strings; non-ASCII paths fail.
    // Copy the exact bytes to an ASCII path instead of changing the file.
    const QString temp = tempPath(QStringLiteral("pm3gui_amiibo_raw.bin"));
    QFile out(temp);
    if(!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size())
    {
        setStatus(tr("Cannot write the temporary client file:\n%1").arg(temp), true);
        return QString();
    }
    out.close();

    QFile check(temp);
    if(!check.open(QIODevice::ReadOnly))
    {
        setStatus(tr("Cannot verify the temporary client file."), true);
        return QString();
    }
    const QByteArray written = check.readAll();
    check.close();
    if(written != bytes || Amiibo::sha256(written) != hash)
    {
        QFile::remove(temp);
        setStatus(tr("The temporary client file does not match the imported bytes."), true);
        return QString();
    }
    if(temporaryFile)
        *temporaryFile = temp;
    return temp;
}

void AmiiboPage::startEmulation()
{
    if(!connected || !session)
    {
        setStatus(tr("Connect the Proxmark3 first."), true);
        return;
    }
    if(!loaded || filePath.isEmpty())
    {
        setStatus(tr("Select an Amiibo BIN first."), true);
        return;
    }

    // Byte-for-byte integrity check before the client sees the file.
    QFile file(filePath);
    if(!file.open(QIODevice::ReadOnly))
    {
        setStatus(tr("The selected file is no longer readable: %1").arg(filePath), true);
        return;
    }
    const QByteArray current = file.readAll();
    file.close();
    const QString currentHash = Amiibo::sha256(current);
    if(current.size() != rawBytes.size() || currentHash != rawHash)
    {
        setStatus(tr("The file changed after import; emulation was not started."), true);
        Logger::warning(QStringLiteral("Amiibo"),
                        QStringLiteral("file changed: expected %1/%2 got %3/%4")
                        .arg(rawBytes.size()).arg(rawHash)
                        .arg(current.size()).arg(currentHash));
        return;
    }

    setBusy(true);
    setStatus(tr("Loading the selected mode..."));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    const QString mode = simModeCombo->currentData().toString();
    QString log = tr("File: %1\nSize: %2 bytes\nSHA-256: %3\n")
            .arg(QDir::toNativeSeparators(filePath))
            .arg(current.size()).arg(currentHash);
    QString error;
    bool loadedOk = false;

    if(mode == QLatin1String("standard") || mode == QLatin1String("auto"))
    {
        QString temporaryFile;
        const QString clientPath = clientPathFor(current, currentHash, &temporaryFile);
        if(!clientPath.isEmpty())
        {
            log += tr("Client file: %1\n").arg(QDir::toNativeSeparators(clientPath));
            loadedOk = loadStandard(clientPath, &log, &error);
        }
        else
        {
            error = tr("Could not prepare the client file.");
        }
        if(!temporaryFile.isEmpty())
            QFile::remove(temporaryFile);

        if(!loadedOk && mode == QLatin1String("auto"))
        {
            const QString standardError = error;
            QString reason;
            if(plausibleRaw540(current, &reason))
            {
                log += tr("\nStandard load failed; trying direct page load.\n");
                error.clear();
                loadedOk = loadDirect(current, &log, &error);
            }
            else
            {
                error = tr("Standard MFU: %1\nDirect page load: %2")
                        .arg(standardError, reason);
            }
            if(!loadedOk && error.isEmpty())
                error = standardError;
            else if(!loadedOk && plausibleRaw540(current, nullptr) &&
                    !error.contains(standardError))
                error = tr("Standard MFU: %1\nDirect page load: %2")
                        .arg(standardError, error);
        }
    }
    else if(mode == QLatin1String("direct"))
    {
        loadedOk = loadDirect(current, &log, &error);
    }
    else
    {
        error = tr("Unknown load mode.");
    }

    outputView->setPlainText(log);
    setBusy(false);

    if(!loadedOk)
    {
        setStatus(error.isEmpty() ? tr("Loading failed.") : error, true);
        Logger::warning(QStringLiteral("Amiibo"),
                        QStringLiteral("load failed mode=%1: %2").arg(mode, error));
        // Do not start simulation on a partially loaded emulator.
        util->execCMDWithOutput(QStringLiteral("hf mf eclr"), 10000);
        return;
    }

    if(!session->executeDetached(QStringLiteral("hf mfu sim -t 7")))
    {
        setStatus(tr("The client accepted the data but refused to start the simulation."), true);
        return;
    }

    emulating = true;
    setStatus(tr("Simulation running (%1).").arg(simModeCombo->currentText()));
    Logger::info(QStringLiteral("Amiibo"),
                 QStringLiteral("simulation started mode=%1").arg(mode));
    updateButtons();
}

void AmiiboPage::stopEmulation()
{
    if(!emulating)
        return;
    emulating = false;
    setStatus(tr("Stopping simulation..."));
    emit stopRequested();
    setStatus(tr("Simulation stopped."));
    updateButtons();
    Logger::info(QStringLiteral("Amiibo"), QStringLiteral("simulation stopped"));
}

void AmiiboPage::searchCatalog()
{
    const QString urlText = catalogUrlEdit->text().trimmed();
    if(urlText.isEmpty())
    {
        catalogInfo->setText(tr("Set a catalog URL first."));
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
        {
            imageLabel->setPixmap(pixmap.scaled(imageLabel->size(),
                                                Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
            imageLabel->setToolTip(tr("Catalog image loaded"));
        }
        else
        {
            imageLabel->setText(tr("No image"));
            imageLabel->setToolTip(tr("No image"));
        }
        return;
    }

    if(reply->error() != QNetworkReply::NoError)
    {
        catalogInfo->setText(tr("Catalog request failed: %1").arg(reply->errorString()));
        return;
    }

    if(kind == QLatin1String("bin"))
    {
        if(body.isEmpty())
        {
            catalogInfo->setText(tr("The catalog returned an empty BIN."));
            return;
        }
        const QString hash = Amiibo::sha256(body);
        const QString path = tempPath(
                    QStringLiteral("pm3gui_amiibo_%1.bin").arg(hash.left(12)));
        QFile file(path);
        if(!file.open(QIODevice::WriteOnly) || file.write(body) != body.size())
        {
            catalogInfo->setText(tr("Cannot save the downloaded BIN."));
            return;
        }
        file.close();
        AmiiboEntry entry;
        entry.name = selectedCatalogName.isEmpty() ? tr("Catalog Amiibo")
                                                   : selectedCatalogName;
        entry.source = tr("online");
        entry.series = reply->property("series").toString();
        entry.imageUrl = reply->property("imageUrl").toString();
        entry.path = path;
        entry.raw = body;
        entry.hash = hash;
        addEntry(entry);
        catalogInfo->setText(tr("Downloaded %1 bytes.").arg(body.size()));
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
    if(!catalogEntries.isEmpty())
        catalogList->setCurrentRow(0);
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
    loadImageFor(entry);
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
    reply->setProperty("series", entry.series);
    reply->setProperty("imageUrl", entry.imageUrl);
}














