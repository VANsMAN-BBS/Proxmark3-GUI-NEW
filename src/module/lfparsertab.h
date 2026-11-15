#ifndef LFPARSERTAB_H
#define LFPARSERTAB_H

#include <QWidget>
#include <QString>

#include <lf/lfparsetypes.h>

class QPlainTextEdit;
class QLabel;
class QTableWidget;
class QTabWidget;
class QPushButton;

/**
 * "LF multi-protocol parsing" sub-page.
 *
 * Pure text front-end for the LF parser module (src/lf/parser): it never
 * executes client commands and never depends on hardware.  Input is either
 * pasted/imported client text or the current RawCommand output.
 */
class LfParserTab : public QWidget
{
    Q_OBJECT
public:
    explicit LfParserTab(QWidget *parent = nullptr);

    /** Called by the owner with the RawCommand output text. */
    void setClientText(const QString &text);

signals:
    /** Emitted when the user asks for the current client output. */
    void clientTextRequested();

private slots:
    void parseNow();
    void clearInput();
    void importTextFile();
    void requestClientText();
    void onInputChanged();

private:
    void buildUi();
    void fillResult(const LFParseResult &result);
    void fillT55xx();
    void setSource(const QString &source);
    void clearResult();
    static QString optionalInt(const Lf::Optional<int> &value);
    static QString optionalUInt32(const Lf::Optional<quint32> &value);
    static QString optionalUInt64(const Lf::Optional<quint64> &value);
    static QString extraLabel(const QString &key);

    QPlainTextEdit *input;
    QLabel *sourceLabel;
    QLabel *statusLabel;
    QLabel *protocolLabel;
    QLabel *formatLabel;
    QLabel *bitsLabel;
    QLabel *confidenceLabel;
    QLabel *noteLabel;
    QTableWidget *fieldTable;
    QTabWidget *detailTabs;
    QPlainTextEdit *rawView;
    QPlainTextEdit *diagnosticsView;
    QPlainTextEdit *t55xxView;
    QPushButton *parseButton;
    QPushButton *clearButton;
    QPushButton *importButton;
    QPushButton *clientButton;

    QString source;
};

#endif // LFPARSERTAB_H
