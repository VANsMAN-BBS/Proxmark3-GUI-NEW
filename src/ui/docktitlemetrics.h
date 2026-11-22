#ifndef DOCKTITLEMETRICS_H
#define DOCKTITLEMETRICS_H

#include <QFontMetrics>
#include <QStyle>
#include <QWidget>

/**
 * Shared metrics for the dock title bar.
 *
 * Qt 5 sizes the title bar as
 *
 *     fontMetrics().height() + 2 * PM_DockWidgetTitleMargin
 *
 * QStyleSheetStyle derives PM_DockWidgetTitleMargin from the top padding of
 * QDockWidget::title and paints the text inside the title rectangle after
 * subtracting the top and bottom padding.  The style sheet therefore uses a
 * slightly asymmetric vertical padding: the 4 px top padding feeds the title
 * height, the 2 px bottom padding leaves two logical pixels of slack for the
 * ascent/descent rounding at 125/150/175/200 % scaling.  text-align: left
 * makes QStyleSheetStyle centre the text vertically in that rectangle.
 *
 * Keep these values in sync with both qdarkstyle QSS files.
 */
namespace DockTitleMetrics
{
enum {
    PaddingTop = 4,
    PaddingBottom = 2,
    PaddingSides = 3,
    MinimumTextSlack = 2
};

inline int titleBarHeight(const QWidget *dock)
{
    if (!dock || !dock->style()) {
        return 0;
    }
    return dock->fontMetrics().height()
            + 2 * dock->style()->pixelMetric(QStyle::PM_DockWidgetTitleMargin, nullptr, dock);
}

/** Height available to the title text after the stylesheet padding. */
inline int textRectHeight(const QWidget *dock)
{
    return titleBarHeight(dock) - (PaddingTop + PaddingBottom);
}

inline bool textFits(const QWidget *dock)
{
    if (!dock) {
        return false;
    }
    const QFontMetrics metrics(dock->font());
    return textRectHeight(dock) >= metrics.ascent() + metrics.descent() + MinimumTextSlack;
}
} // namespace DockTitleMetrics

#endif // DOCKTITLEMETRICS_H
