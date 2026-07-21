#include "viewer/CaseVolumeLoader.h"

#include "viewer/PythonSimpleItkPreprocessor.h"
#include "viewer/RawVolumeLoader.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDateTime>

namespace {

constexpr int kPreprocessSchemaVersion = 1;

QJsonObject readJsonObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.isObject() ? document.object() : QJsonObject();
}

QDateTime latestModifiedInDirectory(const QString &dirPath)
{
    QDateTime latest;
    QDirIterator it(dirPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info(it.next());
        if (!latest.isValid() || info.lastModified() > latest) {
            latest = info.lastModified();
        }
    }
    return latest;
}

QDateTime sourceModifiedTime(const QString &path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        return {};
    }
    if (info.isDir()) {
        return latestModifiedInDirectory(info.absoluteFilePath());
    }
    return info.lastModified();
}

bool metadataSchemaMatches(const QString &metadataPath)
{
    const QJsonObject metadata = readJsonObject(metadataPath);
    return metadata.value(QStringLiteral("preprocess_schema_version")).toInt(-1) == kPreprocessSchemaVersion;
}

bool generatedCacheIsStale(const QString &sourcePath,
                           const QString &rawPath,
                           const QString &metadataPath,
                           const QString &missingLog,
                           const QString &newerLog,
                           const QString &schemaLog,
                           QStringList *logs)
{
    const QFileInfo rawInfo(rawPath);
    const QFileInfo metadataInfo(metadataPath);
    if (!rawInfo.exists() || !metadataInfo.exists()) {
        *logs << missingLog;
        return true;
    }

    if (!metadataSchemaMatches(metadataPath)) {
        *logs << schemaLog;
        return true;
    }

    const QDateTime sourceModified = sourceModifiedTime(sourcePath);
    if (sourceModified.isValid()
        && (sourceModified > rawInfo.lastModified() || sourceModified > metadataInfo.lastModified())) {
        *logs << newerLog;
        return true;
    }

    return false;
}

} // namespace

LoadedCaseVolume CaseVolumeLoader::loadCaseFromCache(const QString &caseCacheDir,
                                                     QString *processLog,
                                                     QString *errorMessage) const
{
    QStringList logs;
    const QString inputVolumePath = findInputVolumeArtifact(caseCacheDir);
    const QString maskPath = findMaskArtifact(caseCacheDir);
    if (inputVolumePath.isEmpty() || maskPath.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Case cache is missing input volume or ai_mask_v0.nrrd.");
        }
        return {};
    }

    const QDir caseDir(caseCacheDir);
    const QDir inputDir(caseDir.filePath(QStringLiteral("input_volume")));
    const QString ctRawPath = inputDir.filePath(QStringLiteral("ct_volume_int16.raw"));
    const QString ctMetadataPath = inputDir.filePath(QStringLiteral("ct_volume_metadata.json"));
    const QString maskRawPath = caseDir.filePath(QStringLiteral("mask_volume_uint8.raw"));
    const QString maskMetadataPath = caseDir.filePath(QStringLiteral("mask_volume_metadata.json"));

    const bool ctStale = generatedCacheIsStale(inputVolumePath,
                                               ctRawPath,
                                               ctMetadataPath,
                                               QStringLiteral("Raw CT cache missing; preprocessing CT"),
                                               QStringLiteral("Input volume source is newer than ct_volume_int16.raw; regenerating CT"),
                                               QStringLiteral("Preprocess schema version mismatch for CT; regenerating"),
                                               &logs);
    const bool maskStale = generatedCacheIsStale(maskPath,
                                                 maskRawPath,
                                                 maskMetadataPath,
                                                 QStringLiteral("Raw mask cache missing; preprocessing mask"),
                                                 QStringLiteral("Source ai_mask_v0.nrrd is newer than mask_volume_uint8.raw; regenerating mask"),
                                                 QStringLiteral("Preprocess schema version mismatch for mask; regenerating"),
                                                 &logs);
    if (ctStale && !maskStale) {
        logs << QStringLiteral("Mask cache fresh, CT cache stale");
    } else if (!ctStale && maskStale) {
        logs << QStringLiteral("CT cache fresh, mask cache stale");
    } else if (!ctStale && !maskStale) {
        logs << QStringLiteral("Raw cache is fresh; loading directly");
    }

    QString preprocessorLog;
    PythonSimpleItkPreprocessor preprocessor;
    if (ctStale && maskStale) {
        if (!preprocessor.preprocessCase(caseCacheDir, inputVolumePath, maskPath, &preprocessorLog, errorMessage)) {
            if (processLog) {
                logs << preprocessorLog;
                *processLog = logs.join(QLatin1Char('\n'));
            }
            return {};
        }
    } else if (ctStale) {
        if (!preprocessor.preprocessCt(caseCacheDir, inputVolumePath, &preprocessorLog, errorMessage)) {
            if (processLog) {
                logs << preprocessorLog;
                *processLog = logs.join(QLatin1Char('\n'));
            }
            return {};
        }
    } else if (maskStale) {
        if (!preprocessor.preprocessMask(caseCacheDir, maskPath, &preprocessorLog, errorMessage)) {
            if (processLog) {
                logs << preprocessorLog;
                *processLog = logs.join(QLatin1Char('\n'));
            }
            return {};
        }
    }
    if (!preprocessorLog.trimmed().isEmpty()) {
        logs << preprocessorLog.trimmed();
    }

    QString loadError;
    LoadedCaseVolume loaded = RawVolumeLoader::loadCase(caseCacheDir, &loadError);
    if (processLog) {
        *processLog = logs.join(QLatin1Char('\n'));
    }
    if (!loaded.volume.isValid() && errorMessage) {
        *errorMessage = loadError;
    }
    return loaded;
}

QString CaseVolumeLoader::findInputVolumeArtifact(const QString &caseCacheDir) const
{
    const QDir inputDir(QDir(caseCacheDir).filePath(QStringLiteral("input_volume")));
    const QFileInfoList zipFiles = inputDir.entryInfoList({QStringLiteral("input_volume_job_*.zip")},
                                                          QDir::Files,
                                                          QDir::Time);
    if (!zipFiles.isEmpty()) {
        return zipFiles.first().absoluteFilePath();
    }

    const QFileInfo dicomSeries(inputDir.filePath(QStringLiteral("dicom_series")));
    if (dicomSeries.exists() && dicomSeries.isDir()) {
        return dicomSeries.absoluteFilePath();
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
