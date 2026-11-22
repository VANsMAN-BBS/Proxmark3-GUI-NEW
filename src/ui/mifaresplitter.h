#ifndef MIFARESPLITTER_H
#define MIFARESPLITTER_H

#include <QList>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSplitter>
#include <QtMath>

/**
 * Horizontal splitter with a stable 35 / 65 initial allocation.
 *
 * QSplitter derives its first layout from the children's size hints, which
 * would otherwise let the wide status terminal and the card tables decide the
 * ratio.  This subclass applies 35 / 65 on first show and keeps it while the
 * window is resized.  Once the user drags the handle, the user's choice is
 * respected and the automatic ratio stops.
 */
class MifareSplitter : public QSplitter
{
public:
    explicit MifareSplitter(QWidget *parent = nullptr)
        : QSplitter(parent)
        , userMoved(false)
    {
        setChildrenCollapsible(false);
        setHandleWidth(6);
        connect(this, &QSplitter::splitterMoved, this, [this](int, int) {
            userMoved = true;
        });
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QSplitter::showEvent(event);
        // Only the first show establishes the 35 / 65 default.  A later tab
        // switch or window restore must not undo a ratio the user dragged.
        if (!userMoved)
            applyRatio();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QSplitter::resizeEvent(event);
        if (!userMoved)
            applyRatio();
    }

private:
    void applyRatio()
    {
        const int total = width() - handleWidth();
        if (total <= 1)
            return;
        const int left = qRound(double(total) * 0.35);
        QList<int> sizes;
        sizes << left << (total - left);
        setSizes(sizes);
    }

    bool userMoved;
};

#endif // MIFARESPLITTER_H
