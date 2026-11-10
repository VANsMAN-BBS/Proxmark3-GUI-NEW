#ifndef WAVEFORMDIALOG_H
#define WAVEFORMDIALOG_H

#include <QDialog>
#include <QVector>
#include <QString>

class WaveformWidget;
class QLabel;
class QPlainTextEdit;

/**
 * Optional LF waveform display.
 *
 * Data is the Client GraphBuffer exported by `data save`. The dialog keeps a
 * text view of the same raw sample values; decoded protocol text remains in
 * the RawCommand output and is never treated as waveform data.
 */
class WaveformDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WaveformDialog(const QVector<int> &samples, const QString &sourceName,
                            QWidget *parent = nullptr);

private:
    WaveformWidget *waveform;
    QPlainTextEdit *textView = nullptr;
    QLabel *infoLabel;
    QVector<int> m_samples;

    void buildUi();
    void updateInfo();
};

#endif // WAVEFORMDIALOG_H
