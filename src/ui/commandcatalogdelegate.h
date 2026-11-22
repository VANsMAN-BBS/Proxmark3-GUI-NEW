#ifndef COMMANDCATALOGDELEGATE_H
#define COMMANDCATALOGDELEGATE_H

#include <QStyledItemDelegate>

/**
 * Dense single-line row: command in a fixed/adaptive left column and the
 * localized description in the remaining space.  Both columns are elided with
 * the full text available through the model's tooltip role.
 */
class CommandCatalogDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit CommandCatalogDelegate(QObject *parent = nullptr);

    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    /** Clamped 40% command column, suitable for 320..900 px viewports. */
    static int commandColumnWidth(int viewportWidth);
};

#endif // COMMANDCATALOGDELEGATE_H
