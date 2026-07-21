#pragma once

#include <QString>

#include "viewer/LoadedCaseVolume.h"

class CaseVolumeLoader
{
public:
    LoadedCaseVolume loadCaseFromCache(const QString &caseCacheDir,
                                       QString *processLog = nullptr,
                                       QString *errorMessage = nullptr) const;

private:
    QString findInputVolumeArtifact(const QString &caseCacheDir) const;
    QString findMaskArtifact(const QString &caseCacheDir) const;
};
