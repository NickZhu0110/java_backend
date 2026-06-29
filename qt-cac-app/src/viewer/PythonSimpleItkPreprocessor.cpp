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
    return QDir(findProjectRoot()).filePath(QStringLiteral("tools/convert_case_to_raw_volume.py"));
}

bool PythonSimpleItkPreprocessor::preprocessCase(const QString &caseCacheDir,
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
              << QStringLiteral("--input-volume") << inputVolumePath
              << QStringLiteral("--mask") << maskPath
              << QStringLiteral("--output-dir") << caseCacheDir;

    QProcess process;
    process.setProgram(QStringLiteral("python3"));
    process.setArguments(arguments);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start();
    if (!process.waitForStarted(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to start python3 for SimpleITK preprocessing.");
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
