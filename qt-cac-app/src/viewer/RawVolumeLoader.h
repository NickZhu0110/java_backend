#pragma once

#include <QString>

#include "viewer/LoadedCaseVolume.h"

class RawVolumeLoader
{
public:
    static bool hasPreprocessedFiles(const QString &caseCacheDir);
    static LoadedCaseVolume loadCase(const QString &caseCacheDir, QString *errorMessage = nullptr);

private:
    static bool loadVolume(const QString &inputVolumeDir, VolumeData *volume, QString *errorMessage);
    static bool loadMask(const QString &caseCacheDir, MaskVolume *mask, QString *errorMessage);
};
