#ifndef SETTINGSLAYOUT_H
#define SETTINGSLAYOUT_H

class QWidget;

/**
 * Tighten the Settings page without changing any setting.
 *
 * The page keeps every widget, objectName, default and signal.  This helper
 * only applies layout properties: a scrollable, expanding content column, one
 * label column for the inline rows, control heights derived from the actual
 * font metrics, compact numeric fields, expanding path fields, aligned
 * checkbox rows, uniform group spacing and a secondary colour for the long
 * explanatory labels.  It is idempotent and may be called again after the
 * global font changes.
 */
void refineSettingsLayout(QWidget *settingsPage);

#endif // SETTINGSLAYOUT_H
