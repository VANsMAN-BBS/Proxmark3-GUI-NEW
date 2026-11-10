#ifndef T55XX_H
#define T55XX_H

#include <QObject>
#include <QVariantMap>

#include "common/util.h"

class QWidget;

/**
 * T55xx page controller.
 *
 * The original page was present in mainwindow.ui but had no implementation.
 * This class restores the local block0/option block editor and wires only the
 * commands/parameters verified against Iceman v4.23346.
 *
 * Hardware note: the currently attached device has LF disabled in firmware,
 * so every device operation is Experimental / Not verified on hardware.
 */
class T55xx : public QObject
{
    Q_OBJECT
public:
    explicit T55xx(Ui::MainWindow *ui, Util *util, QWidget *parent = nullptr);

    void setConfigMap(const QVariantMap &configMap);

public slots:
    void detect();
    void readInfo();
    void applyConfig();

private slots:
    void onBlock0HexChanged();
    void onBlock0BinChanged();
    void onBlock0GetFromData();
    void onBlock0SetToData();

    void onOptionHexChanged();
    void onOptionBinChanged();
    void onOptionGetFromData();
    void onOptionSetToData();

    void onReadEM410x();
    void onCloneEM410x();

private:
    Ui::MainWindow *ui;
    Util *util;
    QWidget *parent;
    QVariantMap configMap;
    bool updatingFields = false;

    void populateCombos();
    void disableUnimplementedControls();

    void updateBlock0Display(quint32 block0);
    quint32 block0FromUi() const;

    static QString normalizedHex(const QString &text, int digits);
    static bool parseHex32(const QString &text, quint32 *value);
    static QString block0ToBinary(quint32 block0);
    static bool binaryToBlock0(const QString &text, quint32 *value);

    void setModulationCombo(quint32 demod);
    quint32 modulationFromCombo() const;
    void setBitrateCombo(quint32 rateIndex);
    quint32 bitrateFromCombo() const;
};

#endif // T55XX_H
