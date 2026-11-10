#ifndef WAVEFORMWIDGET_H
#define WAVEFORMWIDGET_H

#include <QWidget>
#include <QVector>
#include <QPoint>

/**
 * Raw sample waveform viewer.
 *
 * The data is exactly the Client GraphBuffer sample values exported by
 * `data save` (one integer per line, -127..127). No waveform is generated
 * from card IDs, decoded text or protocol annotations.
 *
 * The exported file contains sample values only, not a sample rate. The view
 * therefore labels the horizontal axis as sample index; it does not fabricate
 * a time axis.
 */
class WaveformWidget : public QWidget
{
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget *parent = nullptr);

    void setSamples(const QVector<int> &samples);
    void clearSamples();
    void fit();
    void zoomIn();
    void zoomOut();

    int sampleCount() const;
    int visibleStart() const;
    int visibleCount() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QVector<int> m_samples;
    int m_viewStart = 0;
    int m_viewCount = 0;
    QPoint m_lastMouse;
    bool m_dragging = false;

    void clampView();
    int xForIndex(int index, const QRect &area) const;
    int yForSample(int sample, const QRect &area) const;
};

#endif // WAVEFORMWIDGET_H
