#include "waveformdialog.h"
#include "waveformwidget.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QStringList>
#include <QVBoxLayout>

WaveformDialog::WaveformDialog(const QVector<int> &samples, const QString &sourceName, QWidget *parent)
    : QDialog(parent), m_samples(samples)
{
    setWindowTitle(tr("LF Raw Sample Waveform - %1").arg(sourceName));
    resize(900, 560);
    buildUi();
}

void WaveformDialog::buildUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    QTabWidget *tabs = new QTabWidget(this);

    QWidget *wavePage = new QWidget(tabs);
    QVBoxLayout *waveLayout = new QVBoxLayout(wavePage);
    waveform = new WaveformWidget(wavePage);
    waveform->setSamples(m_samples);
    waveLayout->addWidget(waveform, 1);

    QHBoxLayout *controls = new QHBoxLayout;
    QPushButton *fitButton = new QPushButton(tr("Fit"), wavePage);
    QPushButton *zoomInButton = new QPushButton(tr("Zoom In"), wavePage);
    QPushButton *zoomOutButton = new QPushButton(tr("Zoom Out"), wavePage);
    controls->addWidget(fitButton);
    controls->addWidget(zoomInButton);
    controls->addWidget(zoomOutButton);
    controls->addStretch();
    waveLayout->addLayout(controls);
    connect(fitButton, &QPushButton::clicked, waveform, &WaveformWidget::fit);
    connect(zoomInButton, &QPushButton::clicked, waveform, &WaveformWidget::zoomIn);
    connect(zoomOutButton, &QPushButton::clicked, waveform, &WaveformWidget::zoomOut);

    tabs->addTab(wavePage, tr("Waveform"));

    QPlainTextEdit *text = new QPlainTextEdit(tabs);
    text->setReadOnly(true);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QStringList values;
    const int maxText = qMin(m_samples.size(), 20000);
    values.reserve(maxText + 2);
    for(int i = 0; i < maxText; i++)
        values.append(QString::number(m_samples.at(i)));
    if(m_samples.size() > maxText)
        values.append(tr("... %1 more samples not shown ...").arg(m_samples.size() - maxText));
    text->setPlainText(values.join(QLatin1Char('\n')));
    textView = text;
    tabs->addTab(text, tr("Sample Text"));

    layout->addWidget(tabs, 1);

    infoLabel = new QLabel(this);
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    updateInfo();
}

void WaveformDialog::updateInfo()
{
    int minValue = 0;
    int maxValue = 0;
    if(!m_samples.isEmpty())
    {
        minValue = m_samples.first();
        maxValue = m_samples.first();
        for(int value : m_samples)
        {
            minValue = qMin(minValue, value);
            maxValue = qMax(maxValue, value);
        }
    }

    infoLabel->setText(tr("Raw sample waveform from the Client GraphBuffer (`data save`). "
                          "Samples: %1, range: %2..%3. The .pm3 text file has no sample-rate "
                          "metadata, so the horizontal axis is sample index, not a fabricated "
                          "time axis. Decoded protocol text remains in RawCommand.")
                       .arg(m_samples.size())
                       .arg(minValue)
                       .arg(maxValue));
}
