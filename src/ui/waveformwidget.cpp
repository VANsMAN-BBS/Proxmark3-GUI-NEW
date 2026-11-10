#include "waveformwidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

WaveformWidget::WaveformWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(420, 260);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void WaveformWidget::setSamples(const QVector<int> &samples)
{
    m_samples = samples;
    m_viewStart = 0;
    m_viewCount = m_samples.size();
    update();
}

void WaveformWidget::clearSamples()
{
    m_samples.clear();
    m_viewStart = 0;
    m_viewCount = 0;
    update();
}

void WaveformWidget::fit()
{
    m_viewStart = 0;
    m_viewCount = m_samples.size();
    update();
}

void WaveformWidget::zoomIn()
{
    if(m_samples.isEmpty())
        return;
    const int oldCount = m_viewCount > 0 ? m_viewCount : m_samples.size();
    const int newCount = qMax(32, int(oldCount * 0.8));
    const int center = m_viewStart + oldCount / 2;
    m_viewStart = center - newCount / 2;
    m_viewCount = newCount;
    clampView();
    update();
}

void WaveformWidget::zoomOut()
{
    if(m_samples.isEmpty())
        return;
    const int oldCount = m_viewCount > 0 ? m_viewCount : m_samples.size();
    const int newCount = qMin(m_samples.size(), int(oldCount * 1.25) + 1);
    const int center = m_viewStart + oldCount / 2;
    m_viewStart = center - newCount / 2;
    m_viewCount = newCount;
    clampView();
    update();
}

int WaveformWidget::sampleCount() const
{
    return m_samples.size();
}

int WaveformWidget::visibleStart() const
{
    return m_viewStart;
}

int WaveformWidget::visibleCount() const
{
    return m_viewCount;
}

void WaveformWidget::clampView()
{
    if(m_samples.isEmpty())
    {
        m_viewStart = 0;
        m_viewCount = 0;
        return;
    }
    m_viewCount = qBound(1, m_viewCount, m_samples.size());
    m_viewStart = qBound(0, m_viewStart, m_samples.size() - m_viewCount);
}

int WaveformWidget::xForIndex(int index, const QRect &area) const
{
    if(m_viewCount <= 1)
        return area.left();
    return area.left() + int((qint64)(index - m_viewStart) * area.width() / (m_viewCount - 1));
}

int WaveformWidget::yForSample(int sample, const QRect &area) const
{
    const int clamped = qBound(-127, sample, 127);
    return area.center().y() - int((qint64)clamped * (area.height() / 2 - 4) / 127);
}

void WaveformWidget::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), palette().brush(QPalette::Base));

    const QRect area = rect().adjusted(46, 18, -18, -30);
    if(area.width() < 10 || area.height() < 10)
        return;

    painter.setPen(QPen(palette().color(QPalette::Mid), 1));
    painter.drawRect(area);

    // Horizontal grid at -127, 0, +127 and intermediate steps.
    painter.setPen(QPen(palette().color(QPalette::Midlight), 1, Qt::DotLine));
    for(int value = -100; value <= 100; value += 50)
    {
        const int y = yForSample(value, area);
        painter.drawLine(area.left(), y, area.right(), y);
    }
    const int zeroY = yForSample(0, area);
    painter.setPen(QPen(palette().color(QPalette::Text), 1));
    painter.drawLine(area.left(), zeroY, area.right(), zeroY);

    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(area.left() - 42, area.top() + 10, QStringLiteral("+127"));
    painter.drawText(area.left() - 42, zeroY + 5, QStringLiteral("0"));
    painter.drawText(area.left() - 42, area.bottom(), QStringLiteral("-127"));

    if(m_samples.isEmpty())
    {
        painter.drawText(area, Qt::AlignCenter,
                         tr("No raw samples.\nRun LF Read/Sniff (with LF-capable firmware)\n"
                            "or load a graph buffer with `data load`."));
        return;
    }

    clampView();
    if(m_viewCount <= 1)
    {
        painter.drawText(area, Qt::AlignCenter, tr("Insufficient samples to draw a waveform."));
        return;
    }

    QPainterPath path;
    bool started = false;
    for(int i = m_viewStart; i < m_viewStart + m_viewCount && i < m_samples.size(); i++)
    {
        const QPoint point(xForIndex(i, area), yForSample(m_samples.at(i), area));
        if(!started)
        {
            path.moveTo(point);
            started = true;
        }
        else
        {
            path.lineTo(point);
        }
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(palette().color(QPalette::Highlight), 1));
    painter.drawPath(path);

    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(area.left(), height() - 8,
                     tr("Sample index %1 .. %2  (rate not provided by data save)")
                     .arg(m_viewStart)
                     .arg(m_viewStart + m_viewCount - 1));
}

void WaveformWidget::wheelEvent(QWheelEvent *event)
{
    if(m_samples.isEmpty())
        return;

    const QRect area = rect().adjusted(46, 18, -18, -30);
    const int oldCount = m_viewCount > 0 ? m_viewCount : m_samples.size();
    const int cursorX = qBound(area.left(), int(event->position().toPoint().x()), area.right());
    const double ratio = area.width() > 0 ? double(cursorX - area.left()) / area.width() : 0.5;
    const int cursorSample = m_viewStart + int(ratio * oldCount);

    int newCount = oldCount;
    if(event->angleDelta().y() > 0)
        newCount = qMax(32, int(oldCount * 0.8));
    else
        newCount = qMin(m_samples.size(), int(oldCount * 1.25) + 1);

    m_viewStart = cursorSample - int(ratio * newCount);
    m_viewCount = newCount;
    clampView();
    update();
    event->accept();
}

void WaveformWidget::mousePressEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton)
    {
        m_dragging = true;
        m_lastMouse = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *event)
{
    if(!m_dragging || m_samples.isEmpty())
        return;
    const QRect area = rect().adjusted(46, 18, -18, -30);
    const int dx = event->pos().x() - m_lastMouse.x();
    if(area.width() > 0 && m_viewCount > 0)
        m_viewStart -= int((qint64)dx * m_viewCount / area.width());
    m_lastMouse = event->pos();
    clampView();
    update();
}

void WaveformWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton)
    {
        m_dragging = false;
        unsetCursor();
    }
}

void WaveformWidget::mouseDoubleClickEvent(QMouseEvent *)
{
    fit();
}

