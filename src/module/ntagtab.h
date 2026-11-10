#ifndef NTAGTAB_H
#define NTAGTAB_H

#include <QWidget>
#include <QString>

class Util;
class QTableWidget;
class QLineEdit;
class QSpinBox;
class QLabel;

/**
 * NTAG / MIFARE Ultralight page.
 *
 * Commands are limited to the v4.23346 verified set:
 *   hf mfu info
 *   hf mfu rdbl -b <page> [-k <hex>]
 *   hf mfu wrbl -b <page> -d <8 hex> [-k <hex>]
 *
 * Page protection is deliberately conservative:
 *   - pages 0..3 (UID, lock, CC) are read-only
 *   - configuration/OTP pages reported by `cfg0 [n/0x..]` are read-only
 *   - unknown card layouts disable writes instead of guessing
 */
class NtagTab : public QWidget
{
    Q_OBJECT
public:
    explicit NtagTab(Util *util, QWidget *parent = nullptr);
    ~NtagTab();

private slots:
    void detectCard();
    void readAllPages();
    void readSelectedPages();
    void readOnePage();
    void writeSelectedPages();
    void writeOnePage();
    void verifyOnePage();

private:
    Util *util;

    QTableWidget *table;
    QLineEdit *keyEdit;
    QLineEdit *dataEdit;
    QSpinBox *pageSpin;
    QLabel *uidLabel;
    QLabel *typeLabel;
    QLabel *pagesLabel;
    QLabel *statusLabel;

    int m_pages = 0;
    int m_configStart = -1;
    bool m_knownLayout = false;
    QString m_type;

    void buildUi();
    void setStatus(const QString &text, bool error = false);
    bool checkKey(QString *key) const;
    bool isWritable(int page) const;
    void populateTable();
    void setRowData(int page, const QString &data);
    QString rowData(int page) const;
    bool readPageData(int page, QString *data);
    bool writePageData(int page, const QString &data, bool confirm);
    bool parseInfo(const QString &output);
    int pagesForType(const QString &type, int *configStart) const;
    static QString normalizePageData(const QString &text);
    static QString asciiFor(const QString &hex);
};

#endif // NTAGTAB_H
