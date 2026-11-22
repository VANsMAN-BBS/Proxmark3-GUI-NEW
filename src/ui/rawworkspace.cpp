#include "rawworkspace.h"

#include <QAction>
#include <QCheckBox>
#include <QDockWidget>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

RawWorkspace::RawWorkspace(QWidget *parent)
    : QMainWindow(parent)
{
    setObjectName(QStringLiteral("RawWorkspace"));
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks
                   | QMainWindow::AllowNestedDocks
                   | QMainWindow::AllowTabbedDocks);
    // No central widget on purpose: the two docks own the complete area.
    setWindowFlags(windowFlags() & ~Qt::Window);
}

RawWorkspace::~RawWorkspace() = default;

void RawWorkspace::adoptWidgets(QPlainTextEdit *output,
                                QCheckBox *historyBox,
                                QListWidget *history,
                                QPushButton *clearHistory,
                                QLineEdit *commandEdit,
                                QPushButton *sendButton,
                                QPushButton *clearOutputButton,
                                QGroupBox *catalogGroupBox)
{
    if (m_adopted) {
        return;
    }
    m_adopted = true;

    QWidget *terminalPanel = buildTerminalPanel(output, historyBox, history,
                                                clearHistory, commandEdit,
                                                sendButton, clearOutputButton);
    QWidget *browserPanel = buildBrowserPanel(catalogGroupBox);
    createDocks(terminalPanel, browserPanel);
    createToolBar();
    resetLayout();
}

QWidget *RawWorkspace::buildTerminalPanel(QPlainTextEdit *output,
                                          QCheckBox *historyBox,
                                          QListWidget *history,
                                          QPushButton *clearHistory,
                                          QLineEdit *commandEdit,
                                          QPushButton *sendButton,
                                          QPushButton *clearOutputButton)
{
    QWidget *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("Raw_terminalPanel"));

    QVBoxLayout *root = new QVBoxLayout(panel);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    root->addWidget(output, 1);

    // History is a collapsible strip above the input line.  It starts hidden
    // so the terminal gets the complete width; showing it never steals width
    // from the output.
    m_historyBox = historyBox;
    m_history = history;
    m_historyPanel = new QWidget(panel);
    m_historyPanel->setObjectName(QStringLiteral("Raw_historyPanel"));
    m_historyPanel->setMaximumHeight(150);
    QHBoxLayout *historyLayout = new QHBoxLayout(m_historyPanel);
    historyLayout->setContentsMargins(0, 0, 0, 0);
    historyLayout->setSpacing(4);
    historyLayout->addWidget(history, 1);
    QVBoxLayout *historyActions = new QVBoxLayout;
    historyActions->setContentsMargins(0, 0, 0, 0);
    historyActions->setSpacing(4);
    historyActions->addWidget(historyBox);
    historyActions->addWidget(clearHistory);
    historyActions->addStretch(1);
    historyLayout->addLayout(historyActions);
    m_historyPanel->setVisible(false);
    root->addWidget(m_historyPanel);

    QHBoxLayout *inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(0, 0, 0, 0);
    inputRow->setSpacing(4);
    inputRow->addWidget(commandEdit, 1);

    // Compact entry point next to the input.  It only expands the stored
    // history; it never executes anything.
    m_historyButton = new QToolButton(panel);
    m_historyButton->setObjectName(QStringLiteral("Raw_CMDHistoryButton"));
    m_historyButton->setText(tr("History"));
    m_historyButton->setToolTip(tr("Show or hide the stored command history"));
    m_historyButton->setCheckable(true);
    m_historyButton->setChecked(false);
    m_historyButton->setArrowType(Qt::DownArrow);
    m_historyButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    inputRow->addWidget(m_historyButton);

    inputRow->addWidget(sendButton);
    inputRow->addWidget(clearOutputButton);
    root->addLayout(inputRow);

    connect(m_historyButton, &QToolButton::toggled, this, &RawWorkspace::setHistoryVisible);
    connect(historyBox, &QCheckBox::toggled, this, &RawWorkspace::setHistoryVisible);
    connect(history, &QListWidget::itemDoubleClicked, this,
            [this](QListWidgetItem *item) {
                if (item) {
                    emit historyEntryChosen(item->text());
                }
            });
    connect(history, &QListWidget::itemActivated, this,
            [this](QListWidgetItem *item) {
                if (item) {
                    emit historyEntryChosen(item->text());
                }
            });

    return panel;
}

void RawWorkspace::setHistoryVisible(bool visible)
{
    if (m_historyVisible == visible) {
        return;
    }
    m_historyVisible = visible;
    if (m_historyPanel) {
        m_historyPanel->setVisible(visible);
        // Drop the layout slot immediately instead of only hiding children.
        m_historyPanel->updateGeometry();
    }
    if (m_historyButton && m_historyButton->isChecked() != visible) {
        const QSignalBlocker blocker(m_historyButton);
        m_historyButton->setChecked(visible);
    }
    if (m_historyBox && m_historyBox->isChecked() != visible) {
        const QSignalBlocker blocker(m_historyBox);
        m_historyBox->setChecked(visible);
    }
}

QWidget *RawWorkspace::buildBrowserPanel(QGroupBox *catalogGroupBox)
{
    QWidget *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("Raw_browserPanel"));

    QVBoxLayout *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(0);

    // The old page limited the catalog to 240 px; inside a dock it must be
    // able to use the full height.
    catalogGroupBox->setMaximumHeight(QWIDGETSIZE_MAX);
    catalogGroupBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(catalogGroupBox, 1);
    return panel;
}

void RawWorkspace::createDocks(QWidget *terminalPanel, QWidget *browserPanel)
{
    m_terminalDock = new QDockWidget(tr("Terminal"), this);
    m_terminalDock->setObjectName(QStringLiteral("Raw_terminalDock"));
    m_terminalDock->setFeatures(QDockWidget::DockWidgetClosable
                                | QDockWidget::DockWidgetMovable
                                | QDockWidget::DockWidgetFloatable);
    m_terminalDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_terminalDock->setMinimumWidth(300);
    m_terminalDock->setWidget(terminalPanel);

    m_browserDock = new QDockWidget(tr("Command Browser"), this);
    m_browserDock->setObjectName(QStringLiteral("Raw_browserDock"));
    m_browserDock->setFeatures(QDockWidget::DockWidgetClosable
                               | QDockWidget::DockWidgetMovable
                               | QDockWidget::DockWidgetFloatable);
    m_browserDock->setAllowedAreas(Qt::AllDockWidgetAreas);
    m_browserDock->setMinimumWidth(320);
    m_browserDock->setWidget(browserPanel);

    addDockWidget(Qt::LeftDockWidgetArea, m_terminalDock);
    addDockWidget(Qt::RightDockWidgetArea, m_browserDock);

    connect(m_terminalDock, &QDockWidget::topLevelChanged, this,
            [this](bool floating) { ensureFloatingVisible(m_terminalDock, floating); });
    connect(m_browserDock, &QDockWidget::topLevelChanged, this,
            [this](bool floating) { ensureFloatingVisible(m_browserDock, floating); });
}

void RawWorkspace::ensureFloatingVisible(QDockWidget *dock, bool floating)
{
    if (!floating || !dock) {
        return;
    }

    // A dock floated from an embedded child QMainWindow can be placed at
    // negative screen coordinates by Qt.  Keep it on the screen that contains
    // this workspace; the user can still drag it anywhere afterwards.
    QScreen *screen = QGuiApplication::screenAt(mapToGlobal(rect().center()));
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const QRect available = screen->availableGeometry();
    const QRect current = dock->frameGeometry();
    if (available.intersects(current)) {
        return;
    }

    const QSize preferred = dock->size().expandedTo(QSize(420, 520));
    QSize targetSize = preferred.boundedTo(available.size());
    QRect target(QPoint(0, 0), targetSize);
    target.moveCenter(mapToGlobal(rect().center()));
    if (target.left() < available.left()) {
        target.moveLeft(available.left());
    }
    if (target.top() < available.top()) {
        target.moveTop(available.top());
    }
    if (target.right() > available.right()) {
        target.moveRight(available.right());
    }
    if (target.bottom() > available.bottom()) {
        target.moveBottom(available.bottom());
    }
    dock->move(target.topLeft());
    dock->resize(targetSize);
}

void RawWorkspace::createToolBar()
{
    QToolBar *toolBar = addToolBar(tr("Workspace"));
    toolBar->setObjectName(QStringLiteral("Raw_workspaceToolBar"));
    toolBar->setMovable(false);
    toolBar->setFloatable(false);
    toolBar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    toolBar->addAction(m_terminalDock->toggleViewAction());
    toolBar->addAction(m_browserDock->toggleViewAction());
    toolBar->addSeparator();

    QAction *reset = toolBar->addAction(tr("Reset Layout"));
    connect(reset, &QAction::triggered, this, &RawWorkspace::resetLayout);
}

void RawWorkspace::resetLayout()
{
    if (!m_terminalDock || !m_browserDock) {
        return;
    }

    m_terminalDock->setFloating(false);
    m_browserDock->setFloating(false);
    m_terminalDock->show();
    m_browserDock->show();
    addDockWidget(Qt::LeftDockWidgetArea, m_terminalDock);
    addDockWidget(Qt::RightDockWidgetArea, m_browserDock);

    const int available = qMax(width(), 900);
    const int terminalWidth = qMax(420, available * 56 / 100);
    const int browserWidth = qMax(360, available - terminalWidth);
    resizeDocks({m_terminalDock, m_browserDock},
                {terminalWidth, browserWidth}, Qt::Horizontal);
}

void RawWorkspace::saveWorkspaceState(QSettings &settings) const
{
    if (!m_terminalDock || !m_browserDock) {
        return;
    }
    settings.beginGroup(QStringLiteral("RawWorkspace"));
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
    settings.setValue(QStringLiteral("state"), saveState());
    settings.endGroup();
}

bool RawWorkspace::restoreWorkspaceState(QSettings &settings)
{
    if (!m_terminalDock || !m_browserDock) {
        return false;
    }

    settings.beginGroup(QStringLiteral("RawWorkspace"));
    const QByteArray geometry = settings.value(QStringLiteral("geometry")).toByteArray();
    const QByteArray state = settings.value(QStringLiteral("state")).toByteArray();
    settings.endGroup();

    bool restored = false;
    if (!geometry.isEmpty()) {
        restored = restoreGeometry(geometry) || restored;
    }
    if (!state.isEmpty()) {
        restored = restoreState(state) || restored;
    }
    if (!restored) {
        resetLayout();
    } else {
        if (m_terminalDock->isFloating()) {
            ensureFloatingVisible(m_terminalDock, true);
        }
        if (m_browserDock->isFloating()) {
            ensureFloatingVisible(m_browserDock, true);
        }
    }
    return restored;
}
