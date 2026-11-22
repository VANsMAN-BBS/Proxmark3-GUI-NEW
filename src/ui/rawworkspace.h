#ifndef RAWWORKSPACE_H
#define RAWWORKSPACE_H

#include <QMainWindow>

class QCheckBox;
class QDockWidget;
class QGroupBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSettings;
class QToolButton;

/**
 * Nested workspace for the existing RawCommand tab.
 *
 * The tab keeps its original widgets and objectNames, but the output/input
 * side and the command browser become two independent docks of a lightweight
 * child QMainWindow.  No new top-level tab or top-level QMainWindow is added;
 * the outer dock/tab layout is untouched.
 */
class RawWorkspace : public QMainWindow
{
    Q_OBJECT
public:
    explicit RawWorkspace(QWidget *parent = nullptr);
    ~RawWorkspace() override;

    /**
     * Move the existing RawCommand widgets into the two workspaces.
     * Must be called once, before setupCommandCatalog() uses the widgets.
     */
    void adoptWidgets(QPlainTextEdit *output,
                      QCheckBox *historyBox,
                      QListWidget *history,
                      QPushButton *clearHistory,
                      QLineEdit *commandEdit,
                      QPushButton *sendButton,
                      QPushButton *clearOutputButton,
                      QGroupBox *catalogGroupBox);

    void saveWorkspaceState(QSettings &settings) const;
    bool restoreWorkspaceState(QSettings &settings);
    void resetLayout();

    QDockWidget *terminalDock() const { return m_terminalDock; }
    QDockWidget *browserDock() const { return m_browserDock; }

    /**
     * Collapsible command-history strip.
     *
     * The panel is hidden by default; when hidden it is removed from the
     * layout (QWidget::hide) so it cannot reserve empty space next to the
     * terminal.  Entries are still recorded while it is hidden.
     */
    void setHistoryVisible(bool visible);
    bool isHistoryVisible() const { return m_historyVisible; }
    QWidget *historyPanel() const { return m_historyPanel; }
    QToolButton *historyButton() const { return m_historyButton; }

signals:
    /** Emitted when a stored command is chosen; the receiver must not execute it. */
    void historyEntryChosen(const QString &command);

private:
    QWidget *buildTerminalPanel(QPlainTextEdit *output,
                                QCheckBox *historyBox,
                                QListWidget *history,
                                QPushButton *clearHistory,
                                QLineEdit *commandEdit,
                                QPushButton *sendButton,
                                QPushButton *clearOutputButton);
    QWidget *buildBrowserPanel(QGroupBox *catalogGroupBox);
    void createDocks(QWidget *terminalPanel, QWidget *browserPanel);
    void createToolBar();
    void ensureFloatingVisible(QDockWidget *dock, bool floating);

    QDockWidget *m_terminalDock = nullptr;
    QDockWidget *m_browserDock = nullptr;
    QWidget *m_historyPanel = nullptr;
    QCheckBox *m_historyBox = nullptr;
    QListWidget *m_history = nullptr;
    QToolButton *m_historyButton = nullptr;
    bool m_historyVisible = false;
    bool m_adopted = false;
};

#endif // RAWWORKSPACE_H
