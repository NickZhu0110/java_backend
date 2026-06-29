#include "viewer/CaseVolumeLoader.h"

#include "viewer/PythonSimpleItkPreprocessor.h"
#include "viewer/RawVolumeLoader.h"

#include <QDir>
#include <QFileInfo>

LoadedCaseVolume CaseVolumeLoader::loadCaseFromCache(const QString &caseCacheDir,
                                                     QString *processLog,
                                                     QString *errorMessage) const
{
    if (RawVolumeLoader::hasPreprocessedFiles(caseCacheDir)) {
        if (processLog) {
            *processLog = QStringLiteral("Preprocessed raw case files already exist; loading directly.");
        }
        return RawVolumeLoader::loadCase(caseCacheDir, errorMessage);
    }

    const QString inputVolumePath = findInputVolumeArtifact(caseCacheDir);
    const QString maskPath = findMaskArtifact(caseCacheDir);
    if (inputVolumePath.isEmpty() || maskPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Case cache is missing input volume or ai_mask_v0.nrrd.");
        }
        return {};
    }

    QString preprocessorLog;
    PythonSimpleItkPreprocessor preprocessor;
    if (!preprocessor.preprocessCase(caseCacheDir, inputVolumePath, maskPath, &preprocessorLog, errorMessage)) {
        if (processLog) {
            *processLog = preprocessorLog;
        }
        return {};
    }

    QString loadError;
    LoadedCaseVolume loaded = RawVolumeLoader::loadCase(caseCacheDir, &loadError);
    if (processLog) {
        *processLog = preprocessorLog;
    }
    if (!loaded.volume.isValid() && errorMessage) {
        *errorMessage = loadError;
    }
    return loaded;
}

QString CaseVolumeLoader::findInputVolumeArtifact(const QString &caseCacheDir) const
{
    const QDir inputDir(QDir(caseCacheDir).filePath(QStringLiteral("input_volume")));
    const QFileInfo dicomSeries(inputDir.filePath(QStringLiteral("dicom_series")));
    if (dicomSeries.exists() && dicomSeries.isDir()) {
        return dicomSeries.absoluteFilePath();
    }

    const QFileInfoList zipFiles = inputDir.entryInfoList({QStringLiteral("input_volume_job_*.zip")},
                                                          QDir::Files,
                                                          QDir::Time);
    if (!zipFiles.isEmpty()) {
        return zipFiles.first().absoluteFilePath();
    }

    const QFileInfoList medicalFiles = inputDir.entryInfoList({QStringLiteral("*.nrrd"),
                                                               QStringLiteral("*.nii"),
                                                               QStringLiteral("*.nii.gz"),
                                                               QStringLiteral("*.mhd")},
                                                              QDir::Files,
                                                              QDir::Time);
    if (!medicalFiles.isEmpty()) {
        return medicalFiles.first().absoluteFilePath();
    }

    return {};
}

QString CaseVolumeLoader::findMaskArtifact(const QString &caseCacheDir) const
{
    const QFileInfo canonicalMask(QDir(caseCacheDir).filePath(QStringLiteral("ai_mask_v0.nrrd")));
    if (canonicalMask.exists() && canonicalMask.isFile()) {
        return canonicalMask.absoluteFilePath();
    }

    const QDir maskDir(QDir(caseCacheDir).filePath(QStringLiteral("ai_masks")));
    const QFileInfoList maskFiles = maskDir.entryInfoList({QStringLiteral("*.nrrd")},
                                                          QDir::Files,
                                                          QDir::Time);
    if (!maskFiles.isEmpty()) {
        return maskFiles.first().absoluteFilePath();
    }
    return {};
}
