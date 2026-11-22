#ifndef EMV_EMVRUNTIME_H
#define EMV_EMVRUNTIME_H

#include <QString>
#include <QStringList>

/**
 * Locates (and, when needed, materialises) the bundled Python reader.
 *
 * The APDU / record parser is intentionally NOT rewritten in C++: the exact
 * pm3_emv_log.py module is shipped as a Qt resource and extracted to a
 * per-user runtime directory before the worker starts Python.
 */
namespace EmvRuntime {

/** Extract the bundled Python sources; returns the directory or "" on error. */
QString scriptDirectory(QString *error = nullptr);

/** Name of the JSON-lines bridge script inside scriptDirectory(). */
QString workerScript(const QString &directory);

/**
 * Find a working Python 3 interpreter.
 *
 * `prefixArgs` receives launcher arguments (for the Windows `py` launcher this
 * is "-3"). PM3GUI_EMV_PYTHON overrides the search and is used as-is.
 */
QString pythonExecutable(QStringList *prefixArgs = nullptr, QString *error = nullptr);

/** Drop the cached interpreter probe (used by tests). */
void clearPythonCache();

} // namespace EmvRuntime

#endif // EMV_EMVRUNTIME_H
