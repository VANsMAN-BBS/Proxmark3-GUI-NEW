#include "iso14443aruntime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

namespace {

bool gPythonResolved = false;
QString gPythonExe;
QStringList gPythonPrefix;
QString gPythonError;

bool gScriptResolved = false;
QString gScriptDirectory;
QString gScriptError;

const char *const kPythonFiles[] = {
    "iso14443_sidecar.py",
    "parser/__init__.py",
    "parser/parser_state.py",
    "parser/iso14443_parser.py",
    "parser/raw_data_reconstructor.py",
    "parser/atqa_parser.py",
    "parser/sak_parser.py",
    "parser/mifare_classic.py",
    "parser/mifare_ultralight.py",
    "parser/fm1208.py",
    "parser/desfire.py",
    "ui/__init__.py",
    "ui/language.py",
};

QString bundledFile(const QString &name)
{
    return QStringLiteral(":/iso14443a/") + name;
}

bool writeIfChanged(const QString &target, const QByteArray &content, QString *detail = nullptr)
{
    {
        QFile existing(target);
        if (existing.open(QFile::ReadOnly))
        {
            if (existing.readAll() == content)
                return true;
        }
    }
    QDir parent = QFileInfo(target).dir();
    if (!parent.exists() && !parent.mkpath(QStringLiteral(".")))
    {
        if (detail)
            *detail = QCoreApplication::translate("Iso14443aRuntime",
                                                  "Cannot create directory: %1")
                         .arg(parent.absolutePath());
        return false;
    }

    QFile file(target);
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        if (detail)
            *detail = file.errorString();
        return false;
    }
    if (file.write(content) != content.size())
    {
        if (detail)
            *detail = file.errorString();
        file.close();
        return false;
    }
    if (!file.flush())
    {
        if (detail)
            *detail = file.errorString();
        return false;
    }
    return true;
}

QString readResource(const QString &name, QByteArray *content, QString *error)
{
    QFile file(bundledFile(name));
    if (!file.open(QFile::ReadOnly))
    {
        if (error)
            *error = QCoreApplication::translate("Iso14443aRuntime",
                                                 "Bundled Python file is missing: %1")
                         .arg(bundledFile(name));
        return QString();
    }
    *content = file.readAll();
    return name;
}

bool hasRuntimeFiles(const QString &dir)
{
    for (const char *name : kPythonFiles)
    {
        if (!QFileInfo::exists(dir + QLatin1Char('/') + QLatin1String(name)))
            return false;
    }
    return true;
}

QStringList runtimeRoots()
{
    QStringList roots;
    const QString override = qEnvironmentVariable("PM3GUI_ISO14443A_RUNTIME_DIR");
    if (!override.isEmpty())
        roots.append(override);

    const QString local = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!local.isEmpty())
        roots.append(local + QStringLiteral("/iso14443a-runtime"));

    roots.append(QDir::tempPath() + QStringLiteral("/Proxmark3GUI-iso14443a-runtime"));
    roots.append(QCoreApplication::applicationDirPath() + QStringLiteral("/iso14443a-runtime"));
    roots.removeDuplicates();
    return roots;
}

bool probePython(const QString &exe, const QStringList &prefix)
{
    if (exe.isEmpty())
        return false;
    if (!QFileInfo::exists(exe)
            && (exe.contains(QLatin1Char('/')) || exe.contains(QLatin1Char('\\'))))
    {
        return false;
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(exe, prefix + QStringList() << QStringLiteral("-c")
                                              << QStringLiteral("print('iso14443a-ok')"));
    if (!process.waitForStarted(4000))
        return false;
    if (!process.waitForFinished(6000))
    {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
        && process.readAllStandardOutput().contains("iso14443a-ok");
}

QStringList pythonCandidates()
{
    QStringList list;
    const QString override = qEnvironmentVariable("PM3GUI_ISO14443A_PYTHON");
    if (!override.isEmpty())
        list.append(override);

    // Packaged runtimes that already ship with the GUI.
    const QString emrtdBundled = QCoreApplication::applicationDirPath()
        + QStringLiteral("/emrtd/python.exe");
    if (QFileInfo::exists(emrtdBundled))
        list.append(emrtdBundled);
    const QString emvBundled = QCoreApplication::applicationDirPath()
        + QStringLiteral("/emv/python.exe");
    if (QFileInfo::exists(emvBundled))
        list.append(emvBundled);

    const QStringList names = QStringList() << QStringLiteral("python3")
                                            << QStringLiteral("python");
    for (const QString &name : names)
    {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            list.append(found);
    }

    // Windows: scan per-user Python installs (search only, no stored paths).
    const QString localPrograms = qEnvironmentVariable("LOCALAPPDATA")
        + QStringLiteral("/Programs/Python");
    QDir pythonRoot(localPrograms);
    if (pythonRoot.exists())
    {
        const QStringList dirs = pythonRoot.entryList(QStringList() << QStringLiteral("Python3*"),
                                                      QDir::Dirs, QDir::Name | QDir::Reversed);
        for (const QString &dir : dirs)
        {
            const QString exe = pythonRoot.absoluteFilePath(dir + QStringLiteral("/python.exe"));
            if (QFileInfo::exists(exe))
                list.append(exe);
        }
    }

    const QString py = QStandardPaths::findExecutable(QStringLiteral("py"));
    if (!py.isEmpty())
        list.append(py);

    list.removeDuplicates();
    return list;
}

QStringList prefixFor(const QString &exe)
{
    const QString base = QFileInfo(exe).fileName().toLower();
    if (base == QLatin1String("py") || base == QLatin1String("py.exe"))
        return QStringList() << QStringLiteral("-3");
    return QStringList();
}

QString extractTo(const QString &root, QString *error)
{
    QDir rootDir(root);
    if (!rootDir.exists() && !rootDir.mkpath(QStringLiteral(".")))
    {
        if (error)
            *error = QCoreApplication::translate("Iso14443aRuntime",
                                                 "Cannot create the ISO 14443A runtime directory: %1")
                         .arg(root);
        return QString();
    }

    for (const char *name : kPythonFiles)
    {
        QByteArray content;
        QString readError;
        if (readResource(QLatin1String(name), &content, &readError).isEmpty())
        {
            if (error)
                *error = readError;
            return QString();
        }
        const QString target = root + QLatin1Char('/') + QLatin1String(name);
        QString writeError;
        if (!writeIfChanged(target, content, &writeError))
        {
            if (error)
                *error = QCoreApplication::translate("Iso14443aRuntime",
                                                     "Cannot write the ISO 14443A runtime file: %1 (%2)")
                             .arg(target, writeError);
            return QString();
        }
    }
    return root;
}

} // namespace

namespace Iso14443aRuntime {

QString scriptDirectory(QString *error)
{
    if (error)
        error->clear();

    const QString override = qEnvironmentVariable("PM3GUI_ISO14443A_SCRIPT");
    if (!override.isEmpty() && QFileInfo::exists(override))
        return QFileInfo(override).absolutePath();

    if (gScriptResolved)
    {
        if (error)
            *error = gScriptError;
        return gScriptDirectory;
    }

    QString lastError;
    for (const QString &root : runtimeRoots())
    {
        const QString directory = extractTo(root, &lastError);
        if (!directory.isEmpty())
        {
            gScriptResolved = true;
            gScriptDirectory = directory;
            return directory;
        }
    }

    const QString packaged = QCoreApplication::applicationDirPath()
        + QStringLiteral("/iso14443a");
    if (hasRuntimeFiles(packaged))
    {
        gScriptResolved = true;
        gScriptDirectory = packaged;
        return packaged;
    }

    gScriptResolved = true;
    gScriptError = lastError.isEmpty()
        ? QCoreApplication::translate("Iso14443aRuntime",
                                      "The bundled ISO 14443A parser runtime could not be prepared.")
        : lastError;
    if (error)
        *error = gScriptError;
    return QString();
}

QString workerScript(const QString &directory)
{
    const QString override = qEnvironmentVariable("PM3GUI_ISO14443A_SCRIPT");
    if (!override.isEmpty() && QFileInfo::exists(override))
        return QFileInfo(override).absoluteFilePath();
    return directory + QStringLiteral("/iso14443_sidecar.py");
}

QString pythonExecutable(QStringList *prefixArgs, QString *error)
{
    if (prefixArgs)
        prefixArgs->clear();
    if (error)
        error->clear();

    if (gPythonResolved)
    {
        if (prefixArgs)
            *prefixArgs = gPythonPrefix;
        if (error)
            *error = gPythonError;
        return gPythonExe;
    }

    const QString override = qEnvironmentVariable("PM3GUI_ISO14443A_PYTHON");
    const QStringList candidates = pythonCandidates();
    for (const QString &candidate : candidates)
    {
        const QStringList prefix = prefixFor(candidate);
        if (candidate == override || probePython(candidate, prefix))
        {
            gPythonExe = candidate;
            gPythonPrefix = prefix;
            gPythonResolved = true;
            if (prefixArgs)
                *prefixArgs = gPythonPrefix;
            return gPythonExe;
        }
    }

    gPythonError = QCoreApplication::translate(
        "Iso14443aRuntime",
        "Python 3 was not found. Install Python 3 or set PM3GUI_ISO14443A_PYTHON to python.exe.");
    gPythonResolved = true;
    if (error)
        *error = gPythonError;
    return QString();
}

void clearPythonCache()
{
    gPythonResolved = false;
    gPythonExe.clear();
    gPythonPrefix.clear();
    gPythonError.clear();
}

void clearScriptCache()
{
    gScriptResolved = false;
    gScriptDirectory.clear();
    gScriptError.clear();
}

} // namespace Iso14443aRuntime
