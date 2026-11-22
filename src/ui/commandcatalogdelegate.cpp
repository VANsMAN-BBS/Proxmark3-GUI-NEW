#include "commandcatalogdelegate.h"
#include "commandcatalogmodel.h"

#include <QApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QStyle>

CommandCatalogDelegate::CommandCatalogDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

int CommandCatalogDelegate::commandColumnWidth(int viewportWidth)
{
    // The command is the stable identifier and should stay readable; the
    // description gets the rest.  Clamping avoids a 20 px or 700 px column on
    // very narrow/wide docks.
    return qBound(150, viewportWidth * 40 / 100, 340);
}

QSize CommandCatalogDelegate::sizeHint(const QStyleOptionViewItem &option,
                                       const QModelIndex &index) const
{
    Q_UNUSED(index);
    const QFontMetrics metrics(option.font);
    return QSize(240, metrics.height() + 4);
}

void CommandCatalogDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear(); // text is painted manually so the columns can be elided

    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const QString command = index.data(CommandCatalogModel::CommandRole).toString();
    const QString description = index.data(CommandCatalogModel::DescriptionRole).toString();
    if (command.isEmpty() && description.isEmpty()) {
        return;
    }

    const QFontMetrics metrics(opt.font);
    const QRect content = opt.rect.adjusted(4, 1, -4, -1);
    if (content.width() <= 0 || content.height() <= 0) {
        return;
    }

    const int commandWidth = qMin(commandColumnWidth(opt.rect.width()), content.width());
    const QRect commandRect(content.left(), content.top(),
                            commandWidth, content.height());
    const int gap = 8;
    const QRect descriptionRect(commandRect.right() + gap, content.top(),
                                qMax(0, content.right() - commandRect.right() - gap),
                                content.height());

    const bool selected = opt.state & QStyle::State_Selected;
    const QColor commandColor = selected ? opt.palette.color(QPalette::HighlightedText)
                                         : opt.palette.color(QPalette::Text);
    const QColor descriptionColor = selected ? opt.palette.color(QPalette::HighlightedText)
                                             : opt.palette.color(QPalette::Disabled, QPalette::Text);

    painter->save();
    painter->setFont(opt.font);
    painter->setPen(commandColor);
    painter->drawText(commandRect, Qt::AlignLeft | Qt::AlignVCenter,
                      metrics.elidedText(command, Qt::ElideRight, commandRect.width()));

    if (!description.isEmpty() && descriptionRect.width() > 0) {
        painter->setPen(descriptionColor);
        painter->drawText(descriptionRect, Qt::AlignLeft | Qt::AlignVCenter,
                          metrics.elidedText(description, Qt::ElideRight,
                                             descriptionRect.width()));
    }
    painter->restore();
}
