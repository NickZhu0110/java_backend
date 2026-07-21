#pragma once

#include <QString>

class PythonSimpleItkPreprocessor
{
public:
    bool preprocessCase(const QString &caseCacheDir,
                        const QString &inputVolumePath,
                        const QString &maskPath,
                        QString *processLog = nullptr,
                        QString *errorMessage = nullptr) const;
    bool preprocessCt(const QString &caseCacheDir,
                      const QString &inputVolumePath,
                      QString *processLog = nullptr,
                      QString *errorMessage = nullptr) const;
    bool preprocessMask(const QString &caseCacheDir,
                        const QString &maskPath,
                        QString *processLog = nullptr,
                        QString *errorMessage = nullptr) const;

private:
    bool runPreprocessor(const QString &caseCacheDir,
                         const QString &mode,
                         const QString &inputVolumePath,
                         const QString &maskPath,
                         QString *processLog,
                         QString *errorMessage) const;
    QString findProjectRoot() const;
    QString scriptPath() const;
};
