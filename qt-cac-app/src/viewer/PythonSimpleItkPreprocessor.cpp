#include "viewer/PythonSimpleItkPreprocessor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

QString PythonSimpleItkPreprocessor::findProjectRoot() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    while (!dir.exists(QStringLiteral("CMakeLists.txt")) && dir.cdUp()) {
    }
    if (dir.exists(QStringLiteral("CMakeLists.txt"))) {
        return dir.absolutePath();
    }
    return QCoreApplication::applicationDirPath();
}

QString PythonSimpleItkPreprocessor::scriptPath() const
{
    const QString configuredScript =
        qEnvironmentVariable("CAC_CONVERT_SCRIPT_PATH").trimmed();
    if (!configuredScript.isEmpty()) {
        return QDir::cleanPath(configuredScript);
    }
    return QDir(findProjectRoot()).filePath(QStringLiteral("tools/convert_case_to_raw_volume.py"));
}

bool PythonSimpleItkPreprocessor::preprocessCase(const QString &caseCacheDir,
                                                 const QString &inputVolumePath,
                                                 const QString &maskPath,
                                                 QString *processLog,
                                                 QString *errorMessage) const
{
    return runPreprocessor(caseCacheDir, QStringLiteral("both"), inputVolumePath, maskPath, processLog, errorMessage);
}

bool PythonSimpleItkPreprocessor::preprocessCt(const QString &caseCacheDir,
                                               const QString &inputVolumePath,
                                               QString *processLog,
                                               QString *errorMessage) const
{
    return runPreprocessor(caseCacheDir, QStringLiteral("ct"), inputVolumePath, QString(), processLog, errorMessage);
}

bool PythonSimpleItkPreprocessor::preprocessMask(const QString &caseCacheDir,
                                                 const QString &maskPath,
                                                 QString *processLog,
                                                 QString *errorMessage) const
{
    return runPreprocessor(caseCacheDir, QStringLiteral("mask"), QString(), maskPath, processLog, errorMessage);
}

bool PythonSimpleItkPreprocessor::runPreprocessor(const QString &caseCacheDir,
                                                  const QString &mode,
                                                  const QString &inputVolumePath,
                                                  const QString &maskPath,
                                                  QString *processLog,
                                                  QString *errorMessage) const
{
    const QString script = scriptPath();
    if (!QFileInfo::exists(script)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("SimpleITK preprocessing script not found: %1").arg(script);
        }
        return false;
    }

    QStringList arguments;
    arguments << script
              << QStringLiteral("--case-dir") << caseCacheDir
              << QStringLiteral("--output-dir") << caseCacheDir
              << QStringLiteral("--mode") << mode;
    if (!inputVolumePath.isEmpty()) {
        arguments << QStringLiteral("--input-volume") << inputVolumePath;
    }
    if (!maskPath.isEmpty()) {
        arguments << QStringLiteral("--mask") << maskPath;
    }

    QString pythonExecutable =
        qEnvironmentVariable("CAC_PYTHON_EXECUTABLE").trimmed();
    if (pythonExecutable.isEmpty()) {
        const QString localApplicationData =
            qEnvironmentVariable("LOCALAPPDATA").trimmed();
        if (!localApplicationData.isEmpty()) {
            pythonExecutable = QDir(localApplicationData).filePath(
                QStringLiteral("CAC/python310/python.exe"));
        }
    }
    if (pythonExecutable.isEmpty() || !QFileInfo(pythonExecutable).isAbsolute()
        || !QFileInfo::exists(pythonExecutable)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "CAC_PYTHON_EXECUTABLE must reference the controlled absolute "
                "python.exe. Resolved path: %1")
                                .arg(pythonExecutable);
        }
        return false;
    }

    QProcess process;
    process.setProgram(pythonExecutable);
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Failed to start the controlled Python runtime for SimpleITK preprocessing.");
        }
        return false;
    }
    if (!process.waitForFinished(-1)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("SimpleITK preprocessing process did not finish.");
        }
        return false;
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    if (processLog) {
        *processLog = output;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("SimpleITK preprocessing failed with exit code %1. Output: %2")
                .arg(process.exitCode())
                .arg(output.trimmed());
        }
        return false;
    }
    return true;
}
