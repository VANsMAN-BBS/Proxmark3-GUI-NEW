#ifndef ISO14443A_RUNTIME_H
#define ISO14443A_RUNTIME_H

#include <QString>
#include <QStringList>

/**
 * Locates (and, when needed, materialises) the bundled ISO 14443A parser.
 *
 * The Stage-1 parser is intentionally not rewritten in C++.  Its Python
 * sources are embedded as Qt resources and extracted to a per-user runtime
 * directory before the sidecar starts.  All paths are passed as QProcess
 * argument-list entries, so spaces and CJK directory names are safe.
 */
namespace Iso14443aRuntime {

/** Extract the bundled Python sources; returns the directory or "" on error. */
QString scriptDirectory(QString *error = nullptr);

/** Absolute path of the JSON-lines sidecar script. */
QString workerScript(const QString &directory);

/**
 * Find a working Python 3 interpreter.
 *
 * `prefixArgs` receives launcher arguments (for the Windows `py` launcher this
 * is "-3").  PM3GUI_ISO14443A_PYTHON overrides the search and is used as-is.
 */
QString pythonExecutable(QStringList *prefixArgs = nullptr, QString *error = nullptr);

/** Drop the cached interpreter probe (used by tests). */
void clearPythonCache();

/** Reset the cached script directory (used by tests). */
void clearScriptCache();

} // namespace Iso14443aRuntime

#endif // ISO14443A_RUNTIME_H
