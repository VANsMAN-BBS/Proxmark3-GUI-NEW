#include "emvruntime.h"

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

const char *const kPythonFiles[] = {
    "pm3_emv_log.py",
    "mock_pm3.py",
    "emv_worker.py",
};

QString bundledFile(const QString &name)
{
    return QStringLiteral(":/emv/") + name;
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
            *error = QCoreApplication::translate("EmvRuntime",
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
    const QString override = qEnvironmentVariable("PM3GUI_EMV_RUNTIME_DIR");
    if (!override.isEmpty())
        roots.append(override);

    const QString local = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!local.isEmpty())
        roots.append(local + QStringLiteral("/emv-runtime"));

    roots.append(QDir::tempPath() + QStringLiteral("/Proxmark3GUI-emv-runtime"));
    roots.append(QCoreApplication::applicationDirPath() + QStringLiteral("/emv-runtime"));
    roots.removeDuplicates();
    return roots;
}

bool probePython(const QString &exe, const QStringList &prefix)
{
    if (exe.isEmpty() || !QFileInfo::exists(exe))
    {
        // A bare name (python/py) is also accepted; QProcess resolves PATH.
        if (exe.isEmpty())
            return false;
        if (exe.contains(QLatin1Char('/')) || exe.contains(QLatin1Char('\\')))
            return false;
    }
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(exe, prefix + QStringList() << QStringLiteral("-c")
                                              << QStringLiteral("print('emv-ok')"));
    if (!process.waitForStarted(4000))
        return false;
    if (!process.waitForFinished(6000))
    {
        process.kill();
        process.waitForFinished(1000);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
        && process.readAllStandardOutput().contains("emv-ok");
}

QStringList pythonCandidates()
{
    QStringList list;
    const QString override = qEnvironmentVariable("PM3GUI_EMV_PYTHON");
    if (!override.isEmpty())
        list.append(override);

    const QString bundled = QCoreApplication::applicationDirPath()
        + QStringLiteral("/emv/python.exe");
    if (QFileInfo::exists(bundled))
        list.append(bundled);

    const QStringList names = QStringList() << QStringLiteral("python3")
                                            << QStringLiteral("python");
    for (const QString &name : names)
    {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty())
            list.append(found);
    }

    // Windows: scan per-user Python installs. This is a search, not a stored
    // machine-specific path.
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

} // namespace

namespace EmvRuntime {

QString extractTo(const QString &root, QString *error)
{
    QDir rootDir(root);
    if (!rootDir.exists() && !rootDir.mkpath(QStringLiteral(".")))
    {
        if (error)
            *error = QCoreApplication::translate("EmvRuntime",
                                                 "Cannot create the EMV runtime directory: %1")
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
                *error = QCoreApplication::translate("EmvRuntime",
                                                     "Cannot write the EMV runtime file: %1 (%2)")
                             .arg(target, writeError);
            return QString();
        }
    }
    return root;
}

QString scriptDirectory(QString *error)
{
    if (error)
        error->clear();

    QString lastError;
    for (const QString &root : runtimeRoots())
    {
        const QString directory = extractTo(root, &lastError);
        if (!directory.isEmpty())
            return directory;
    }

    // Packaged runtime next to the executable (optional deployment pattern).
    const QString packaged = QCoreApplication::applicationDirPath()
        + QStringLiteral("/emv");
    if (hasRuntimeFiles(packaged))
        return packaged;

    if (error)
        *error = lastError.isEmpty()
            ? QCoreApplication::translate("EmvRuntime",
                                          "The bundled EMV reader runtime could not be prepared.")
            : lastError;
    return QString();
}

QString workerScript(const QString &directory)
{
    return directory + QStringLiteral("/emv_worker.py");
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

    const QString override = qEnvironmentVariable("PM3GUI_EMV_PYTHON");
    QStringList candidates = pythonCandidates();
    for (const QString &candidate : candidates)
    {
        const QStringList prefix = prefixFor(candidate);
        // An explicit override is trusted even if the probe is inconclusive
        // (for example a wrapper script that only works with the reader args).
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
        "EmvRuntime",
        "Python 3 was not found. Install Python 3 or set PM3GUI_EMV_PYTHON to python.exe.");
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

} // namespace EmvRuntime
