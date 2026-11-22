#include <QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QGroupBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QToolBar>
#include <QToolButton>

#include "ui/commandcatalogdelegate.h"
#include "ui/rawworkspace.h"

class TestRawWorkspace : public QObject
{
    Q_OBJECT

private slots:
    void createsTwoIndependentDocks();
    void floatsAndRedocks();
    void closesAndReopens();
    void resetRestoresDefaultLayout();
    void savesAndRestoresState();
    void compactColumnIsClamped();
    void historyHiddenByDefaultWithoutReservedSpace();
    void historyToggleShowsAndHidesPanel();
    void historyKeepsEntriesWhileHidden();
    void historyEntrySignalDoesNotExecute();

private:
    struct Widgets {
        QPlainTextEdit *output = nullptr;
        QCheckBox *historyBox = nullptr;
        QListWidget *history = nullptr;
        QPushButton *clearHistory = nullptr;
        QLineEdit *commandEdit = nullptr;
        QPushButton *send = nullptr;
        QPushButton *clearOutput = nullptr;
        QGroupBox *catalog = nullptr;
    };
    static Widgets makeWidgets();
    static void adoptSyntheticWidgets(RawWorkspace *workspace);
};

TestRawWorkspace::Widgets TestRawWorkspace::makeWidgets()
{
    Widgets w;
    w.output = new QPlainTextEdit;
    w.historyBox = new QCheckBox(QStringLiteral("History"));
    w.history = new QListWidget;
    w.clearHistory = new QPushButton(QStringLiteral("Clear"));
    w.commandEdit = new QLineEdit;
    w.send = new QPushButton(QStringLiteral("Send"));
    w.clearOutput = new QPushButton(QStringLiteral("Clear output"));
    w.catalog = new QGroupBox(QStringLiteral("Commands"));
    return w;
}

void TestRawWorkspace::adoptSyntheticWidgets(RawWorkspace *workspace)
{
    const Widgets w = makeWidgets();
    workspace->adoptWidgets(w.output, w.historyBox, w.history, w.clearHistory,
                            w.commandEdit, w.send, w.clearOutput, w.catalog);
}

void TestRawWorkspace::createsTwoIndependentDocks()
{
    RawWorkspace workspace;
    adoptSyntheticWidgets(&workspace);
    workspace.resize(1000, 700);
    workspace.show();

    QVERIFY(workspace.centralWidget() == nullptr);
    QVERIFY(workspace.terminalDock() != nullptr);
    QVERIFY(workspace.browserDock() != nullptr);
    QCOMPARE(workspace.terminalDock()->objectName(), QStringLiteral("Raw_terminalDock"));
    QCOMPARE(workspace.browserDock()->objectName(), QStringLiteral("Raw_browserDock"));
    QCOMPARE(workspace.dockWidgetArea(workspace.terminalDock()), Qt::LeftDockWidgetArea);
    QCOMPARE(workspace.dockWidgetArea(workspace.browserDock()), Qt::RightDockWidgetArea);

    const QDockWidget::DockWidgetFeatures expected =
        QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
        | QDockWidget::DockWidgetFloatable;
    QCOMPARE(workspace.terminalDock()->features() & expected, expected);
    QCOMPARE(workspace.browserDock()->features() & expected, expected);
    QCOMPARE(workspace.terminalDock()->allowedAreas(), Qt::DockWidgetAreas(Qt::AllDockWidgetAreas));
    QCOMPARE(workspace.browserDock()->allowedAreas(), Qt::DockWidgetAreas(Qt::AllDockWidgetAreas));

    QToolBar *toolBar = workspace.findChild<QToolBar *>(QStringLiteral("Raw_workspaceToolBar"));
    QVERIFY(toolBar != nullptr);
    QVERIFY(toolBar->actions().size() >= 3);
}

void TestRawWorkspace::floatsAndRedocks()
{
    RawWorkspace workspace;
    adoptSyntheticWidgets(&workspace);
    workspace.resize(1000, 700);
    workspace.show();

    workspace.terminalDock()->setFloating(true);
    QVERIFY(workspace.terminalDock()->isFloating());
    QVERIFY(QGuiApplication::screenAt(workspace.terminalDock()->frameGeometry().center())
            != nullptr);
    workspace.browserDock()->setFloating(true);
    QVERIFY(workspace.browserDock()->isFloating());
    QVERIFY(QGuiApplication::screenAt(workspace.browserDock()->frameGeometry().center())
            != nullptr);

    workspace.resetLayout();
    QVERIFY(!workspace.terminalDock()->isFloating());
    QVERIFY(!workspace.browserDock()->isFloating());
    QVERIFY(workspace.terminalDock()->isVisible());
    QVERIFY(workspace.browserDock()->isVisible());
    QCOMPARE(workspace.dockWidgetArea(workspace.terminalDock()), Qt::LeftDockWidgetArea);
    QCOMPARE(workspace.dockWidgetArea(workspace.browserDock()), Qt::RightDockWidgetArea);
}

void TestRawWorkspace::closesAndReopens()
{
    RawWorkspace workspace;
    adoptSyntheticWidgets(&workspace);
    workspace.resize(1000, 700);
    workspace.show();

    QDockWidget *browser = workspace.browserDock();
    QAction *toggle = browser->toggleViewAction();
    QVERIFY(toggle->isCheckable());
    QVERIFY(toggle->isChecked());

    browser->close();
    QVERIFY(!browser->isVisible());
    QVERIFY(!toggle->isChecked());

    toggle->trigger();
    QVERIFY(browser->isVisible());
    QVERIFY(toggle->isChecked());
}

void TestRawWorkspace::resetRestoresDefaultLayout()
{
    RawWorkspace workspace;
    adoptSyntheticWidgets(&workspace);
    workspace.resize(1000, 700);
    workspace.show();

    workspace.addDockWidget(Qt::BottomDockWidgetArea, workspace.terminalDock());
    workspace.tabifyDockWidget(workspace.terminalDock(), workspace.browserDock());
    workspace.resetLayout();

    QCOMPARE(workspace.dockWidgetArea(workspace.terminalDock()), Qt::LeftDockWidgetArea);
    QCOMPARE(workspace.dockWidgetArea(workspace.browserDock()), Qt::RightDockWidgetArea);
    QVERIFY(workspace.terminalDock()->isVisible());
    QVERIFY(workspace.browserDock()->isVisible());
}

void TestRawWorkspace::savesAndRestoresState()
{
    const QString settingsPath =
        QDir::currentPath() + QStringLiteral("/rawworkspace-test-%1.ini")
                                 .arg(QCoreApplication::applicationPid());
    QFile::remove(settingsPath);

    {
        RawWorkspace workspace;
        adoptSyntheticWidgets(&workspace);
        workspace.resize(1000, 700);
        workspace.show();
        workspace.browserDock()->setFloating(true);

        QSettings settings(settingsPath, QSettings::IniFormat);
        workspace.saveWorkspaceState(settings);
        settings.sync();
        QVERIFY(!settings.value(QStringLiteral("RawWorkspace/state")).toByteArray().isEmpty());
    }

    {
        RawWorkspace workspace;
        adoptSyntheticWidgets(&workspace);
        workspace.resize(1000, 700);
        workspace.show();
        QSettings settings(settingsPath, QSettings::IniFormat);
        const QByteArray state = settings.value(QStringLiteral("RawWorkspace/state")).toByteArray();
        QVERIFY(!state.isEmpty());
        QVERIFY(workspace.restoreWorkspaceState(settings));
        QVERIFY(workspace.browserDock()->isFloating());
        QVERIFY(workspace.browserDock()->isVisible());
    }
    QFile::remove(settingsPath);
}

void TestRawWorkspace::compactColumnIsClamped()
{
    QCOMPARE(CommandCatalogDelegate::commandColumnWidth(100), 150);
    QCOMPARE(CommandCatalogDelegate::commandColumnWidth(500), 200);
    QCOMPARE(CommandCatalogDelegate::commandColumnWidth(2000), 340);
}

void TestRawWorkspace::historyHiddenByDefaultWithoutReservedSpace()
{
    const Widgets w = makeWidgets();
    RawWorkspace workspace;
    workspace.adoptWidgets(w.output, w.historyBox, w.history, w.clearHistory,
                           w.commandEdit, w.send, w.clearOutput, w.catalog);
    workspace.resize(1000, 700);
    workspace.show();
    QApplication::processEvents();

    QVERIFY(!workspace.isHistoryVisible());
    QVERIFY(workspace.historyPanel() != nullptr);
    QVERIFY(!workspace.historyPanel()->isVisible());
    QVERIFY(!w.history->isVisible());

    // The output expands over the full terminal panel width: no empty column
    // is reserved for the (disabled) history list.
    QWidget *terminalPanel = workspace.historyPanel()->parentWidget();
    QVERIFY(terminalPanel != nullptr);
    QVERIFY(w.output->width() >= terminalPanel->width() - 12);
}

void TestRawWorkspace::historyToggleShowsAndHidesPanel()
{
    const Widgets w = makeWidgets();
    RawWorkspace workspace;
    workspace.adoptWidgets(w.output, w.historyBox, w.history, w.clearHistory,
                           w.commandEdit, w.send, w.clearOutput, w.catalog);
    workspace.resize(1000, 700);
    workspace.show();
    QApplication::processEvents();

    // Toolbar button and the panel header checkbox stay in sync.
    workspace.historyButton()->setChecked(true);
    QApplication::processEvents();
    QVERIFY(workspace.isHistoryVisible());
    QVERIFY(workspace.historyPanel()->isVisible());
    QVERIFY(w.historyBox->isChecked());

    workspace.setHistoryVisible(false);
    QApplication::processEvents();
    QVERIFY(!workspace.isHistoryVisible());
    QVERIFY(!workspace.historyPanel()->isVisible());
    QVERIFY(!workspace.historyButton()->isChecked());

    w.historyBox->setChecked(true);
    QApplication::processEvents();
    QVERIFY(workspace.isHistoryVisible());
    QVERIFY(workspace.historyPanel()->isVisible());
    QVERIFY(workspace.historyButton()->isChecked());
}

void TestRawWorkspace::historyKeepsEntriesWhileHidden()
{
    const Widgets w = makeWidgets();
    RawWorkspace workspace;
    workspace.adoptWidgets(w.output, w.historyBox, w.history, w.clearHistory,
                           w.commandEdit, w.send, w.clearOutput, w.catalog);
    workspace.resize(1000, 700);
    workspace.show();

    w.history->addItem(QStringLiteral("hf 14a info"));
    w.history->addItem(QStringLiteral("lf search"));
    workspace.setHistoryVisible(false);

    // Storage is independent of visibility.
    QCOMPARE(w.history->count(), 2);
    QCOMPARE(w.history->item(0)->text(), QStringLiteral("hf 14a info"));
    QCOMPARE(w.history->item(1)->text(), QStringLiteral("lf search"));
}

void TestRawWorkspace::historyEntrySignalDoesNotExecute()
{
    const Widgets w = makeWidgets();
    RawWorkspace workspace;
    workspace.adoptWidgets(w.output, w.historyBox, w.history, w.clearHistory,
                           w.commandEdit, w.send, w.clearOutput, w.catalog);
    workspace.resize(1000, 700);
    workspace.show();
    QApplication::processEvents();

    w.history->addItem(QStringLiteral("hw version"));
    QSignalSpy spy(&workspace, &RawWorkspace::historyEntryChosen);
    emit w.history->itemDoubleClicked(w.history->item(0));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("hw version"));

    // Choosing an entry must not populate the input or send anything; the
    // receiver decides how to refill.
    QVERIFY(w.commandEdit->text().isEmpty());

    // A plain refill receiver only sets the text (no execution path).
    QObject::connect(&workspace, &RawWorkspace::historyEntryChosen,
                     w.commandEdit, [&w](const QString &command) {
                         w.commandEdit->setText(command);
                     });
    emit w.history->itemDoubleClicked(w.history->item(0));
    QCOMPARE(w.commandEdit->text(), QStringLiteral("hw version"));
    QCOMPARE(spy.count(), 1);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    QApplication app(argc, argv);
    TestRawWorkspace test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_rawworkspace.moc"
