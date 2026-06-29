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

private:
    QString findProjectRoot() const;
    QString scriptPath() const;
};
