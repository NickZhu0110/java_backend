#include "cache/JobFileCacheManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>

QString JobFileCacheManager::baseCacheDir() const
{
    const QString configuredDataRoot = qEnvironmentVariable("CAC_DATA_ROOT").trimmed();
    if (!configuredDataRoot.isEmpty()) {
        return QDir(configuredDataRoot).filePath(QStringLiteral("qt-cache"));
    }

    QDir dir(QCoreApplication::applicationDirPath());
    while (!dir.exists(QStringLiteral("CMakeLists.txt")) && dir.cdUp()) {
    }

    if (!dir.exists(QStringLiteral("CMakeLists.txt"))) {
        dir = QDir(QCoreApplication::applicationDirPath());
    }

    return dir.filePath(QStringLiteral("cac_platform/cache"));
}

QString JobFileCacheManager::caseKeyFromInputPath(const QString &inputPath) const
{
    QFileInfo info(inputPath);
    QString rawKey = info.fileName();
    if (rawKey.isEmpty()) {
        rawKey = info.dir().dirName();
    }
    if (rawKey.isEmpty()) {
        rawKey = QStringLiteral("unknown_case");
    }

    // TODO: Strengthen this key with DICOM PatientID, StudyInstanceUID,
    // SeriesInstanceUID, and input folder/file checksums before production use.
    return sanitizeCaseKey(rawKey);
}

QString JobFileCacheManager::sanitizeCaseKey(const QString &rawKey) const
{
    QString key = rawKey.trimmed();
    key.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("_"));
    key.replace(QRegularExpression(QStringLiteral("_+")), QStringLiteral("_"));
    key = key.left(120);
    while (key.startsWith(QLatin1Char('.')) || key.startsWith(QLatin1Char('_'))) {
        key.remove(0, 1);
    }
    while (key.endsWith(QLatin1Char('.')) || key.endsWith(QLatin1Char('_'))) {
        key.chop(1);
    }
    return key.isEmpty() ? QStringLiteral("unknown_case") : key;
}

QString JobFileCacheManager::getCaseCacheDir(const QString &caseKey) const
{
    return QDir(baseCacheDir()).filePath(QStringLiteral("cases/%1").arg(sanitizeCaseKey(caseKey)));
}

bool JobFileCacheManager::ensureCaseCacheDir(const QString &caseKey) const
{
    const QString caseDir = getCaseCacheDir(caseKey);
    QDir dir;
    return dir.mkpath(QDir(caseDir).filePath(QStringLiteral("input_volume")))
        && dir.mkpath(QDir(caseDir).filePath(QStringLiteral("ai_masks")))
        && dir.mkpath(QDir(caseDir).filePath(QStringLiteral("corrected_masks")))
        && dir.mkpath(QDir(caseDir).filePath(QStringLiteral("edit_ops")));
}

QString JobFileCacheManager::getJobCacheDir(qint64 jobId) const
{
    return QDir(baseCacheDir()).filePath(QStringLiteral("jobs/%1").arg(jobId));
}

bool JobFileCacheManager::ensureJobCacheDir(qint64 jobId) const
{
    QDir dir;
    return dir.mkpath(getJobCacheDir(jobId));
}

QString JobFileCacheManager::getJobMetadataPath(qint64 jobId) const
{
    return QDir(getJobCacheDir(jobId)).filePath(QStringLiteral("job_metadata.json"));
}

QString JobFileCacheManager::localAiMaskPath(const QString &caseKey) const
{
    return QDir(getCaseCacheDir(caseKey)).filePath(QStringLiteral("ai_mask_v0.nrrd"));
}

QString JobFileCacheManager::localAiMaskJobPath(const QString &caseKey, qint64 jobId) const
{
    return QDir(getCaseCacheDir(caseKey)).filePath(QStringLiteral("ai_masks/ai_mask_job_%1.nrrd").arg(jobId));
}

QString JobFileCacheManager::localJobAiMaskPath(const QString &caseKey, qint64 jobId) const
{
    return localAiMaskJobPath(caseKey, jobId);
}

QString JobFileCacheManager::localInputVolumeDir(const QString &caseKey) const
{
    return QDir(getCaseCacheDir(caseKey)).filePath(QStringLiteral("input_volume"));
}

QString JobFileCacheManager::localInputVolumeZipPath(const QString &caseKey, qint64 jobId) const
{
    return QDir(localInputVolumeDir(caseKey)).filePath(QStringLiteral("input_volume_job_%1.zip").arg(jobId));
}

QString JobFileCacheManager::localInputVolumeNrrdPath(const QString &caseKey, qint64 jobId) const
{
    return QDir(localInputVolumeDir(caseKey)).filePath(
        QStringLiteral("input_volume_job_%1.nrrd").arg(jobId));
}

QString JobFileCacheManager::localResultJsonPath(const QString &caseKey) const
{
    return QDir(getCaseCacheDir(caseKey)).filePath(QStringLiteral("result.json"));
}

QString JobFileCacheManager::localCorrectedMaskPath(const QString &caseKey, int version) const
{
    return QDir(getCaseCacheDir(caseKey)).filePath(QStringLiteral("corrected_masks/corrected_mask_v%1.nrrd").arg(version));
}

QString JobFileCacheManager::localEditOpsPath(const QString &caseKey, int version) const
{
    return QDir(getCaseCacheDir(caseKey)).filePath(QStringLiteral("edit_ops/edit_ops_v%1.json").arg(version));
}

bool JobFileCacheManager::writeJobMetadata(qint64 jobId, const QJsonObject &metadata) const
{
    ensureJobCacheDir(jobId);
    return writeJsonFile(getJobMetadataPath(jobId), metadata);
}

bool JobFileCacheManager::writeCaseMetadata(const QString &caseKey, const QJsonObject &metadata) const
{
    ensureCaseCacheDir(caseKey);
    return writeJsonFile(QDir(getCaseCacheDir(caseKey)).filePath(QStringLiteral("metadata.json")), metadata);
}

bool JobFileCacheManager::writeJsonFile(const QString &path, const QJsonObject &metadata) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(metadata).toJson(QJsonDocument::Indented));
    return true;
}
