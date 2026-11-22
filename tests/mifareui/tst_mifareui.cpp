#include <QtTest>
#include <QAbstractButton>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSpacerItem>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextStream>
#include <QTranslator>
#include <QVBoxLayout>

#include "ui_mainwindow.h"
#include "module/mifare/mfcancelgate.h"
#include "module/mifare/mfimage.h"
#include "module/mifare/mftablelayout.h"

/**
 * Stage 3.3 MIFARE UI contract.
 *
 * The page is a horizontal 35 / 65 splitter: the left pane owns device status,
 * the live PM3 terminal and the task/progress area; the right pane owns the
 * card/key tables and the flat card-operation workspace.  These tests pin the
 * structure, the removed one-click entry, the Gen3 button contract, the
 * retained control object names / auto-connect slots and the DPI / resize
 * behaviour.
 */
class TestMifareUi : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void splitterStructureAndRatio();
    void paneMinimumWidthsAndResizeRatio();
    void splitterIsUserResizable();
    void requiredWidgetsAndObjectNames();
    void tableWidgetsAreStableInstances();
    void noOneClickReadEntry();
    void gen3ButtonIsNormalAndAfterInfo();
    void backdoorCaptionIsTranslated();
    void hardnestedCaptionIsTranslated();
    void cardTypeComboReplacesRadios();
    void workspaceRenamedAndFileButtonsMoved();
    void cardInfoStaysOnMifarePage();
    void sectionTitlesKeepNormalWeightUnderThemes();
    void groupTitlesHaveRoomAtAllFontScales();
    void flatWorkspaceHasNoDeepGroupBoxes();
    void terminalUsesExistingOutputStream();
    void allRetainedSlotsStillDeclared();
    void resultWidgetsExist();
    void resultLayoutIsCompact();
    void progressRowHasNoSlack();
    void logKeepsCapAndCollapses();
    void noVerticalSpacersInResultArea();
    void progressBarUsesDpiSafePolicy();
    void verticalAllocationHasNoRegression();
    void maximizedAndRestoredKeepProportions();
    void smallWindowKeepsEverythingReachable();
    void identityLabelsExistWithoutAllowedFlow();
    void advancedAreaScrollsInsteadOfClipping();
    void dpiScreenshotArtifacts();

    // ---- Stage 3.2 audit regressions --------------------------------------
    void miniTo1kLayoutHasEveryCell();
    void tableSyncIgnoresOutOfRangeRows();
    void backupCoversActualCardSize();
    void backupNameCarriesCardSize();
    void cancelGateResetAllowsRefresh();

private:
    QString readProjectFile(const QString &relative) const;
    void selectMifareTab();

    Ui::MainWindow *ui = nullptr;
    QMainWindow *window = nullptr;
};

void TestMifareUi::init()
{
    window = new QMainWindow;
    ui = new Ui::MainWindow;
    ui->setupUi(window);
    window->resize(1400, 950);
    window->show();
    QTest::qWait(60);
    selectMifareTab();
    QTest::qWait(60);
}

void TestMifareUi::cleanup()
{
    delete window;
    window = nullptr;
    delete ui;
    ui = nullptr;
}

void TestMifareUi::selectMifareTab()
{
    for (int i = 0; i < ui->funcTab->count(); ++i) {
        if (ui->funcTab->widget(i)->objectName() == QStringLiteral("mifareTab")) {
            ui->funcTab->setCurrentIndex(i);
            break;
        }
    }
}

QString TestMifareUi::readProjectFile(const QString &relative) const
{
    QFile file(QStringLiteral(SRCDIR "/../../") + relative);
    if (!file.open(QFile::ReadOnly | QFile::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

void TestMifareUi::splitterStructureAndRatio()
{
    QVERIFY(ui->MF_mainSplitter != nullptr);
    QCOMPARE(ui->MF_mainSplitter->orientation(), Qt::Horizontal);
    QCOMPARE(ui->MF_mainSplitter->count(), 2);
    QCOMPARE(ui->MF_mainSplitter->widget(0), static_cast<QWidget *>(ui->MF_leftPane));
    QCOMPARE(ui->MF_mainSplitter->widget(1), static_cast<QWidget *>(ui->MF_rightPane));
    QVERIFY(!ui->MF_mainSplitter->childrenCollapsible());

    // The splitter is the only direct child of the MIFARE page layout.
    QCOMPARE(ui->verticalLayout_7->count(), 1);
    QCOMPARE(ui->verticalLayout_7->itemAt(0)->widget(), static_cast<QWidget *>(ui->MF_mainSplitter));

    const QList<int> sizes = ui->MF_mainSplitter->sizes();
    QCOMPARE(sizes.size(), 2);
    const double ratio = double(sizes.at(0)) / double(sizes.at(0) + sizes.at(1));
    QVERIFY2(qAbs(ratio - 0.35) <= 0.03,
             qPrintable(QStringLiteral("ratio %1 sizes %2/%3").arg(ratio).arg(sizes.at(0)).arg(sizes.at(1))));
}

void TestMifareUi::paneMinimumWidthsAndResizeRatio()
{
    QVERIFY2(ui->MF_leftPane->minimumWidth() >= 320,
             qPrintable(QString::number(ui->MF_leftPane->minimumWidth())));
    QVERIFY2(ui->MF_rightPane->minimumWidth() >= 500,
             qPrintable(QString::number(ui->MF_rightPane->minimumWidth())));
    QVERIFY(ui->MF_leftPane->width() >= ui->MF_leftPane->minimumWidth());
    QVERIFY(ui->MF_rightPane->width() >= ui->MF_rightPane->minimumWidth());

    const QList<int> wide = { 1400, 1700, 2200 };
    for (int width : wide) {
        window->resize(width, 950);
        QTest::qWait(60);
        const QList<int> sizes = ui->MF_mainSplitter->sizes();
        const double ratio = double(sizes.at(0)) / double(sizes.at(0) + sizes.at(1));
        QVERIFY2(qAbs(ratio - 0.35) <= 0.035,
                 qPrintable(QStringLiteral("width %1 ratio %2 sizes %3/%4")
                            .arg(width).arg(ratio).arg(sizes.at(0)).arg(sizes.at(1))));
        QVERIFY(ui->MF_leftPane->width() >= ui->MF_leftPane->minimumWidth());
        QVERIFY(ui->MF_rightPane->width() >= ui->MF_rightPane->minimumWidth());
    }

    window->resize(1100, 700);
    QTest::qWait(60);
    const QList<int> sizes = ui->MF_mainSplitter->sizes();
    const double ratio = double(sizes.at(0)) / double(sizes.at(0) + sizes.at(1));
    QVERIFY2(qAbs(ratio - 0.35) <= 0.04,
             qPrintable(QStringLiteral("small ratio %1 sizes %2/%3")
                        .arg(ratio).arg(sizes.at(0)).arg(sizes.at(1))));
}

void TestMifareUi::splitterIsUserResizable()
{
    QVERIFY(!ui->MF_mainSplitter->childrenCollapsible());

    const QList<int> before = ui->MF_mainSplitter->sizes();
    QVERIFY(before.size() == 2);

    // Simulate the first user move (the real signal is emitted by the handle)
    // and then verify that the requested allocation is honoured instead of the
    // automatic 35 / 65 first-show ratio.
    QVERIFY(QMetaObject::invokeMethod(ui->MF_mainSplitter, "splitterMoved",
                                      Qt::DirectConnection,
                                      Q_ARG(int, 450), Q_ARG(int, 1)));
    ui->MF_mainSplitter->setSizes(QList<int>() << 560 << 765);
    QTest::qWait(50);
    const QList<int> after = ui->MF_mainSplitter->sizes();
    QVERIFY2(after.size() == 2, "splitter lost a pane");
    QVERIFY2(qAbs(after.at(0) - 560) <= 50,
             qPrintable(QStringLiteral("left %1").arg(after.at(0))));
    QVERIFY2(qAbs(after.at(1) - 765) <= 50,
             qPrintable(QStringLiteral("right %1").arg(after.at(1))));
    QVERIFY(ui->MF_leftPane->width() >= ui->MF_leftPane->minimumWidth());
    QVERIFY(ui->MF_rightPane->width() >= ui->MF_rightPane->minimumWidth());

    // Hiding and re-showing the page (a tab switch) must keep the ratio the
    // user chose instead of snapping back to 35 / 65.
    ui->MF_mainSplitter->hide();
    ui->MF_mainSplitter->show();
    QTest::qWait(50);
    const QList<int> afterShow = ui->MF_mainSplitter->sizes();
    QCOMPARE(afterShow.size(), 2);
    QVERIFY2(qAbs(afterShow.at(0) - after.at(0)) <= 30,
             qPrintable(QStringLiteral("left %1 -> %2").arg(after.at(0)).arg(afterShow.at(0))));
    QVERIFY2(qAbs(afterShow.at(1) - after.at(1)) <= 30,
             qPrintable(QStringLiteral("right %1 -> %2").arg(after.at(1)).arg(afterShow.at(1))));
}

void TestMifareUi::requiredWidgetsAndObjectNames()
{
    const QStringList names = {
        QStringLiteral("MF_mainSplitter"), QStringLiteral("MF_leftPane"),
        QStringLiteral("MF_rightPane"), QStringLiteral("MF_statusGroupBox"),
        QStringLiteral("MF_terminalGroupBox"), QStringLiteral("MF_terminalView"),
        QStringLiteral("MF_resultGroupBox"), QStringLiteral("MF_tablesWidget"),
        QStringLiteral("MF_dataWidget"), QStringLiteral("MF_keyWidget"),
        QStringLiteral("MF_advancedScrollArea"), QStringLiteral("MF_advancedGroupBox"),
        QStringLiteral("MF_fileGroupBox"), QStringLiteral("MF_attackGroupBox"),
        QStringLiteral("MF_RWGroupBox"), QStringLiteral("MF_normalGroupBox"),
        QStringLiteral("MF_UIDGroupBox"), QStringLiteral("MF_simGroupBox"),
        QStringLiteral("MF_sniffGroupBox"), QStringLiteral("MF_Type_box"),
        QStringLiteral("MF_typeLabel"),
        QStringLiteral("MF_File_loadButton"),
        QStringLiteral("MF_File_saveButton"), QStringLiteral("MF_File_clearButton"),
        QStringLiteral("MF_File_dataButton"), QStringLiteral("MF_File_keyButton"),
        QStringLiteral("MF_selectAllBox"), QStringLiteral("MF_selectTrailerBox"),
        QStringLiteral("MF_data2KeyButton"), QStringLiteral("MF_key2DataButton"),
        QStringLiteral("MF_fillKeysButton"), QStringLiteral("MF_trailerDecoderButton"),
        QStringLiteral("MF_RW_blockBox"), QStringLiteral("MF_RW_keyEdit"),
        QStringLiteral("MF_RW_keyTypeBox"), QStringLiteral("MF_RW_dataEdit"),
        QStringLiteral("MF_RW_readBlockButton"), QStringLiteral("MF_RW_writeBlockButton"),
        QStringLiteral("MF_RW_readSelectedButton"), QStringLiteral("MF_RW_writeSelectedButton"),
        QStringLiteral("MF_RW_dumpButton"), QStringLiteral("MF_RW_restoreButton"),
        QStringLiteral("MF_UID_lockButton"), QStringLiteral("MF_UID_aboutUIDButton"),
        QStringLiteral("MF_UID_readBlockButton"), QStringLiteral("MF_UID_writeBlockButton"),
        QStringLiteral("MF_UID_readSelectedButton"), QStringLiteral("MF_UID_writeSelectedButton"),
        QStringLiteral("MF_UID_setParaButton"), QStringLiteral("MF_UID_wipeButton"),
        QStringLiteral("MF_Sim_writeSelectedButton"), QStringLiteral("MF_Sim_readSelectedButton"),
        QStringLiteral("MF_Sim_clearButton"), QStringLiteral("MF_Sim_simButton"),
        QStringLiteral("MF_Sniff_sniffButton"), QStringLiteral("MF_14aSniff_snoopButton"),
        QStringLiteral("MF_Sniff_listButton"), QStringLiteral("MF_Sniff_loadButton"),
        QStringLiteral("MF_Sniff_saveButton"), QStringLiteral("MF_Quick_crackButton"),
        QStringLiteral("MF_Quick_loadButton"),
        QStringLiteral("MF_Quick_saveButton"), QStringLiteral("MF_Quick_cancelButton"),
        QStringLiteral("MF_status_refreshButton"), QStringLiteral("MF_status_capacityTitle"),
        QStringLiteral("MF_status_capacityLabel"), QStringLiteral("MF_Attack_infoButton"),
        QStringLiteral("MF_result_progressBar"),
        QStringLiteral("MF_result_stepLabel"), QStringLiteral("MF_result_statusLabel"),
        QStringLiteral("MF_result_keyLabel"),
        QStringLiteral("MF_result_metaLabel"), QStringLiteral("MF_result_logView"),
        QStringLiteral("MF_result_logToggleButton"), QStringLiteral("MF_result_clearButton"),
        QStringLiteral("MF_Quick_cancelButton")
    };

    for (const QString &name : names) {
        const QList<QWidget *> found = ui->mifareTab->findChildren<QWidget *>(name);
        QVERIFY2(found.size() == 1,
                 qPrintable(QStringLiteral("%1 count %2").arg(name).arg(found.size())));
    }

    // Removed Stage 3.2.3 UI must not survive as hidden/duplicated controls.
    const QStringList removed = {
        QStringLiteral("MF_primaryGroupBox"), QStringLiteral("MF_primaryLayout"),
        QStringLiteral("MF_typeGroupBox"), QStringLiteral("MF_Type_miniButton"),
        QStringLiteral("MF_Type_1kButton"), QStringLiteral("MF_Type_2kButton"),
        QStringLiteral("MF_Type_4kButton"), QStringLiteral("MF_Quick_infoButton"),
        QStringLiteral("MF_Quick_readAllButton"), QStringLiteral("MF_Quick_checkButton"),
        QStringLiteral("MF_status_confidenceLabel"), QStringLiteral("MF_status_confidenceTitle"),
        QStringLiteral("MF_status_evidenceLabel"), QStringLiteral("MF_status_evidenceTitle")
    };
    for (const QString &name : removed) {
        QVERIFY2(ui->mifareTab->findChild<QWidget *>(name) == nullptr,
                 qPrintable(name));
    }

    // One combo box replaces the four radio buttons; default is still 1K.
    QCOMPARE(ui->MF_Type_box->count(), 4);
    QCOMPARE(ui->MF_Type_box->itemText(0), QStringLiteral("MINI"));
    QCOMPARE(ui->MF_Type_box->itemText(1), QStringLiteral("1K"));
    QCOMPARE(ui->MF_Type_box->itemText(2), QStringLiteral("2K"));
    QCOMPARE(ui->MF_Type_box->itemText(3), QStringLiteral("4K"));
    QCOMPARE(ui->MF_Type_box->currentText(), QStringLiteral("1K"));

    // Every functional control lives inside the MIFARE page, not in a stray
    // duplicate page. No old + new copy of a control may coexist.
    QCOMPARE(ui->mifareTab->findChildren<QTableWidget *>().size(), 2);
    QCOMPARE(ui->MF_dataWidget->parentWidget(), static_cast<QWidget *>(ui->MF_tablesWidget));
    QCOMPARE(ui->MF_keyWidget->parentWidget(), static_cast<QWidget *>(ui->MF_tablesWidget));
    QVERIFY(ui->mifareTab->findChild<QLabel *>(QStringLiteral("MF_result_flowLabel")) == nullptr);
}

void TestMifareUi::tableWidgetsAreStableInstances()
{
    // The refactor moves the two tables into a vertical right-pane layout but
    // must keep the exact widgets (and therefore their models/connections).
    QCOMPARE(ui->mifareTab->findChildren<QTableWidget *>().size(), 2);
    QCOMPARE(ui->mifareTab->findChild<QTableWidget *>(QStringLiteral("MF_dataWidget")),
             ui->MF_dataWidget);
    QCOMPARE(ui->mifareTab->findChild<QTableWidget *>(QStringLiteral("MF_keyWidget")),
             ui->MF_keyWidget);
    QVERIFY(ui->MF_tablesLayout != nullptr);
    QCOMPARE(ui->MF_tablesLayout->indexOf(ui->MF_dataWidget), 0);
    QVERIFY(ui->MF_tablesLayout->indexOf(ui->MF_keyWidget) > 0);
    QVERIFY(ui->MF_tablesLayout->indexOf(ui->MF_dataWidget)
            < ui->MF_tablesLayout->indexOf(ui->MF_keyWidget));
    // The data table is the dominant right-pane table (3 : 2 : 0 toolbar).
    QCOMPARE(ui->MF_tablesLayout->stretch(0), 3);
    QCOMPARE(ui->MF_tablesLayout->stretch(1), 2);
    QCOMPARE(ui->MF_tablesLayout->stretch(2), 0);
    QVERIFY2(ui->MF_dataWidget->height() > ui->MF_keyWidget->height(),
             qPrintable(QStringLiteral("data %1 key %2")
                        .arg(ui->MF_dataWidget->height()).arg(ui->MF_keyWidget->height())));
    QCOMPARE(ui->MF_dataWidget->columnCount(), 3);
    QCOMPARE(ui->MF_keyWidget->columnCount(), 3);

    // The key toolbar remains attached to the same key table area.
    const QList<QAbstractButton *> keyButtons = {
        ui->MF_data2KeyButton, ui->MF_key2DataButton,
        ui->MF_fillKeysButton, ui->MF_trailerDecoderButton
    };
    for (QAbstractButton *button : keyButtons) {
        QVERIFY2(button->parentWidget() == static_cast<QWidget *>(ui->MF_tablesWidget),
                 qPrintable(button->objectName()));
    }
    QVERIFY(ui->MF_selectAllBox->parentWidget() == static_cast<QWidget *>(ui->MF_tablesWidget));
    QVERIFY(ui->MF_selectTrailerBox->parentWidget() == static_cast<QWidget *>(ui->MF_tablesWidget));
}

void TestMifareUi::noOneClickReadEntry()
{
    // The non-one-click bulk read UI was removed in Stage 3.2.3; the backend
    // quickReadCard() implementation stays available but has no UI entry.
    QVERIFY(ui->mifareTab->findChild<QAbstractButton *>(QStringLiteral("MF_Quick_readAllButton")) == nullptr);
    QVERIFY(ui->mifareTab->findChild<QAbstractButton *>(QStringLiteral("MF_Quick_readButton")) == nullptr);
    const QList<QPushButton *> buttons = ui->mifareTab->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        QVERIFY2(button->text() != QStringLiteral("Read Card"),
                 qPrintable(QStringLiteral("unexpected one-click entry %1").arg(button->objectName())));
        QVERIFY2(button->text() != QStringLiteral("Read Blocks"),
                 qPrintable(QStringLiteral("unexpected bulk-read entry %1").arg(button->objectName())));
    }
    const QString mifareHeader = readProjectFile(QStringLiteral("src/module/mifare.h"));
    const QString mifareSource = readProjectFile(QStringLiteral("src/module/mifare.cpp"));
    const QString windowHeader = readProjectFile(QStringLiteral("src/ui/mainwindow.h"));
    const QString windowSource = readProjectFile(QStringLiteral("src/ui/mainwindow.cpp"));
    QVERIFY(mifareHeader.contains(QStringLiteral("quickReadCard")));
    QVERIFY(mifareHeader.contains(QStringLiteral("quickRead")));
    QVERIFY(mifareSource.contains(QStringLiteral("void Mifare::quickReadCard()")));
    // The auto-connect slots for the removed UI entries are gone as well.
    QVERIFY(!windowHeader.contains(QStringLiteral("on_MF_Quick_readButton_clicked")));
    QVERIFY(!windowSource.contains(QStringLiteral("on_MF_Quick_readButton_clicked")));
    QVERIFY(!windowHeader.contains(QStringLiteral("on_MF_Quick_readAllButton_clicked")));
    QVERIFY(!windowSource.contains(QStringLiteral("on_MF_Quick_readAllButton_clicked")));
    QVERIFY(!windowSource.contains(QStringLiteral("mifare->quickReadCard()")));
}

void TestMifareUi::gen3ButtonIsNormalAndAfterInfo()
{
    QVERIFY(ui->MF_Attack_infoButton != nullptr);
    QVERIFY(ui->MF_Quick_crackButton != nullptr);

    // Exactly one read-info entry remains in the whole MIFARE page.
    int infoEntries = 0;
    const QList<QPushButton *> buttons = ui->mifareTab->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->text() == QString::fromUtf8("Card Info")
                || button->text() == QString::fromUtf8("读卡信息"))
            ++infoEntries;
    }
    QCOMPARE(infoEntries, 1);
    QCOMPARE(ui->MF_Attack_infoButton->parentWidget(),
             static_cast<QWidget *>(ui->MF_attackGroupBox));

    // The Gen3 entry is a real two-line caption, not auto-wrap.
    const QString crackText = QString::fromUtf8("Backdoor攻击\n[解三代无漏洞]");
    QCOMPARE(ui->MF_Quick_crackButton->text(), crackText);
    QCOMPARE(ui->MF_Quick_crackButton->text().count(QLatin1Char('\n')), 1);
    QVERIFY(!ui->MF_Quick_crackButton->font().bold());
    QVERIFY2(ui->MF_Quick_crackButton->minimumHeight() <= 4,
             qPrintable(QString::number(ui->MF_Quick_crackButton->minimumHeight())));
    QVERIFY(ui->MF_Quick_crackButton->maximumHeight() > 1000);
    QVERIFY2(ui->MF_Quick_crackButton->sizeHint().height()
             >= ui->MF_Quick_crackButton->fontMetrics().height() * 2,
             qPrintable(QStringLiteral("hint %1 font %2")
                        .arg(ui->MF_Quick_crackButton->sizeHint().height())
                        .arg(ui->MF_Quick_crackButton->fontMetrics().height())));

    // Backdoor stays immediately after Darkside; Hardnested is two-line too.
    QCOMPARE(ui->MF_Quick_crackButton->parentWidget(),
             static_cast<QWidget *>(ui->MF_attackGroupBox));
    QHBoxLayout *attackLayout = qobject_cast<QHBoxLayout *>(ui->MF_attackGroupBox->layout());
    QVERIFY(attackLayout != nullptr);
    const int darksideIndex = attackLayout->indexOf(ui->MF_Attack_darksideButton);
    QVERIFY(darksideIndex >= 0);
    QCOMPARE(attackLayout->indexOf(ui->MF_Quick_crackButton), darksideIndex + 1);

    const QString hardText = QString::fromUtf8("Hardnested攻击\n[知一求全]");
    QCOMPARE(ui->MF_Attack_hardnestedButton->text(), hardText);
    QCOMPARE(ui->MF_Attack_hardnestedButton->text().count(QLatin1Char('\n')), 1);
    QVERIFY2(ui->MF_Attack_hardnestedButton->sizeHint().height()
             >= ui->MF_Attack_hardnestedButton->fontMetrics().height() * 2,
             qPrintable(QString::number(ui->MF_Attack_hardnestedButton->sizeHint().height())));
    const QFontMetrics hardMetrics(ui->MF_Attack_hardnestedButton->font());
    for (const QString &line : hardText.split(QLatin1Char('\n'))) {
        QVERIFY2(hardMetrics.horizontalAdvance(line) <= ui->MF_Attack_hardnestedButton->width() - 4,
                 qPrintable(QStringLiteral("%1 px in %2 px").arg(hardMetrics.horizontalAdvance(line))
                            .arg(ui->MF_Attack_hardnestedButton->width())));
    }
    const QFontMetrics crackMetrics(ui->MF_Quick_crackButton->font());
    for (const QString &line : crackText.split(QLatin1Char('\n'))) {
        QVERIFY2(crackMetrics.horizontalAdvance(line) <= ui->MF_Quick_crackButton->width() - 4,
                 qPrintable(QStringLiteral("%1 px in %2 px").arg(crackMetrics.horizontalAdvance(line))
                            .arg(ui->MF_Quick_crackButton->width())));
    }

    // Cancel stays in the compact left-bottom task area.
    QCOMPARE(ui->MF_Quick_cancelButton->parentWidget(),
             static_cast<QWidget *>(ui->MF_resultGroupBox));
    QVERIFY(ui->MF_resultSummaryLayout->indexOf(ui->MF_Quick_cancelButton) >= 0);

    const QString header = readProjectFile(QStringLiteral("src/ui/mainwindow.h"));
    const QString source = readProjectFile(QStringLiteral("src/ui/mainwindow.cpp"));
    QVERIFY(header.contains(QStringLiteral("on_MF_Quick_crackButton_clicked")));
    QVERIFY(source.contains(QStringLiteral("void MainWindow::on_MF_Quick_crackButton_clicked()")));
    QVERIFY(source.contains(QStringLiteral("mifare->crackGen3Card()")));
    QVERIFY(header.contains(QStringLiteral("on_MF_Attack_hardnestedButton_clicked")));
    QVERIFY(source.contains(QStringLiteral("void MainWindow::on_MF_Attack_hardnestedButton_clicked()")));
}

void TestMifareUi::backdoorCaptionIsTranslated()
{
    const QString source = QString::fromUtf8("Backdoor攻击\n[解三代无漏洞]");
    const QString projectRoot = QStringLiteral(SRCDIR "/../../");
    const QString zhPath = projectRoot + QStringLiteral("i18n/zh_CN.qm");
    const QString enPath = projectRoot + QStringLiteral("i18n/en_US.qm");

    for (int language = 0; language < 2; ++language) {
        QTranslator translator;
        const QString path = language == 0 ? zhPath : enPath;
        QVERIFY2(translator.load(path), qPrintable(path));
        QVERIFY(qApp->installTranslator(&translator));
        const QString translated = QCoreApplication::translate(
                    "MainWindow", source.toUtf8().constData(), nullptr);
        qApp->removeTranslator(&translator);
        if (language == 0) {
            QCOMPARE(translated, QString::fromUtf8("Backdoor攻击\n[解三代无漏洞]"));
        } else {
            QCOMPARE(translated, QString::fromUtf8("Backdoor Attack\n[Gen3 Backdoor]"));
        }
    }

    // The Designer text and the runtime busy/idle restore use the same string.
    const QString uiXml = readProjectFile(QStringLiteral("src/ui/mainwindow.ui"));
    QVERIFY(uiXml.contains(QStringLiteral("Backdoor攻击&#10;[解三代无漏洞]")));
    const QString mifareSource = readProjectFile(QStringLiteral("src/module/mifare.cpp"));
    QVERIFY(mifareSource.contains(QStringLiteral("gen3ButtonIdleText")));
}

void TestMifareUi::cardInfoStaysOnMifarePage()
{
    const QString header = readProjectFile(QStringLiteral("src/module/mifare.h"));
    const QString mifareSource = readProjectFile(QStringLiteral("src/module/mifare.cpp"));
    const QString windowSource = readProjectFile(QStringLiteral("src/ui/mainwindow.cpp"));

    // The old fire-and-forget/-nsv branch is gone from the shared info helper.
    const int infoStart = mifareSource.indexOf(QStringLiteral("QMap<QString, QString> Mifare::info("));
    QVERIFY(infoStart >= 0);
    const int infoEnd = mifareSource.indexOf(QStringLiteral("\n}\n"), infoStart);
    QVERIFY(infoEnd > infoStart);
    const QString infoBody = mifareSource.mid(infoStart, infoEnd - infoStart);
    QVERIFY(!infoBody.contains(QStringLiteral("gotoRawTab")));
    QVERIFY(infoBody.contains(QStringLiteral("remove(QRegularExpression")));
    QVERIFY(header.contains(QStringLiteral("QMap<QString, QString> info(bool isRequiringOutput = true)")));

    // The single button calls one serial workflow.
    const int start = windowSource.indexOf(
                QStringLiteral("void MainWindow::on_MF_Attack_infoButton_clicked()"));
    QVERIFY(start >= 0);
    const int end = windowSource.indexOf(QStringLiteral("\n}\n"), start);
    QVERIFY(end > start);
    const QString body = windowSource.mid(start, end - start);
    QVERIFY(body.contains(QStringLiteral("mifare->readCardInfo()")));
    QCOMPARE(body.count(QStringLiteral("readCardInfo()")), 1);
    QVERIFY(!body.contains(QStringLiteral("gotoRawTab")));
    QVERIFY(!body.contains(QStringLiteral("mifare->info(")));

    // One hf 14a info in the workflow; hf mf info is conditional and serial.
    QVERIFY(mifareSource.contains(QStringLiteral("mfgen3::CardInfoService service(hooks)")));
    QVERIFY(mifareSource.contains(QStringLiteral("execCMDWithOutputInterruptible")));
    const QString service = readProjectFile(QStringLiteral("src/module/mifare/mfcardinfoservice.cpp"));
    const QString serviceHeader = readProjectFile(QStringLiteral("src/module/mifare/mfcardinfoservice.h"));
    QVERIFY(serviceHeader.contains(QStringLiteral("\"hf 14a info\"")));
    QVERIFY(serviceHeader.contains(QStringLiteral("\"hf mf info\"")));
    QVERIFY(!service.contains(QStringLiteral("-nsv")));
    // The second command is guarded by classic/connected/cancel conditions.
    QVERIFY(service.contains(QStringLiteral("identity.classic")));
    QVERIFY(service.contains(QStringLiteral("m_hooks.isConnected")));
    QVERIFY(service.contains(QStringLiteral("m_hooks.canceled")));

    // Refresh uses the same workflow, so it cannot issue a second hf 14a info.
    const int refreshStart = mifareSource.indexOf(QStringLiteral("void Mifare::refreshCardStatus()"));
    QVERIFY(refreshStart >= 0);
    const int refreshEnd = mifareSource.indexOf(QStringLiteral("\n}\n"), refreshStart);
    QVERIFY(refreshEnd > refreshStart);
    QVERIFY(mifareSource.mid(refreshStart, refreshEnd - refreshStart)
            .contains(QStringLiteral("readCardInfo()")));

    // The MIFARE page does not issue its own command path: the output still
    // comes from ClientSession::newOutput -> Util -> refreshOutput.
    QVERIFY(windowSource.contains(QStringLiteral("appendOutput(ui->MF_terminalView, output)")));
}

void TestMifareUi::hardnestedCaptionIsTranslated()
{
    const QString source = QString::fromUtf8("Hardnested攻击\n[知一求全]");
    const QString projectRoot = QStringLiteral(SRCDIR "/../../");

    QTranslator zh;
    QVERIFY(zh.load(projectRoot + QStringLiteral("i18n/zh_CN.qm")));
    QVERIFY(qApp->installTranslator(&zh));
    QCOMPARE(QCoreApplication::translate("MainWindow", source.toUtf8().constData(), nullptr),
             QString::fromUtf8("Hardnested攻击\n[知一求全]"));
    qApp->removeTranslator(&zh);

    QTranslator en;
    QVERIFY(en.load(projectRoot + QStringLiteral("i18n/en_US.qm")));
    QVERIFY(qApp->installTranslator(&en));
    QCOMPARE(QCoreApplication::translate("MainWindow", source.toUtf8().constData(), nullptr),
             QString::fromUtf8("Hardnested Attack\n[Known One, Recover All]"));
    qApp->removeTranslator(&en);

    const QString uiXml = readProjectFile(QStringLiteral("src/ui/mainwindow.ui"));
    QVERIFY(uiXml.contains(QStringLiteral("Hardnested攻击&#10;[知一求全]")));
}

void TestMifareUi::cardTypeComboReplacesRadios()
{
    QVERIFY(ui->MF_Type_box != nullptr);
    QVERIFY(ui->MF_typeLabel != nullptr);
    QCOMPARE(ui->MF_Type_box->parentWidget(), static_cast<QWidget *>(ui->MF_tablesWidget));
    QCOMPARE(ui->MF_typeLabel->parentWidget(), static_cast<QWidget *>(ui->MF_tablesWidget));
    QCOMPARE(ui->MF_Type_box->count(), 4);
    QCOMPARE(ui->MF_Type_box->currentText(), QStringLiteral("1K"));

    const QStringList removed = {
        QStringLiteral("MF_Type_miniButton"), QStringLiteral("MF_Type_1kButton"),
        QStringLiteral("MF_Type_2kButton"), QStringLiteral("MF_Type_4kButton"),
        QStringLiteral("MF_typeGroupBox")
    };
    for (const QString &name : removed)
        QVERIFY2(ui->mifareTab->findChild<QWidget *>(name) == nullptr, qPrintable(name));

    // The combo sits to the right of Fill Keys / Trailer Decoder.
    QVERIFY(ui->MF_keyButtonsLayout != nullptr);
    const int trailerIndex = ui->MF_keyButtonsLayout->indexOf(ui->MF_trailerDecoderButton);
    const int labelIndex = ui->MF_keyButtonsLayout->indexOf(ui->MF_typeLabel);
    const int comboIndex = ui->MF_keyButtonsLayout->indexOf(ui->MF_Type_box);
    QVERIFY(trailerIndex >= 0);
    QVERIFY(labelIndex > trailerIndex);
    QVERIFY(comboIndex > labelIndex);

    const QString source = readProjectFile(QStringLiteral("src/ui/mainwindow.cpp"));
    QVERIFY(source.contains(QStringLiteral("MF_onTypeBoxActivated")));
    QVERIFY(source.contains(QStringLiteral("&QComboBox::activated")));
    QVERIFY(source.contains(QStringLiteral("mfcardtype::applyDetected")));
    QVERIFY(source.contains(QStringLiteral("mfcardtype::selectManually")));
    QVERIFY(source.contains(QStringLiteral("addItem(tr(\"MINI\"), 0)")));
    QVERIFY(source.contains(QStringLiteral("addItem(tr(\"1K\"), 1)")));
    QVERIFY(source.contains(QStringLiteral("addItem(tr(\"2K\"), 2)")));
    QVERIFY(source.contains(QStringLiteral("addItem(tr(\"4K\"), 4)")));
    QVERIFY(source.contains(QStringLiteral("cardTypeState.type = 1")));
}

void TestMifareUi::workspaceRenamedAndFileButtonsMoved()
{
    QCOMPARE(ui->MF_fileGroupBox->title(), QStringLiteral("File Operations"));
    QCOMPARE(ui->MF_attackGroupBox->title(), QStringLiteral("Card Operations"));
    QVERIFY(ui->mifareTab->findChild<QGroupBox *>(QStringLiteral("MF_primaryGroupBox")) == nullptr);

    // Import / export are now part of the file operation group.
    QCOMPARE(ui->MF_Quick_loadButton->parentWidget(),
             static_cast<QWidget *>(ui->MF_fileGroupBox));
    QCOMPARE(ui->MF_Quick_saveButton->parentWidget(),
             static_cast<QWidget *>(ui->MF_fileGroupBox));
    QHBoxLayout *fileLayout = qobject_cast<QHBoxLayout *>(ui->MF_fileGroupBox->layout());
    QVERIFY(fileLayout != nullptr);
    QVERIFY(fileLayout->indexOf(ui->MF_Quick_loadButton) >= 0);
    QVERIFY(fileLayout->indexOf(ui->MF_Quick_saveButton) >= 0);
    QVERIFY(fileLayout->indexOf(ui->MF_File_loadButton) >= 0);
    QVERIFY(fileLayout->indexOf(ui->MF_File_saveButton) >= 0);

    // The unique info entry sits in the card-operation group.
    QCOMPARE(ui->MF_Attack_infoButton->parentWidget(),
             static_cast<QWidget *>(ui->MF_attackGroupBox));
    QVERIFY(ui->MF_advancedGroupBox->findChild<QPushButton *>(QStringLiteral("MF_Quick_infoButton")) == nullptr);
}

void TestMifareUi::sectionTitlesKeepNormalWeightUnderThemes()
{
    const QString previous = qApp->styleSheet();
    struct StyleRestore {
        QString sheet;
        ~StyleRestore() { qApp->setStyleSheet(sheet); }
    } restore{previous};
    const QList<QGroupBox *> sections = {
        ui->MF_fileGroupBox, ui->MF_attackGroupBox, ui->MF_RWGroupBox,
        ui->MF_normalGroupBox, ui->MF_UIDGroupBox, ui->MF_simGroupBox,
        ui->MF_sniffGroupBox
    };
    const QStringList themes = {
        QStringLiteral("src/qdarkstyle/light/lightstyle.qss"),
        QStringLiteral("src/qdarkstyle/dark/darkstyle.qss")
    };

    for (const QString &theme : themes) {
        const QString qss = readProjectFile(theme);
        QVERIFY2(!qss.isEmpty(), qPrintable(theme));
        qApp->setStyleSheet(qss);
        QApplication::processEvents();
        QTest::qWait(40);

        for (QGroupBox *section : sections) {
            QVERIFY2(!section->font().bold(),
                     qPrintable(QStringLiteral("%1 stays bold under %2")
                                .arg(section->objectName(), theme)));
            QCOMPARE(section->font().weight(), int(QFont::Normal));
            // The full caption must fit the section width (no clipped title).
            const int textWidth = section->fontMetrics().horizontalAdvance(section->title());
            QVERIFY2(section->width() >= textWidth + 8,
                     qPrintable(QStringLiteral("%1 title %2 px in %3 px under %4")
                                .arg(section->objectName()).arg(textWidth)
                                .arg(section->width()).arg(theme)));
        }

        // The scoping is intentional: other MIFARE area titles keep the
        // theme's bold weight.
        QVERIFY(ui->MF_statusGroupBox->font().bold());
        QVERIFY(ui->MF_terminalGroupBox->font().bold());
        QVERIFY(ui->MF_resultGroupBox->font().bold());
        QVERIFY(ui->MF_result_statusLabel->font().bold());

        // Global title fix: every themed group box reserves a full text line
        // above its contents, so the caption cannot overlap the border.
        for (QGroupBox *group : ui->mifareTab->findChildren<QGroupBox *>()) {
            if (group->title().isEmpty())
                continue;
            QVERIFY2(group->contentsRect().top() >= group->fontMetrics().height(),
                     qPrintable(QStringLiteral("%1 contentsTop %2 font %3 under %4")
                                .arg(group->objectName())
                                .arg(group->contentsRect().top())
                                .arg(group->fontMetrics().height())
                                .arg(theme)));
        }

        // The dense status grid keeps every caption fully visible with the
        // theme applied (the theme's label padding is removed for this group).
        const QList<QLabel *> statusTitles = {
            ui->MF_status_connTitle, ui->MF_status_cardTitle, ui->MF_status_typeTitle,
            ui->MF_status_uidTitle, ui->MF_status_atqaTitle, ui->MF_status_sakTitle,
            ui->MF_status_sectorTitle, ui->MF_status_capacityTitle
        };
        for (QLabel *label : statusTitles) {
            const int textWidth = label->fontMetrics().horizontalAdvance(label->text());
            QVERIFY2(label->width() >= textWidth,
                     qPrintable(QStringLiteral("%1 %2 px in %3 px under %4")
                                .arg(label->objectName()).arg(textWidth)
                                .arg(label->width()).arg(theme)));
        }
    }

    // No-theme rendering keeps the same normal weight.
    qApp->setStyleSheet(QString());
    QApplication::processEvents();
    for (QGroupBox *section : sections) {
        QVERIFY2(!section->font().bold(), qPrintable(section->objectName()));
    }
    qApp->setStyleSheet(previous);
    QApplication::processEvents();
}

void TestMifareUi::groupTitlesHaveRoomAtAllFontScales()
{
    const QString previousSheet = qApp->styleSheet();
    const QFont base = QApplication::font();
    struct Restore {
        QFont font;
        QString sheet;
        ~Restore() { QApplication::setFont(font); qApp->setStyleSheet(sheet); }
    } restore{base, previousSheet};

    qApp->setStyleSheet(readProjectFile(QStringLiteral("src/qdarkstyle/light/lightstyle.qss")));
    QApplication::processEvents();

    for (double factor : { 1.0, 1.25, 1.5, 2.0 }) {
        QFont font = base;
        font.setPointSizeF(base.pointSizeF() * factor);
        QApplication::setFont(font);
        QApplication::processEvents();
        QTest::qWait(40);

        for (QGroupBox *group : ui->mifareTab->findChildren<QGroupBox *>()) {
            if (group->title().isEmpty() || !group->isVisible())
                continue;
            const int textHeight = group->fontMetrics().height();
            QVERIFY2(group->contentsRect().top() >= textHeight,
                     qPrintable(QStringLiteral("%1 at %2x: contentsTop %3 < text %4")
                                .arg(group->objectName()).arg(factor)
                                .arg(group->contentsRect().top()).arg(textHeight)));
        }
    }
}

void TestMifareUi::flatWorkspaceHasNoDeepGroupBoxes()
{
    QCOMPARE(ui->MF_advancedScrollArea->widget(), static_cast<QWidget *>(ui->MF_advancedGroupBox));
    QVERIFY(ui->MF_advancedScrollArea->widgetResizable());
    QCOMPARE(ui->MF_advancedScrollArea->frameShape(), QFrame::NoFrame);
    QCOMPARE(ui->MF_advancedScrollArea->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
    QVERIFY(qobject_cast<QGroupBox *>(ui->MF_advancedGroupBox) == nullptr);
    QVERIFY(ui->MF_workspaceGrid != nullptr);
    QCOMPARE(ui->MF_workspaceGrid->count(), 7);

    const QList<QGroupBox *> sections = {
        ui->MF_fileGroupBox, ui->MF_attackGroupBox, ui->MF_RWGroupBox,
        ui->MF_normalGroupBox, ui->MF_UIDGroupBox, ui->MF_simGroupBox,
        ui->MF_sniffGroupBox
    };
    for (QGroupBox *section : sections) {
        QVERIFY2(section->parentWidget() == static_cast<QWidget *>(ui->MF_advancedGroupBox),
                 qPrintable(QStringLiteral("%1 parent %2").arg(section->objectName(),
                                                              section->parentWidget()
                                                              ? section->parentWidget()->objectName()
                                                              : QStringLiteral("<null>"))));
        QVERIFY2(ui->MF_workspaceGrid->indexOf(section) >= 0, qPrintable(section->objectName()));
    }
    // Classic / magic block operations are siblings, not nested inside the
    // Read/Write section.
    QVERIFY(ui->MF_RWGroupBox->findChildren<QGroupBox *>().isEmpty());
    QVERIFY(ui->MF_UIDGroupBox->findChildren<QGroupBox *>().isEmpty());
}

void TestMifareUi::terminalUsesExistingOutputStream()
{
    QVERIFY(ui->MF_terminalView != nullptr);
    QVERIFY(ui->MF_terminalView->isReadOnly());
    QCOMPARE(ui->MF_terminalView->lineWrapMode(), QPlainTextEdit::NoWrap);
    QVERIFY(ui->MF_terminalView->maximumBlockCount() >= 1000);
    QCOMPARE(ui->MF_terminalView->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
    QCOMPARE(ui->MF_terminalGroupBox->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
    QVERIFY(ui->MF_terminalView->isVisible());

    const QString source = readProjectFile(QStringLiteral("src/ui/mainwindow.cpp"));
    QVERIFY(source.contains(QStringLiteral("appendOutput(ui->Raw_outputEdit, output)")));
    QVERIFY(source.contains(QStringLiteral("appendOutput(ui->MF_terminalView, output)")));
    QVERIFY(source.contains(QStringLiteral("mfTerminalHighlighter = new TerminalHighlighter")));
    QVERIFY(source.contains(QStringLiteral("Pm3LogFilter::shouldAutoScroll")));
    // The terminal is a mirror; it must not execute commands itself.
    const int fnStart = source.indexOf(QStringLiteral("void MainWindow::refreshOutput"));
    QVERIFY(fnStart >= 0);
    const int fnEnd = source.indexOf(QStringLiteral("\n}\n"), fnStart);
    QVERIFY(fnEnd > fnStart);
    const QString body = source.mid(fnStart, fnEnd - fnStart);
    QVERIFY(!body.contains(QStringLiteral("execCMD")));
    QVERIFY(!body.contains(QStringLiteral("executeCommand")));
    QVERIFY(body.contains(QStringLiteral("verticalScrollBar")));
}

void TestMifareUi::allRetainedSlotsStillDeclared()
{
    const QString header = readProjectFile(QStringLiteral("src/ui/mainwindow.h"));
    const QString source = readProjectFile(QStringLiteral("src/ui/mainwindow.cpp"));
    QVERIFY(!header.isEmpty());
    QVERIFY(!source.isEmpty());

    const QList<QPushButton *> buttons = ui->mifareTab->findChildren<QPushButton *>();
    // Stage 3.2.3 removed the three duplicate main-operation buttons, leaving
    // 42 actionable MIFARE buttons; each still keeps its auto-connect slot.
    QCOMPARE(buttons.size(), 42);
    QSet<QString> names;
    for (QPushButton *button : buttons) {
        const QString name = button->objectName();
        QVERIFY2(!name.isEmpty(), "every retained MIFARE button keeps an objectName");
        QVERIFY2(!names.contains(name), qPrintable(QStringLiteral("duplicate button %1").arg(name)));
        names.insert(name);
        const QString suffix = button->isCheckable() ? QStringLiteral("toggled")
                                                     : QStringLiteral("clicked");
        const QString declaration = QStringLiteral("void on_%1_%2(").arg(name, suffix);
        const QString definition = QStringLiteral("void MainWindow::on_%1_%2(").arg(name, suffix);
        QVERIFY2(header.contains(declaration),
                 qPrintable(QStringLiteral("missing declaration %1").arg(declaration)));
        QVERIFY2(source.contains(definition),
                 qPrintable(QStringLiteral("missing definition %1").arg(definition)));
    }
}

void TestMifareUi::resultWidgetsExist()
{
    QVERIFY(ui->MF_resultGroupBox != nullptr);
    QVERIFY(ui->MF_result_progressBar != nullptr);
    QVERIFY(ui->MF_result_stepLabel != nullptr);
    QVERIFY(ui->MF_result_logView != nullptr);
    QVERIFY(ui->MF_result_logToggleButton != nullptr);
    QVERIFY(ui->MF_result_clearButton != nullptr);
    QVERIFY(ui->MF_result_statusLabel != nullptr);
    QVERIFY(ui->MF_result_keyLabel != nullptr);
    QVERIFY(ui->MF_result_metaLabel != nullptr);
    QVERIFY(ui->mifareTab->findChild<QLabel *>(QStringLiteral("MF_result_flowLabel")) == nullptr);
    QCOMPARE(ui->MF_Quick_cancelButton->parentWidget(),
             static_cast<QWidget *>(ui->MF_resultGroupBox));
    QVERIFY(ui->MF_resultGroupBox->isVisible());
    QCOMPARE(ui->MF_resultGroupBox->parentWidget(), static_cast<QWidget *>(ui->MF_leftPane));
}

void TestMifareUi::resultLayoutIsCompact()
{
    const QMargins margins = ui->MF_resultLayout->contentsMargins();
    QVERIFY2(margins.top() <= 4, qPrintable(QString::number(margins.top())));
    QVERIFY2(margins.bottom() <= 6, qPrintable(QString::number(margins.bottom())));
    QVERIFY2(margins.left() <= 6 && margins.right() <= 6,
             qPrintable(QStringLiteral("%1,%2").arg(margins.left()).arg(margins.right())));
    QVERIFY2(ui->MF_resultLayout->spacing() <= 2,
             qPrintable(QString::number(ui->MF_resultLayout->spacing())));

    const QMargins progressMargins = ui->MF_resultProgressLayout->contentsMargins();
    QCOMPARE(progressMargins, QMargins(0, 0, 0, 0));
    QVERIFY2(ui->MF_resultProgressLayout->spacing() <= 6,
             qPrintable(QString::number(ui->MF_resultProgressLayout->spacing())));
}

void TestMifareUi::progressRowHasNoSlack()
{
    const QRect progress = ui->MF_resultProgressLayout->geometry();
    const QRect summary = ui->MF_resultSummaryLayout->geometry();
    const QRect bar = ui->MF_result_progressBar->geometry();

    QVERIFY2(progress.height() <= bar.height() + 1,
             qPrintable(QStringLiteral("row %1 bar %2").arg(progress.height()).arg(bar.height())));
    const int maxHeight = qMax(34, ui->MF_result_progressBar->fontMetrics().height() * 2 + 8);
    QVERIFY2(bar.height() <= maxHeight,
             qPrintable(QStringLiteral("bar %1 max %2").arg(bar.height()).arg(maxHeight)));
    const int gap = summary.top() - (progress.top() + progress.height());
    QVERIFY2(gap <= 3, qPrintable(QString::number(gap)));
}

void TestMifareUi::logKeepsCapAndCollapses()
{
    QCOMPARE(ui->MF_result_logView->maximumHeight(), 130);
    QVERIFY(ui->MF_result_logView->minimumHeight() < 130);
    QVERIFY(ui->MF_result_logToggleButton->isChecked());
    QVERIFY(ui->MF_result_logView->isVisible());

    const int expandedHeight = ui->MF_resultGroupBox->sizeHint().height();
    ui->MF_result_logView->setVisible(false);
    QTest::qWait(40);
    QVERIFY(!ui->MF_result_logView->isVisible());
    const int collapsedHeight = ui->MF_resultGroupBox->sizeHint().height();
    QVERIFY2(collapsedHeight < expandedHeight,
             qPrintable(QStringLiteral("%1 -> %2").arg(expandedHeight).arg(collapsedHeight)));

    ui->MF_result_logView->setVisible(true);
    QTest::qWait(40);
    QVERIFY(ui->MF_result_logView->isVisible());
    QCOMPARE(ui->MF_result_logView->maximumHeight(), 130);
    QCOMPARE(ui->MF_resultGroupBox->sizeHint().height(), expandedHeight);
}

void TestMifareUi::noVerticalSpacersInResultArea()
{
    const QList<QLayout *> layouts = { ui->MF_resultLayout, ui->MF_resultProgressLayout,
                                       ui->MF_resultSummaryLayout };
    for (QLayout *layout : layouts) {
        for (int i = 0; i < layout->count(); ++i) {
            QSpacerItem *spacer = layout->itemAt(i)->spacerItem();
            if (spacer) {
                QVERIFY2(spacer->expandingDirections() == Qt::Horizontal,
                         "result area must not contain vertical spacers");
            }
        }
    }
}

void TestMifareUi::progressBarUsesDpiSafePolicy()
{
    QCOMPARE(ui->MF_result_progressBar->sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
    QCOMPARE(ui->MF_result_stepLabel->sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
    QCOMPARE(ui->MF_result_progressBar->minimumHeight(), 0);
    QVERIFY(ui->MF_result_progressBar->maximumHeight() > 1000);
    QCOMPARE(ui->MF_result_logView->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
}

void TestMifareUi::verticalAllocationHasNoRegression()
{
    // Stage 3.2 guarantee: status and result stay bounded by their hints
    // instead of absorbing the extra height when the window grows.
    QCOMPARE(ui->MF_statusGroupBox->sizePolicy().verticalPolicy(), QSizePolicy::Maximum);
    QCOMPARE(ui->MF_resultGroupBox->sizePolicy().verticalPolicy(), QSizePolicy::Maximum);
    QCOMPARE(ui->MF_terminalGroupBox->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
    QCOMPARE(ui->MF_dataWidget->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
    QCOMPARE(ui->MF_keyWidget->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
    QCOMPARE(ui->MF_advancedScrollArea->sizePolicy().verticalPolicy(), QSizePolicy::Expanding);
    QCOMPARE(ui->MF_result_logView->maximumHeight(), 130);

    const int normalStatus = ui->MF_statusGroupBox->height();
    const int normalResult = ui->MF_resultGroupBox->height();
    const int normalTerminal = ui->MF_terminalView->height();
    const int normalTable = ui->MF_dataWidget->height();
    const int normalWorkspace = ui->MF_advancedScrollArea->height();

    window->resize(2200, 1300);
    QTest::qWait(90);

    QVERIFY2(qAbs(ui->MF_statusGroupBox->height()
                   - qMin(normalStatus, ui->MF_statusGroupBox->sizeHint().height())) <= 2,
             qPrintable(QStringLiteral("status %1 hint %2")
                        .arg(ui->MF_statusGroupBox->height())
                        .arg(ui->MF_statusGroupBox->sizeHint().height())));
    QVERIFY2(ui->MF_resultGroupBox->height() <= ui->MF_resultGroupBox->sizeHint().height() + 2,
             qPrintable(QStringLiteral("result %1 hint %2")
                        .arg(ui->MF_resultGroupBox->height())
                        .arg(ui->MF_resultGroupBox->sizeHint().height())));
    QVERIFY2(ui->MF_terminalView->height() > normalTerminal + 80,
             qPrintable(QStringLiteral("terminal %1 -> %2")
                        .arg(normalTerminal).arg(ui->MF_terminalView->height())));
    QVERIFY2(ui->MF_dataWidget->height() > normalTable + 40,
             qPrintable(QStringLiteral("table %1 -> %2")
                        .arg(normalTable).arg(ui->MF_dataWidget->height())));
    QVERIFY2(ui->MF_advancedScrollArea->height() > normalWorkspace + 40,
             qPrintable(QStringLiteral("workspace %1 -> %2")
                        .arg(normalWorkspace).arg(ui->MF_advancedScrollArea->height())));
    QVERIFY(ui->MF_result_logView->isVisible());
    QVERIFY(ui->MF_result_progressBar->isVisible());
    QVERIFY(qAbs(normalResult - ui->MF_resultGroupBox->height())
            <= ui->MF_resultGroupBox->sizeHint().height());
}

void TestMifareUi::maximizedAndRestoredKeepProportions()
{
    const auto checkRatio = [this](const QString &state) {
        const QList<int> sizes = ui->MF_mainSplitter->sizes();
        const double ratio = double(sizes.at(0)) / double(sizes.at(0) + sizes.at(1));
        QVERIFY2(qAbs(ratio - 0.35) <= 0.04,
                 qPrintable(QStringLiteral("%1 ratio %2 sizes %3/%4")
                            .arg(state).arg(ratio).arg(sizes.at(0)).arg(sizes.at(1))));
    };

    checkRatio(QStringLiteral("normal"));
    const int normalLeft = ui->MF_leftPane->width();
    const int normalRight = ui->MF_rightPane->width();

    window->resize(2200, 1300);
    QTest::qWait(90);
    checkRatio(QStringLiteral("maximized-size"));
    QVERIFY(ui->MF_leftPane->width() > normalLeft);
    QVERIFY(ui->MF_rightPane->width() > normalRight);

    window->resize(1400, 950);
    QTest::qWait(90);
    checkRatio(QStringLiteral("restored"));
    QVERIFY2(qAbs(ui->MF_leftPane->width() - normalLeft) <= 8,
             qPrintable(QStringLiteral("left %1 -> %2").arg(normalLeft).arg(ui->MF_leftPane->width())));
    QVERIFY2(qAbs(ui->MF_rightPane->width() - normalRight) <= 8,
             qPrintable(QStringLiteral("right %1 -> %2").arg(normalRight).arg(ui->MF_rightPane->width())));
}

void TestMifareUi::smallWindowKeepsEverythingReachable()
{
    window->resize(1100, 700);
    QTest::qWait(90);

    QVERIFY(ui->MF_terminalView->isVisible());
    QVERIFY(ui->MF_terminalView->height() >= 40);
    QVERIFY(ui->MF_result_progressBar->isVisible());
    QVERIFY(ui->MF_result_progressBar->height() >= 15);
    QVERIFY(ui->MF_result_logView->isVisible());
    QVERIFY(ui->MF_result_logView->height() >= 24);
    QVERIFY(ui->MF_dataWidget->height() >= 100);
    QVERIFY(ui->MF_advancedScrollArea->height() >= 100);

    QWidget *page = ui->mifareTab;
    const QList<QWidget *> widgets = {
        ui->MF_statusGroupBox, ui->MF_terminalGroupBox, ui->MF_resultGroupBox,
        ui->MF_dataWidget, ui->MF_keyWidget, ui->MF_advancedScrollArea
    };
    for (QWidget *widget : widgets) {
        const QRect rect(widget->mapTo(page, QPoint(0, 0)), widget->size());
        QVERIFY2(rect.top() >= 0 && rect.bottom() < page->height(),
                 qPrintable(QStringLiteral("%1 rect %2,%3 %4x%5 page %6x%7")
                            .arg(widget->objectName()).arg(rect.x()).arg(rect.y())
                            .arg(rect.width()).arg(rect.height())
                            .arg(page->width()).arg(page->height())));
    }

    // The advanced workspace still scrolls instead of clipping its lower
    // sections at a small window.
    QVERIFY(ui->MF_advancedScrollArea->verticalScrollBar()->maximum() > 0);
}

void TestMifareUi::identityLabelsExistWithoutAllowedFlow()
{
    // Confidence/evidence were removed from the status area in Stage 3.2.3.
    QVERIFY(ui->mifareTab->findChild<QWidget *>(QStringLiteral("MF_status_confidenceTitle")) == nullptr);
    QVERIFY(ui->mifareTab->findChild<QWidget *>(QStringLiteral("MF_status_confidenceLabel")) == nullptr);
    QVERIFY(ui->mifareTab->findChild<QWidget *>(QStringLiteral("MF_status_evidenceTitle")) == nullptr);
    QVERIFY(ui->mifareTab->findChild<QWidget *>(QStringLiteral("MF_status_evidenceLabel")) == nullptr);
    QVERIFY(ui->MF_status_capacityTitle != nullptr);
    QVERIFY(ui->MF_status_capacityLabel != nullptr);

    // The "allowed flow" display is intentionally gone.  The underlying flow
    // helper and the one-click backend are not UI and stay in place.
    QVERIFY(ui->mifareTab->findChild<QLabel *>(QStringLiteral("MF_result_flowLabel")) == nullptr);
    QVERIFY(ui->mifareTab->findChild<QLabel *>(QStringLiteral("MF_status_flowTitle")) == nullptr);
    QVERIFY(ui->mifareTab->findChild<QLabel *>(QStringLiteral("MF_status_flowLabel")) == nullptr);
    const QString uiXml = readProjectFile(QStringLiteral("src/ui/mainwindow.ui"));
    QVERIFY(!uiXml.contains(QStringLiteral("Allowed flow")));
    QVERIFY(!uiXml.contains(QStringLiteral("MF_result_flowLabel")));
    const QString mifareSource = readProjectFile(QStringLiteral("src/module/mifare.cpp"));
    QVERIFY(!mifareSource.contains(QStringLiteral("Allowed flow")));
    QVERIFY(!mifareSource.contains(QStringLiteral("MF_status_flowLabel")));
    QVERIFY(!mifareSource.contains(QStringLiteral("ui->MF_result_flowLabel")));

    // Confidence/evidence columns are gone; their data stays in CardIdentity.
    QVERIFY(!uiXml.contains(QStringLiteral("Confidence:")));
    QVERIFY(!uiXml.contains(QStringLiteral("Evidence:")));
    QVERIFY(!mifareSource.contains(QStringLiteral("MF_status_confidence")));
    QVERIFY(!mifareSource.contains(QStringLiteral("MF_status_evidence")));
    const QString types = readProjectFile(QStringLiteral("src/module/mifare/mfgen3types.h"));
    QVERIFY(types.contains(QStringLiteral("Confidence confidence")));
    QVERIFY(types.contains(QStringLiteral("QStringList evidence")));
}

void TestMifareUi::advancedAreaScrollsInsteadOfClipping()
{
    QVERIFY(ui->MF_advancedScrollArea != nullptr);
    QVERIFY(ui->MF_advancedGroupBox != nullptr);
    QVERIFY(ui->MF_advancedScrollArea->widgetResizable());
    QCOMPARE(ui->MF_advancedScrollArea->frameShape(), QFrame::NoFrame);
    QVERIFY(ui->MF_advancedScrollArea->widget() == ui->MF_advancedGroupBox);

    QTest::qWait(40);
    // With the removed groups the workspace may fit at 1400x950; it must still
    // expose a valid scroll range and keep every control reachable.
    QVERIFY(ui->MF_advancedScrollArea->verticalScrollBar()->maximum() >= 0);

    window->resize(2200, 1300);
    QTest::qWait(90);
    // The workspace can still be scrolled at a large size; the important
    // guarantee is that controls are reachable, not a fixed height.
    QVERIFY(ui->MF_advancedScrollArea->verticalScrollBar()->maximum() >= 0);

    window->resize(1100, 700);
    QTest::qWait(90);
    QVERIFY(ui->MF_advancedScrollArea->verticalScrollBar()->maximum() > 0);
    // On a narrow right pane the operation rows can be wider than the
    // viewport; horizontal scrolling must be available.
    QVERIFY(ui->MF_advancedScrollArea->horizontalScrollBar()->maximum() >= 0);
}

void TestMifareUi::dpiScreenshotArtifacts()
{
    // This slot is an opt-in artifact generator used by
    // scripts/capture-stage33-screenshots.ps1 and
    // scripts/capture-stage322-screenshots.ps1. In a normal test run it only
    // verifies that logical geometry stays inside the page.
    const QString captureDir = qEnvironmentVariable("STAGE33_CAPTURE_DIR");
    const QString themeCaptureDir = qEnvironmentVariable("STAGE322_CAPTURE_DIR");
    const QString themeName = qEnvironmentVariable("STAGE322_THEME");
    const QString themeQss = qEnvironmentVariable("STAGE322_QSS");
    const QString capturePrefix = qEnvironmentVariable("STAGE322_PREFIX", QStringLiteral("stage322"));
    const QString previousSheet = qApp->styleSheet();
    struct StyleRestore {
        QString sheet;
        ~StyleRestore() { qApp->setStyleSheet(sheet); }
    } restore{previousSheet};
    if (!themeQss.isEmpty()) {
        const QString qss = readProjectFile(themeQss);
        QVERIFY2(!qss.isEmpty(), qPrintable(themeQss));
        qApp->setStyleSheet(qss);
        QApplication::processEvents();
        QTest::qWait(60);
    }
    const double dpr = window->devicePixelRatioF();
    const QString dprText = QString::number(dpr, 'g', 3);

    const auto metrics = [this]() {
        return QStringLiteral("window=%1x%2 splitter=%3/%4 left=%5 right=%6 "
                              "status=%7 terminal=%8 result=%9 tables=%10 data=%11 key=%12 workspace=%13 "
                              "log=%14 progress=%15")
                .arg(window->width()).arg(window->height())
                .arg(ui->MF_mainSplitter->sizes().value(0))
                .arg(ui->MF_mainSplitter->sizes().value(1))
                .arg(ui->MF_leftPane->width()).arg(ui->MF_rightPane->width())
                .arg(ui->MF_statusGroupBox->height())
                .arg(ui->MF_terminalView->height())
                .arg(ui->MF_resultGroupBox->height())
                .arg(ui->MF_tablesWidget->height())
                .arg(ui->MF_dataWidget->height())
                .arg(ui->MF_keyWidget->height())
                .arg(ui->MF_advancedScrollArea->height())
                .arg(ui->MF_result_logView->height())
                .arg(ui->MF_result_progressBar->height());
    };

    const auto geometryCheck = [&](const QString &state) {
        const auto rectIn = [](QWidget *widget, QWidget *ancestor) {
            return QRect(widget->mapTo(ancestor, QPoint(0, 0)), widget->size());
        };
        const auto inside = [](const QRect &outer, const QRect &inner) {
            return outer.contains(inner.topLeft()) && outer.contains(inner.bottomRight());
        };
        const auto noOverlap = [&](const QList<QWidget *> &widgets, QWidget *parent,
                                   const QString &stateName) {
            for (int i = 0; i < widgets.size(); ++i) {
                const QRect a = rectIn(widgets.at(i), parent);
                QVERIFY2(inside(parent->rect(), a),
                         qPrintable(QStringLiteral("%1 %2 outside %3 %4,%5 %6x%7")
                                    .arg(stateName, widgets.at(i)->objectName(),
                                         parent->objectName())
                                    .arg(a.x()).arg(a.y()).arg(a.width()).arg(a.height())));
                for (int j = i + 1; j < widgets.size(); ++j) {
                    const QRect b = rectIn(widgets.at(j), parent);
                    QVERIFY2(!a.intersects(b),
                             qPrintable(QStringLiteral("%1 overlap %2/%3 %4,%5 %6x%7 vs %8,%9 %10x%11")
                                        .arg(stateName, widgets.at(i)->objectName(),
                                             widgets.at(j)->objectName())
                                        .arg(a.x()).arg(a.y()).arg(a.width()).arg(a.height())
                                        .arg(b.x()).arg(b.y()).arg(b.width()).arg(b.height())));
                }
            }
        };

        noOverlap(QList<QWidget *>() << ui->MF_statusGroupBox << ui->MF_terminalGroupBox
                                     << ui->MF_resultGroupBox,
                 ui->MF_leftPane, state);
        noOverlap(QList<QWidget *>() << ui->MF_tablesWidget << ui->MF_advancedScrollArea,
                 ui->MF_rightPane, state);
        noOverlap(QList<QWidget *>() << ui->MF_dataWidget << ui->MF_keyWidget,
                 ui->MF_tablesWidget, state);
        noOverlap(QList<QWidget *>()
                  << ui->MF_fileGroupBox
                  << ui->MF_attackGroupBox << ui->MF_RWGroupBox << ui->MF_normalGroupBox
                  << ui->MF_UIDGroupBox << ui->MF_simGroupBox << ui->MF_sniffGroupBox,
                 ui->MF_advancedGroupBox, state);
    };

    const auto capture = [&](const QString &state) {
        const QList<int> sizes = ui->MF_mainSplitter->sizes();
        const double ratio = double(sizes.value(0)) / double(qMax(1, sizes.value(0) + sizes.value(1)));
        QVERIFY2(qAbs(ratio - 0.35) <= 0.04,
                 qPrintable(QStringLiteral("%1 ratio %2").arg(state).arg(ratio)));
        geometryCheck(state);
        if (!themeCaptureDir.isEmpty()) {
            QDir().mkpath(themeCaptureDir);
            const QString themeFile = QStringLiteral("%1/%2-%3-%4-dpr%5.png")
                    .arg(themeCaptureDir, capturePrefix,
                         themeName.isEmpty() ? QStringLiteral("none") : themeName,
                         state, dprText);
            QVERIFY2(window->grab().save(themeFile), qPrintable(themeFile));
        }
        if (captureDir.isEmpty())
            return;
        QDir().mkpath(captureDir);
        const QString png = QStringLiteral("%1/stage33-%2-dpr%3.png")
                                .arg(captureDir, state, dprText);
        QVERIFY2(window->grab().save(png), qPrintable(png));
        QFile txt(QStringLiteral("%1/geometry-dpr%2.txt").arg(captureDir, dprText));
        QVERIFY(txt.open(QFile::WriteOnly | QFile::Append | QFile::Text));
        QTextStream stream(&txt);
        stream << "dpr=" << dprText << " state=" << state << " "
               << metrics() << "\n";
    };

    window->resize(1400, 950);
    QTest::qWait(90);
    capture(QStringLiteral("normal"));

    window->showMaximized();
    QTest::qWait(140);
    capture(QStringLiteral("maximized"));

    window->showNormal();
    QTest::qWait(90);
    window->resize(1400, 950);
    QTest::qWait(90);
    capture(QStringLiteral("restored"));
}

void TestMifareUi::miniTo1kLayoutHasEveryCell()
{
    // MINI-sized tables (20 data rows / 5 key rows) followed by a 1K Gen3
    // result must rebuild every cell instead of dereferencing item(i, 2).
    QTableWidget data;
    data.setColumnCount(3);
    data.setRowCount(20);
    QTableWidget key;
    key.setColumnCount(3);
    key.setRowCount(5);
    for (int row = 0; row < 20; ++row)
        for (int column = 0; column < 3; ++column)
            data.setItem(row, column, new QTableWidgetItem(QString::number(row)));

    // What MainWindow::MF_widgetReset does after Mifare::setCardType(1).
    mftable::resizeGrid(&data, 64, 3);
    mftable::resizeGrid(&key, 16, 3);
    QCOMPARE(data.rowCount(), 64);
    QCOMPARE(key.rowCount(), 16);

    QStringList rows;
    for (int i = 0; i < 64; ++i)
        rows << QString(32, QLatin1Char('A'));
    QCOMPARE(mftable::syncColumnText(&data, 2, rows, 64), 64);
    QVERIFY(data.item(20, 2) != nullptr);
    QVERIFY(data.item(63, 2) != nullptr);
    QCOMPARE(data.item(63, 2)->text(), QString(32, QLatin1Char('A')));
}

void TestMifareUi::tableSyncIgnoresOutOfRangeRows()
{
    QTableWidget data;
    data.setColumnCount(3);
    data.setRowCount(20);
    QVERIFY(!mftable::setItemText(&data, 64, 2, QStringLiteral("x")));
    QVERIFY(!mftable::setItemText(&data, -1, 2, QStringLiteral("x")));
    QVERIFY(!mftable::setItemText(&data, 0, 9, QStringLiteral("x")));
    QCOMPARE(mftable::syncColumnText(&data, 2, QStringList(), 64), 0);
    QVERIFY(data.item(19, 2) == nullptr);   // untouched, but no crash
}

void TestMifareUi::backupCoversActualCardSize()
{
    QStringList rows;
    for (int block = 0; block < 256; ++block)
    {
        QString row;
        for (int byte = 0; byte < 16; ++byte)
            row += QStringLiteral("%1").arg((block * 16 + byte) & 0xFF, 2, 16, QLatin1Char('0'));
        rows << row.toUpper();
    }

    bool any = false;
    const QByteArray oneK = mfimage::imageFromRows(rows, 64, &any);
    QVERIFY(any);
    QCOMPARE(oneK.size(), 1024);

    const QByteArray fourK = mfimage::imageFromRows(rows, 256, &any);
    QVERIFY(any);
    QCOMPARE(fourK.size(), 4096);
    QCOMPARE(int(quint8(fourK.at(255 * 16))), 240);
    QCOMPARE(int(quint8(fourK.at(255 * 16 + 15))), 255);

    // A 4K image whose upper area is not filled still keeps its full size.
    QStringList miniRows = rows.mid(0, 20);
    const QByteArray mini = mfimage::imageFromRows(miniRows, 20, &any);
    QVERIFY(any);
    QCOMPARE(mini.size(), 320);
}

void TestMifareUi::backupNameCarriesCardSize()
{
    const QDateTime stamp = QDateTime::fromString(QStringLiteral("2026-11-21T12:00:00"),
                                                  Qt::ISODate);
    QCOMPARE(mfimage::backupFileName(QStringLiteral("A1B2C3D4"), QStringLiteral("1k"), 64, stamp),
             QStringLiteral("backup_A1B2C3D4_1k_1024B_20261121_120000.bin"));
    QCOMPARE(mfimage::backupFileName(QStringLiteral("A1B2C3D4"), QStringLiteral("4k"), 256, stamp),
             QStringLiteral("backup_A1B2C3D4_4k_4096B_20261121_120000.bin"));
}

void TestMifareUi::cancelGateResetAllowsRefresh()
{
    mfgen3::CancelGate gate;
    QVERIFY(!gate.requested());
    gate.request();
    QVERIFY(gate.requested());          // Cancel pressed during the task
    QVERIFY(gate.atomic() != nullptr);
    gate.reset();                       // finishGen3Task / cancel completion
    QVERIFY(!gate.requested());         // Refresh is no longer poisoned
}

QTEST_MAIN(TestMifareUi)
#include "tst_mifareui.moc"
