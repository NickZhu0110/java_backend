#pragma once

#include <QJsonObject>
#include <QString>

class JobFileCacheManager
{
public:
    QString baseCacheDir() const;
    QString caseKeyFromInputPath(const QString &inputPath) const;
    QString sanitizeCaseKey(const QString &rawKey) const;
    QString getCaseCacheDir(const QString &caseKey) const;
    bool ensureCaseCacheDir(const QString &caseKey) const;
    QString getJobCacheDir(qint64 jobId) const;
    bool ensureJobCacheDir(qint64 jobId) const;
    QString getJobMetadataPath(qint64 jobId) const;
    QString localAiMaskPath(const QString &caseKey) const;
    QString localAiMaskJobPath(const QString &caseKey, qint64 jobId) const;
    QString localJobAiMaskPath(const QString &caseKey, qint64 jobId) const;
    QString localInputVolumeDir(const QString &caseKey) const;
    QString localInputVolumeZipPath(const QString &caseKey, qint64 jobId) const;
    QString localResultJsonPath(const QString &caseKey) const;
    QString localCorrectedMaskPath(const QString &caseKey, int version) const;
    QString localEditOpsPath(const QString &caseKey, int version) const;
    bool writeJobMetadata(qint64 jobId, const QJsonObject &metadata) const;
    bool writeCaseMetadata(const QString &caseKey, const QJsonObject &metadata) const;

private:
    bool writeJsonFile(const QString &path, const QJsonObject &metadata) const;
};
