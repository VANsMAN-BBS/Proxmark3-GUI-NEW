#include "emrtd.h"
#include "core/logger.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>
#include <QJsonParseError>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
const char *kDefaultDgs = "DG1,DG2,DG11,DG12";

QString fieldLabel(const QString &key)
{
    if(key == "surname") return EMRTDPage::tr("Surname");
    if(key == "given_names") return EMRTDPage::tr("Given names");
    if(key == "name_native") return EMRTDPage::tr("Name (native script)");
    if(key == "document_number") return EMRTDPage::tr("Document number");
    if(key == "nationality") return EMRTDPage::tr("Nationality");
    if(key == "issuing_state") return EMRTDPage::tr("Issuing state");
    if(key == "document_code") return EMRTDPage::tr("Document code");
    if(key == "date_of_birth") return EMRTDPage::tr("Date of birth");
    if(key == "sex") return EMRTDPage::tr("Sex");
    if(key == "date_of_expiry") return EMRTDPage::tr("Date of expiry");
    if(key == "personal_number") return EMRTDPage::tr("Personal number");
    if(key == "note") return EMRTDPage::tr("Note");
    return key;
}
}

ImageDecodeTask::ImageDecodeTask(const QByteArray &data, quint64 generation, QObject *parent)
    : QThread(parent)
    , m_data(data)
    , m_generation(generation)
{
}

void ImageDecodeTask::run()
{
    QImage image;
    const bool ok = image.loadFromData(m_data);
    emit decoded(m_generation, image, ok);
}

EMRTDPage::EMRTDPage(QWidget *parent)    : QWidget(parent)
    , process(nullptr)
    , cancelTimer(nullptr)
    , stallTimer(nullptr)
    , decoder(nullptr)
    , imageGeneration(0)
    , decoding(false)
    , reading(false)
    , gotResult(false)
    , zoom(1.0)
{
    buildUi();

    process = new QProcess(this);
    process->setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
    // Console subsystem is needed for the JSON pipe; hide the console window.
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args)
    {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    connect(process, &QProcess::readyReadStandardOutput, this, &EMRTDPage::onProcessStdout);
    connect(process, &QProcess::readyReadStandardError, this, &EMRTDPage::onProcessStderr);
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &EMRTDPage::onProcessFinished);
    connect(process, &QProcess::errorOccurred, this, &EMRTDPage::onProcessError);

    cancelTimer = new QTimer(this);
    cancelTimer->setSingleShot(true);
    cancelTimer->setInterval(4000);
    connect(cancelTimer, &QTimer::timeout, this, &EMRTDPage::onCancelTimeout);

    // A read can legitimately take tens of seconds on DG2; report that the
    // reader is still alive instead of showing a frozen stage.
    stallTimer = new QTimer(this);
    stallTimer->setSingleShot(true);
    stallTimer->setInterval(15000);
    connect(stallTimer, &QTimer::timeout, this, &EMRTDPage::onStallTimeout);

    setBusy(false);
}

EMRTDPage::~EMRTDPage()
{
    ++imageGeneration; // invalidate a pending decode result
    if(decoder)
    {
        decoder->wait(3000);
        decoder = nullptr;
    }
    stopReaderProcess();
}

void EMRTDPage::buildUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);

    QSplitter *splitter = new QSplitter(Qt::Vertical, this);
    root->addWidget(splitter);

    // ---------------- MRZ input ----------------
    QGroupBox *mrzBox = new QGroupBox(tr("Document MRZ"), splitter);
    QVBoxLayout *mrzLayout = new QVBoxLayout(mrzBox);

    QLabel *mrzHint = new QLabel(
        tr("Paste the machine readable zone (TD1, TD2 or TD3). "
           "The document number, date of birth and expiry are read from it, "
           "so no BAC key has to be entered manually."), mrzBox);
    mrzHint->setWordWrap(true);
    mrzLayout->addWidget(mrzHint);

    mrzEdit = new QPlainTextEdit(mrzBox);
    mrzEdit->setPlaceholderText(tr("P<UTO...\nL898902C36UTO..."));
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mrzEdit->setFont(mono);
    mrzEdit->setFixedHeight(84);
    mrzEdit->setTabChangesFocus(true);
    mrzLayout->addWidget(mrzEdit);

    QHBoxLayout *mrzButtons = new QHBoxLayout();
    validateButton = new QPushButton(tr("Validate MRZ"), mrzBox);
    clearButton = new QPushButton(tr("Clear"), mrzBox);
    connect(validateButton, &QPushButton::clicked, this, &EMRTDPage::validateMrz);
    connect(clearButton, &QPushButton::clicked, this, &EMRTDPage::clearMrz);
    mrzButtons->addWidget(validateButton);
    mrzButtons->addWidget(clearButton);
    mrzButtons->addStretch(1);
    mrzLayout->addLayout(mrzButtons);

    mrzStatus = new QLabel(tr("MRZ not checked."), mrzBox);
    mrzStatus->setWordWrap(true);
    mrzLayout->addWidget(mrzStatus);

    // ---------------- read control ----------------
    QGroupBox *readBox = new QGroupBox(tr("Read"), splitter);
    QVBoxLayout *readLayout = new QVBoxLayout(readBox);

    deviceLabel = new QLabel(tr("Port: not selected"), readBox);
    deviceLabel->setWordWrap(true);
    readLayout->addWidget(deviceLabel);

    QHBoxLayout *readButtons = new QHBoxLayout();
    startButton = new QPushButton(tr("Start Reading"), readBox);
    cancelButton = new QPushButton(tr("Cancel"), readBox);
    connect(startButton, &QPushButton::clicked, this, &EMRTDPage::startRead);
    connect(cancelButton, &QPushButton::clicked, this, &EMRTDPage::cancelRead);
    readButtons->addWidget(startButton);
    readButtons->addWidget(cancelButton);
    readButtons->addStretch(1);
    readLayout->addLayout(readButtons);

    progress = new QProgressBar(readBox);
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setTextVisible(true);
    readLayout->addWidget(progress);

    stageLabelWidget = new QLabel(tr("Idle"), readBox);
    QFont stageFont = stageLabelWidget->font();
    stageFont.setBold(true);
    stageLabelWidget->setFont(stageFont);
    readLayout->addWidget(stageLabelWidget);

    detailLabel = new QLabel(QString(), readBox);
    detailLabel->setWordWrap(true);
    detailLabel->setStyleSheet(QStringLiteral("color: palette(mid);"));
    readLayout->addWidget(detailLabel);

    // ---------------- results ----------------
    resultTabs = new QTabWidget(splitter);

    QWidget *infoPage = new QWidget(resultTabs);
    QVBoxLayout *infoLayout = new QVBoxLayout(infoPage);
    infoTable = new QTableWidget(0, 2, infoPage);
    infoTable->setHorizontalHeaderLabels(QStringList() << tr("Field") << tr("Value"));
    infoTable->horizontalHeader()->setStretchLastSection(true);
    infoTable->verticalHeader()->setVisible(false);
    infoTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    infoTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    infoTable->setAlternatingRowColors(true);
    infoLayout->addWidget(infoTable, 1);

    QHBoxLayout *infoButtons = new QHBoxLayout();
    QPushButton *copyAllButton = new QPushButton(tr("Copy all information"), infoPage);
    QPushButton *copyMrzButton = new QPushButton(tr("Copy MRZ"), infoPage);
    QPushButton *copyFieldButton = new QPushButton(tr("Copy selected field"), infoPage);
    connect(copyAllButton, &QPushButton::clicked, this, &EMRTDPage::copyAllFields);
    connect(copyMrzButton, &QPushButton::clicked, this, &EMRTDPage::copyMrz);
    connect(copyFieldButton, &QPushButton::clicked, this, &EMRTDPage::copySelection);
    infoButtons->addWidget(copyAllButton);
    infoButtons->addWidget(copyMrzButton);
    infoButtons->addWidget(copyFieldButton);
    infoButtons->addStretch(1);
    infoLayout->addLayout(infoButtons);
    resultTabs->addTab(infoPage, tr("Personal information"));

    QWidget *portraitPage = new QWidget(resultTabs);
    QVBoxLayout *portraitLayout = new QVBoxLayout(portraitPage);
    QHBoxLayout *portraitButtons = new QHBoxLayout();
    QPushButton *zoomInButton = new QPushButton(tr("Zoom in"), portraitPage);
    QPushButton *zoomOutButton = new QPushButton(tr("Zoom out"), portraitPage);
    QPushButton *fitButton = new QPushButton(tr("Fit"), portraitPage);
    QPushButton *saveButton = new QPushButton(tr("Save portrait..."), portraitPage);
    connect(zoomInButton, &QPushButton::clicked, this, &EMRTDPage::zoomIn);
    connect(zoomOutButton, &QPushButton::clicked, this, &EMRTDPage::zoomOut);
    connect(fitButton, &QPushButton::clicked, this, &EMRTDPage::fitPortrait);
    connect(saveButton, &QPushButton::clicked, this, &EMRTDPage::savePortrait);
    portraitButtons->addWidget(zoomInButton);
    portraitButtons->addWidget(zoomOutButton);
    portraitButtons->addWidget(fitButton);
    portraitButtons->addStretch(1);
    portraitButtons->addWidget(saveButton);
    portraitLayout->addLayout(portraitButtons);

    QScrollArea *scroll = new QScrollArea(portraitPage);
    scroll->setWidgetResizable(true);
    scroll->setAlignment(Qt::AlignCenter);
    portraitLabel = new QLabel(scroll);
    portraitLabel->setAlignment(Qt::AlignCenter);
    portraitLabel->setText(tr("No portrait read yet."));
    portraitLabel->setMinimumSize(1, 1);
    scroll->setWidget(portraitLabel);
    portraitLayout->addWidget(scroll, 1);
    resultTabs->addTab(portraitPage, tr("Portrait"));

    QWidget *otherPage = new QWidget(resultTabs);
    QVBoxLayout *otherLayout = new QVBoxLayout(otherPage);
    otherTabs = new QTabWidget(otherPage);
    otherLayout->addWidget(otherTabs);
    resultTabs->addTab(otherPage, tr("Other data"));

    splitter->addWidget(mrzBox);
    splitter->addWidget(readBox);
    splitter->addWidget(resultTabs);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 0);
    splitter->setStretchFactor(2, 1);

    // The portrait is only laid out once its tab becomes visible, so fit it
    // again when the user switches to it.
    connect(resultTabs, &QTabWidget::currentChanged, this, [this](int index)
    {
        if(index == 1)
            fitPortrait();
    });
}

void EMRTDPage::setDeviceInfo(const QString &port, const QString &clientPath)
{
    // The main window appends " *" to ports that are currently available.
    QString cleanPort = port.trimmed();
    const int marker = cleanPort.indexOf(QStringLiteral(" *"));
    if(marker > 0)
        cleanPort = cleanPort.left(marker).trimmed();
    this->port = cleanPort;
    this->clientPath = clientPath;
    deviceLabel->setText(cleanPort.isEmpty()
                         ? tr("Port: not selected")
                         : tr("Port: %1").arg(cleanPort));
}

void EMRTDPage::setPythonExecutable(const QString &path)
{
    pythonPathOverride = path;
}

bool EMRTDPage::isReading() const
{
    return reading || decoding;
}

QString EMRTDPage::pythonExecutable() const
{
    if(!pythonPathOverride.isEmpty())
        return pythonPathOverride;
    const QString env = qEnvironmentVariable("PM3GUI_EMRTD_EXE");
    if(!env.isEmpty())
        return env;
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("/emrtd/python.exe");
#else
    const QString name = QStringLiteral("/emrtd/bin/python3");
#endif
    const QString bundled = QCoreApplication::applicationDirPath() + name;
    if(QFileInfo::exists(bundled))
        return bundled;
    // Development fallback: <repo>/build-runtime/emrtd/python.exe
    const QString dev = QCoreApplication::applicationDirPath()
            + QStringLiteral("/../build-runtime/emrtd/python.exe");
    if(QFileInfo::exists(dev))
        return QDir(dev).absolutePath();
    return bundled;
}

QString EMRTDPage::readerDirectory() const
{
    return QFileInfo(pythonExecutable()).absolutePath();
}

QString EMRTDPage::stageLabel(const QString &stage) const
{
    if(stage == "mrz_check") return tr("Checking MRZ");
    if(stage == "connect") return tr("Connecting to Proxmark3");
    if(stage == "select_aid") return tr("Selecting the eMRTD application");
    if(stage == "get_challenge") return tr("Getting challenge");
    if(stage == "bac") return tr("BAC authentication");
    if(stage == "ef_com") return tr("Reading EF.COM");
    if(stage == "dg1") return tr("Reading DG1 (personal data)");
    if(stage == "dg2") return tr("Reading DG2 (portrait)");
    if(stage == "dg11") return tr("Reading DG11 (additional data)");
    if(stage == "dg12") return tr("Reading DG12 (additional data)");
    if(stage == "parse") return tr("Parsing data");
    if(stage == "done") return tr("Done");
    return stage;
}

void EMRTDPage::setBusy(bool busy, const QString &stage)
{
    reading = busy;
    startButton->setEnabled(!busy);
    validateButton->setEnabled(!busy);
    clearButton->setEnabled(!busy);
    cancelButton->setEnabled(busy);
    mrzEdit->setReadOnly(busy);
    if(busy && !stage.isEmpty())
        stageLabelWidget->setText(stage);
    if(!busy && stage.isEmpty() && progress->value() >= 100)
        stageLabelWidget->setText(tr("Done"));
}

void EMRTDPage::validateMrz()
{
    const QString text = mrzEdit->toPlainText().trimmed();
    if(text.isEmpty())
    {
        mrzStatus->setText(tr("Paste the MRZ first."));
        return;
    }
    const QString python = pythonExecutable();
    if(!QFileInfo::exists(python))
    {
        showError(tr("The eMRTD reader runtime was not found:\n%1").arg(python));
        return;
    }

    QJsonObject request;
    request.insert(QStringLiteral("action"), QStringLiteral("validate"));
    request.insert(QStringLiteral("mrz"), text);

    QProcess check;
    check.setWorkingDirectory(readerDirectory());
    check.start(python, QStringList() << QStringLiteral("-m")
                                      << QStringLiteral("emrtd_pm3")
                                      << QStringLiteral("gui"));
    if(!check.waitForStarted(5000))
    {
        showError(tr("Cannot start the eMRTD reader:\n%1").arg(check.errorString()));
        return;
    }
    check.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n");
    check.waitForBytesWritten(1000);
    check.closeWriteChannel();
    if(!check.waitForFinished(15000))
    {
        check.kill();
        showError(tr("MRZ validation timed out."));
        return;
    }

    QJsonObject result;
    const QList<QByteArray> lines = check.readAllStandardOutput().split('\n');
    for(const QByteArray &line : lines)
    {
        if(line.trimmed().isEmpty())
            continue;
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if(error.error == QJsonParseError::NoError && doc.isObject())
            result = doc.object();
    }

    if(result.value(QStringLiteral("event")).toString() == QLatin1String("error"))
    {
        mrzStatus->setText(tr("MRZ invalid: %1")
                           .arg(result.value(QStringLiteral("message")).toString()));
        return;
    }

    const QJsonObject validation = result.value(QStringLiteral("data")).toObject()
            .value(QStringLiteral("validation")).toObject();
    const QString layout = validation.value(QStringLiteral("layout")).toString();
    const QJsonArray issues = validation.value(QStringLiteral("issues")).toArray();
    const QJsonObject parsed = validation.value(QStringLiteral("parsed")).toObject();

    QStringList issueText;
    for(const QJsonValue value : issues)
    {
        const QString code = value.toString();
        if(code == QLatin1String("line_length"))
            issueText << tr("line length does not match %1").arg(layout);
        else if(code.startsWith(QLatin1String("check_digit:")))
            issueText << tr("check digit failed: %1").arg(code.mid(12));
        else if(code == QLatin1String("date_of_birth_format"))
            issueText << tr("date of birth must be YYMMDD");
        else if(code == QLatin1String("date_of_expiry_format"))
            issueText << tr("expiry must be YYMMDD");
        else if(code == QLatin1String("non_icao"))
            issueText << tr("non-ICAO layout; verify the document number and dates");
        else
            issueText << code;
    }

    const QString doc = parsed.value(QStringLiteral("document_number")).toString();
    const QString dob = parsed.value(QStringLiteral("date_of_birth")).toString();
    const QString exp = parsed.value(QStringLiteral("date_of_expiry")).toString();
    const QString summary = tr("%1 | document %2 | born %3 | expires %4")
            .arg(layout.isEmpty() ? tr("unknown layout") : layout, doc, dob, exp);
    if(issueText.isEmpty())
        mrzStatus->setText(tr("MRZ valid. %1").arg(summary));
    else
        mrzStatus->setText(tr("Check the MRZ: %1. %2")
                           .arg(issueText.join(QStringLiteral("; ")), summary));
}

void EMRTDPage::clearMrz()
{
    mrzEdit->clear();
    mrzStatus->setText(tr("MRZ not checked."));
    clearSensitiveData();
}

void EMRTDPage::startRead()
{
    if(reading || decoding)
        return;
    const QString text = mrzEdit->toPlainText().trimmed();
    if(text.isEmpty())
    {
        QMessageBox::information(this, tr("eMRTD Reader"),
                                 tr("Paste the MRZ of the document first."));
        return;
    }
    const QString python = pythonExecutable();
    if(!QFileInfo::exists(python))
    {
        showError(tr("The eMRTD reader runtime was not found:\n%1\n\n"
                     "Install the packaged build or set PM3GUI_EMRTD_EXE.")
                  .arg(python));
        return;
    }
    if(port.isEmpty())
    {
        QMessageBox::information(this, tr("eMRTD Reader"),
                                 tr("Select the Proxmark3 port on the main window first."));
        return;
    }

    resetReport();
    pendingError.clear();
    pendingErrorField.clear();
    gotResult = false;
    stdoutBuffer.clear();
    stderrBuffer.clear();
    progress->setValue(0);
    detailLabel->clear();
    setBusy(true, tr("Starting..."));
    stageLabelWidget->setText(tr("Starting..."));

    Logger::info(QStringLiteral("eMRTD"),
                 QStringLiteral("read started (port %1)").arg(port));

    // Hand the serial device over before the reader starts its own client.
    emit aboutToRead();

    QJsonObject request;
    request.insert(QStringLiteral("mrz"), text);
    request.insert(QStringLiteral("port"), port);
    request.insert(QStringLiteral("dgs"), QString::fromLatin1(kDefaultDgs));
    request.insert(QStringLiteral("incognito"), true);
    if(!clientPath.isEmpty())
        request.insert(QStringLiteral("pm3_exe"), clientPath);

    process->setWorkingDirectory(readerDirectory());
    process->start(python, QStringList() << QStringLiteral("-m")
                                         << QStringLiteral("emrtd_pm3")
                                         << QStringLiteral("gui"));
    if(!process->waitForStarted(8000))
    {
        setBusy(false);
        showError(tr("Cannot start the eMRTD reader:\n%1").arg(process->errorString()));
        emit readFinished();
        return;
    }
    process->write(QJsonDocument(request).toJson(QJsonDocument::Compact) + "\n");
    process->waitForBytesWritten(1000);
    stallTimer->start();
}

void EMRTDPage::cancelRead()
{
    if(decoding)
    {
        // The reader already finished; only the portrait worker is running.
        ++imageGeneration;
        decoding = false;
        progress->setRange(0, 100);
        progress->setValue(0);
        stageLabelWidget->setText(tr("Cancelled"));
        portraitLabel->setText(tr("Cancelled"));
        return;
    }
    if(!reading || process->state() == QProcess::NotRunning)
        return;
    stageLabelWidget->setText(tr("Cancelling..."));
    cancelButton->setEnabled(false);
    process->write("{\"action\":\"cancel\"}\n");
    process->waitForBytesWritten(500);
    cancelTimer->start();
}

void EMRTDPage::onCancelTimeout()
{
    if(process->state() != QProcess::NotRunning)
    {
        Logger::warning(QStringLiteral("eMRTD"),
                        QStringLiteral("cancel timeout, terminating reader process tree"));
        stopReaderProcess();
    }
}

void EMRTDPage::onStallTimeout()
{
    if(!reading && !decoding)
        return;
    stageLabelWidget->setText(tr("Still working..."));
    detailLabel->setText(tr("The reader has not produced output for 15 seconds. "
                            "Reading a large DG2 can take a while; Cancel is "
                            "still available."));
    stallTimer->start();
}

void EMRTDPage::onImageDecoded(quint64 generation, const QImage &image, bool ok)
{
    if(generation != imageGeneration)
        return; // superseded or cancelled
    decoding = false;
    progress->setRange(0, 100);
    progress->setValue(100);
    stallTimer->stop();
    if(!ok || image.isNull())
    {
        portrait = QPixmap();
        portraitLabel->setPixmap(QPixmap());
        portraitLabel->setText(tr("The portrait could not be decoded (%1).")
                               .arg(portraitFormat));
        Logger::warning(QStringLiteral("eMRTD"),
                        QStringLiteral("portrait decode failed: %1").arg(portraitFormat));
        stageLabelWidget->setText(tr("Done"));
        return;
    }
    portrait = QPixmap::fromImage(image);
    fitPortrait();
    stageLabelWidget->setText(tr("Done"));
}

void EMRTDPage::onProcessStdout()
{
    stallTimer->start(); // the reader is alive
    stdoutBuffer += process->readAllStandardOutput();
    int newline = -1;
    while((newline = stdoutBuffer.indexOf('\n')) >= 0)
    {
        const QByteArray line = stdoutBuffer.left(newline).trimmed();
        stdoutBuffer.remove(0, newline + 1);
        if(line.isEmpty())
            continue;
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
        if(error.error != QJsonParseError::NoError || !doc.isObject())
            continue; // diagnostics from a dependency are not part of the protocol
        handleEvent(doc.object());
    }
}

void EMRTDPage::onProcessStderr()
{
    stderrBuffer += process->readAllStandardError();
    if(stderrBuffer.size() > 8192)
        stderrBuffer = stderrBuffer.right(8192);
}

void EMRTDPage::handleEvent(const QJsonObject &event)
{
    const QString type = event.value(QStringLiteral("event")).toString();
    if(type == QLatin1String("progress"))
    {
        const QString stage = event.value(QStringLiteral("stage")).toString();
        const int index = event.value(QStringLiteral("index")).toInt();
        const int total = event.value(QStringLiteral("total")).toInt(12);
        const QString status = event.value(QStringLiteral("status")).toString();
        if(total > 0)
            progress->setValue(qBound(0, index * 100 / total, 100));
        if(status == QLatin1String("start"))
            stageLabelWidget->setText(stageLabel(stage));
        else if(status == QLatin1String("skip"))
            stageLabelWidget->setText(tr("%1 - skipped").arg(stageLabel(stage)));
        else if(stage == QLatin1String("dg2") && status == QLatin1String("receiving"))
        {
            const int readBytes = event.value(QStringLiteral("bytes_read")).toInt();
            const int totalBytes = event.value(QStringLiteral("bytes_total")).toInt();
            stageLabelWidget->setText(tr("Reading DG2 (%1 / %2 bytes)")
                                      .arg(readBytes).arg(totalBytes));
            detailLabel->setText(tr("Receiving the facial image data from the document."));
        }
        else if(stage == QLatin1String("dg2") && status == QLatin1String("parsing"))
            stageLabelWidget->setText(tr("Parsing DG2 structure"));
        else if(stage == QLatin1String("dg2") && status == QLatin1String("extracting"))
            stageLabelWidget->setText(tr("Extracting facial image"));
        else if(stage == QLatin1String("dg2") && status == QLatin1String("decoding"))
            stageLabelWidget->setText(tr("Decoding facial image"));
        else if(stage == QLatin1String("dg2") && status == QLatin1String("preview"))
            stageLabelWidget->setText(tr("Updating preview"));
        if(event.contains(QStringLiteral("detail")))
            detailLabel->setText(event.value(QStringLiteral("detail")).toString());
    }
    else if(type == QLatin1String("info"))
    {
        const QString message = event.value(QStringLiteral("message")).toString();
        if(!message.isEmpty())
            detailLabel->setText(message);
    }
    else if(type == QLatin1String("result"))
    {
        gotResult = true;
        applyResult(event.value(QStringLiteral("data")).toObject());
    }
    else if(type == QLatin1String("error"))
    {
        pendingError = event.value(QStringLiteral("message")).toString();
        pendingErrorField = event.value(QStringLiteral("field")).toString();
        const int code = event.value(QStringLiteral("code")).toInt();
        if(code == 2)
            pendingError = tr("Authentication or card error: %1").arg(pendingError);
        else if(code == 3)
            pendingError = tr("Proxmark3 communication error: %1").arg(pendingError);
        else if(code == 4)
            pendingError = tr("Invalid input: %1").arg(pendingError);
        else if(code == 5)
            pendingError = tr("Reading was cancelled.");
        stageLabelWidget->setText(tr("Failed"));
        // The box is shown once the process finished, so it can never block
        // the session hand-back or stdout parsing.
    }
}

void EMRTDPage::applyResult(const QJsonObject &data)
{
    const QJsonObject document = data.value(QStringLiteral("document")).toObject();

    QStringList order;
    order << QStringLiteral("surname") << QStringLiteral("given_names")
          << QStringLiteral("name_native") << QStringLiteral("document_number")
          << QStringLiteral("nationality") << QStringLiteral("issuing_state")
          << QStringLiteral("document_code") << QStringLiteral("date_of_birth")
          << QStringLiteral("sex") << QStringLiteral("date_of_expiry")
          << QStringLiteral("personal_number") << QStringLiteral("note");

    infoTable->setRowCount(0);
    for(const QString &key : order)
    {
        const QString value = document.value(key).toString();
        if(value.isEmpty())
            continue;
        const int row = infoTable->rowCount();
        infoTable->insertRow(row);
        infoTable->setItem(row, 0, new QTableWidgetItem(fieldLabel(key)));
        infoTable->setItem(row, 1, new QTableWidgetItem(value));
    }
    infoTable->resizeColumnsToContents();

    // Other data: EF.COM, DG11, DG12 and the raw hexadecimal view.
    otherTabs->clear();
    auto addTextPage = [this](const QString &title, const QString &text)
    {
        QPlainTextEdit *view = new QPlainTextEdit(otherTabs);
        view->setReadOnly(true);
        view->setPlainText(text);
        otherTabs->addTab(view, title);
    };

    const QJsonObject efCom = data.value(QStringLiteral("ef_com")).toObject();
    if(!efCom.isEmpty())
    {
        QStringList groups;
        const QJsonArray dgList = efCom.value(QStringLiteral("data_groups")).toArray();
        for(const QJsonValue value : dgList)
            groups << QString::number(value.toInt());
        addTextPage(tr("EF.COM"),
                    tr("LDS version: %1\nUnicode version: %2\nData groups: %3")
                    .arg(efCom.value(QStringLiteral("lds_version")).toString(),
                         efCom.value(QStringLiteral("unicode_version")).toString(),
                         groups.join(QStringLiteral(", "))));
    }

    auto addObjectPage = [this, &addTextPage](const QString &title, const QJsonObject &obj)
    {
        if(obj.isEmpty())
            return;
        QStringList lines;
        for(auto it = obj.begin(); it != obj.end(); ++it)
        {
            if(it.value().isObject())
                continue;
            lines << QStringLiteral("%1: %2").arg(it.key(), it.value().toVariant().toString());
        }
        addTextPage(title, lines.join(QLatin1Char('\n')));
    };
    addObjectPage(tr("DG11"), data.value(QStringLiteral("dg11")).toObject());
    addObjectPage(tr("DG12"), data.value(QStringLiteral("dg12")).toObject());

    const QJsonObject hex = data.value(QStringLiteral("hex")).toObject();
    if(!hex.isEmpty())
    {
        QStringList blocks;
        for(auto it = hex.begin(); it != hex.end(); ++it)
            blocks << QStringLiteral("== %1 ==\n%2").arg(it.key(), it.value().toString());
        addTextPage(tr("Raw (hex)"), blocks.join(QStringLiteral("\n\n")));
    }
    else if(otherTabs->count() == 0)
    {
        addTextPage(tr("Other data"), tr("No additional files were returned by the document."));
    }

    // Portrait: decode off the GUI thread.
    const QJsonObject face = data.value(QStringLiteral("face")).toObject();
    portrait = QPixmap();
    portraitBytes.clear();
    portraitMime.clear();
    portraitFormat = face.value(QStringLiteral("format")).toString();
    bool decodeStarted = false;
    if(!face.isEmpty())
    {
        portraitMime = face.value(QStringLiteral("mime")).toString();
        portraitBytes = QByteArray::fromBase64(
                    face.value(QStringLiteral("data_b64")).toString().toLatin1());
        if(portraitBytes.isEmpty())
        {
            portraitLabel->setText(tr("The document did not return a portrait."));
        }
        else
        {
            ++imageGeneration;
            decoding = true;
            decodeStarted = true;
            stageLabelWidget->setText(tr("Decoding facial image"));
            progress->setRange(0, 0); // indeterminate: decoding time varies
            if(decoder)
            {
                decoder->wait(3000);
                decoder = nullptr;
            }
            decoder = new ImageDecodeTask(portraitBytes, imageGeneration, this);
            connect(decoder, &ImageDecodeTask::decoded,
                    this, &EMRTDPage::onImageDecoded, Qt::QueuedConnection);
            connect(decoder, &QThread::finished, decoder, &QObject::deleteLater);
            decoder->start();
        }
    }
    else
    {
        portraitLabel->setText(tr("The document did not return a portrait."));
    }

    if(!decodeStarted)
    {
        progress->setValue(100);
        stageLabelWidget->setText(tr("Done"));
    }
    resultTabs->setCurrentIndex(0);

    const QJsonArray warnings = data.value(QStringLiteral("warnings")).toArray();
    if(!warnings.isEmpty())
    {
        QStringList text;
        for(const QJsonValue value : warnings)
            text << value.toString();
        detailLabel->setText(text.join(QStringLiteral(" | ")));
    }
}

void EMRTDPage::resetReport()
{
    infoTable->setRowCount(0);
    otherTabs->clear();
    portraitLabel->setText(tr("No portrait read yet."));
    portraitLabel->setPixmap(QPixmap());
    portrait = QPixmap();
    portraitBytes.clear();
    portraitMime.clear();
    portraitFormat.clear();
    zoom = 1.0;
    progress->setValue(0);
}

void EMRTDPage::clearSensitiveData()
{
    resetReport();
    stdoutBuffer.clear();
    stderrBuffer.clear();
}

void EMRTDPage::showError(const QString &message, const QString &field)
{
    Q_UNUSED(field);
    Logger::error(QStringLiteral("eMRTD"), message);
    QMessageBox::warning(this, tr("eMRTD Reader"), message);
}

void EMRTDPage::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    cancelTimer->stop();
    stallTimer->stop();
    onProcessStdout(); // drain anything still buffered
    onProcessStderr();
    setBusy(false);
    // Give the serial port back before any modal dialog can block the event
    // loop; the owner reconnects its own client here.
    emit readFinished();
    if(decoding)
        cancelButton->setEnabled(true); // the portrait worker can still be stopped
    if(gotResult)    {
        Logger::info(QStringLiteral("eMRTD"), QStringLiteral("read finished"));
    }
    else
    {
        if(pendingError.isEmpty())
        {
            QString detail = QString::fromLocal8Bit(stderrBuffer).trimmed();
            if(detail.size() > 400)
                detail = detail.right(400);
            pendingError = (status == QProcess::CrashExit)
                    ? tr("The reader process crashed.")
                    : tr("The reader exited with code %1.").arg(exitCode);
            if(!detail.isEmpty())
                pendingError += QStringLiteral("\n") + detail;
        }
        stageLabelWidget->setText(tr("Failed"));
        Logger::warning(QStringLiteral("eMRTD"),
                        QStringLiteral("read failed: exit=%1").arg(exitCode));
        if(!pendingError.isEmpty())
            QMessageBox::warning(this, tr("eMRTD Reader"), pendingError);
    }
    pendingError.clear();
}

void EMRTDPage::onProcessError(QProcess::ProcessError error)
{
    if(error == QProcess::FailedToStart)
    {
        setBusy(false);
        showError(tr("Cannot start the eMRTD reader:\n%1").arg(process->errorString()));
        emit readFinished();
    }
}

void EMRTDPage::stopReaderProcess()
{
    if(process->state() == QProcess::NotRunning)
        return;
#ifdef Q_OS_WIN
    // The reader starts its own proxmark3.exe; kill the whole tree so no
    // orphan keeps the serial port busy.
    QProcess::execute(QStringLiteral("taskkill"),
                      QStringList() << QStringLiteral("/F") << QStringLiteral("/T")
                                    << QStringLiteral("/PID")
                                    << QString::number(process->processId()));
#endif
    process->kill();
    process->waitForFinished(2000);
}

void EMRTDPage::copyAllFields()
{
    QStringList lines;
    for(int row = 0; row < infoTable->rowCount(); row++)
    {
        QTableWidgetItem *name = infoTable->item(row, 0);
        QTableWidgetItem *value = infoTable->item(row, 1);
        if(name && value)
            lines << QStringLiteral("%1: %2").arg(name->text(), value->text());
    }
    if(lines.isEmpty())
        return;
    QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}

void EMRTDPage::copyMrz()
{
    const QString text = mrzEdit->toPlainText().trimmed();
    if(!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

void EMRTDPage::copySelection()
{
    const QList<QTableWidgetItem *> items = infoTable->selectedItems();
    if(items.isEmpty())
        return;
    // Rows are selected as a whole, so prefer the value column.
    for(QTableWidgetItem *item : items)
    {
        if(item->column() == 1)
        {
            QApplication::clipboard()->setText(item->text());
            return;
        }
    }
    QApplication::clipboard()->setText(items.first()->text());
}

void EMRTDPage::savePortrait()
{
    if(portrait.isNull())
    {
        QMessageBox::information(this, tr("eMRTD Reader"),
                                 tr("There is no portrait to save."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
                this, tr("Save portrait"),
                QDir::homePath() + QStringLiteral("/portrait.png"),
                tr("PNG image (*.png);;JPEG image (*.jpg *.jpeg)"));
    if(path.isEmpty())
        return;
    bool ok = false;
    if(path.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)
            || path.endsWith(QStringLiteral(".jpeg"), Qt::CaseInsensitive))
    {
        if(portraitMime == QLatin1String("image/jpeg") && !portraitBytes.isEmpty())
        {
            // Keep the original bytes: no re-encoding, no quality loss.
            QFile file(path);
            if(file.open(QIODevice::WriteOnly))
            {
                ok = file.write(portraitBytes) == portraitBytes.size();
                file.close();
            }
        }
        else
        {
            ok = portrait.save(path, "JPEG", 95);
        }
    }
    else
    {
        ok = portrait.save(path, "PNG");
    }
    if(!ok)
    {
        Logger::error(QStringLiteral("eMRTD"),
                      QStringLiteral("failed to save portrait: %1").arg(path));
        QMessageBox::warning(this, tr("eMRTD Reader"),
                             tr("Failed to save the image."));
    }
}

void EMRTDPage::zoomIn()
{
    zoom *= 1.25;
    updatePortrait();
}

void EMRTDPage::zoomOut()
{
    zoom = qMax(0.1, zoom / 1.25);
    updatePortrait();
}

void EMRTDPage::fitPortrait()
{
    if(portrait.isNull())
        return;
    QWidget *viewport = portraitLabel->parentWidget();
    const QSize available = viewport ? viewport->size() : QSize(600, 400);
    const double sx = double(available.width() - 8) / portrait.width();
    const double sy = double(available.height() - 8) / portrait.height();
    zoom = qMax(0.1, qMin(1.0, qMin(sx, sy)));
    updatePortrait();
}

void EMRTDPage::updatePortrait()
{
    if(portrait.isNull())
        return;
    const QSize target(qMax(1, int(portrait.width() * zoom)),
                       qMax(1, int(portrait.height() * zoom)));
    portraitLabel->setPixmap(portrait.scaled(target, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation));
}
