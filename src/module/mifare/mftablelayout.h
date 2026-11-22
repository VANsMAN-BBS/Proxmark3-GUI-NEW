#ifndef MFTABLELAYOUT_H
#define MFTABLELAYOUT_H

#include <QTableWidget>
#include <QTableWidgetItem>
#include <QStringList>

/**
 * Minimal, widget-only helpers shared by the MIFARE page and its UI tests.
 *
 * The MIFARE page keeps its model (data/key lists) and the two table widgets in
 * separate owners.  Changing the card layout therefore has to resize the grid
 * and create the cells *together*, otherwise a later sync dereferences a
 * missing `QTableWidgetItem`.  These helpers make that transition explicit and
 * keep every index access inside the widget's own bounds.
 */
namespace mftable
{

/** Return the item at (row, column), creating it when the cell exists but is empty. */
inline QTableWidgetItem *ensureItem(QTableWidget *widget, int row, int column)
{
    if(!widget || row < 0 || row >= widget->rowCount() ||
            column < 0 || column >= widget->columnCount())
        return nullptr;
    QTableWidgetItem *item = widget->item(row, column);
    if(!item)
    {
        item = new QTableWidgetItem();
        widget->setItem(row, column, item);
    }
    return item;
}

/** Set a cell text; false when (row, column) is outside the current grid. */
inline bool setItemText(QTableWidget *widget, int row, int column, const QString &text)
{
    QTableWidgetItem *item = ensureItem(widget, row, column);
    if(!item)
        return false;
    item->setText(text);
    return true;
}

/** Resize the grid and make sure every cell really has an item. */
inline void resizeGrid(QTableWidget *widget, int rows, int columns)
{
    if(!widget)
        return;
    widget->setRowCount(rows);
    widget->setColumnCount(columns);
    for(int row = 0; row < rows; ++row)
        for(int column = 0; column < columns; ++column)
            ensureItem(widget, row, column);
}

/** Sync one column from `values`; returns the number of rows actually written. */
inline int syncColumnText(QTableWidget *widget, int column, const QStringList &values, int rows)
{
    if(!widget || column < 0 || column >= widget->columnCount())
        return 0;
    const int limit = qMin(rows, qMin(widget->rowCount(), values.size()));
    for(int row = 0; row < limit; ++row)
        setItemText(widget, row, column, values.at(row));
    return limit;
}

} // namespace mftable

#endif // MFTABLELAYOUT_H
