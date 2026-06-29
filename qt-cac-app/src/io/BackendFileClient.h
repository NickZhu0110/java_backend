#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class BackendFileClient : public QObject
{
    Q_OBJECT

public:
    explicit BackendFileClient(QObject *parent = nullptr);

    void loadSettings();
    void setBaseUrl(const QString &baseUrl);
    QString aiMaskDownloadUrl(qint64 jobId) const;
    QString inputVolumeDownloadUrl(qint64 jobId) const;
    void downloadAiMask(qint64 jobId, const QString &destinationPath);
    void downloadInputVolume(qint64 jobId, const QString &destinationPath);
    void uploadCorrectedMask(qint64 jobId, const QString &localCorrectedMaskPath);

signals:
    void aiMaskDownloaded(qint64 jobId, const QString &destinationPath, qint64 bytesWritten);
    void aiMaskDownloadFailed(qint64 jobId, const QString &message, int httpStatus);
    void inputVolumeDownloaded(qint64 jobId, const QString &destinationPath, qint64 bytesWritten);
    void inputVolumeDownloadFailed(qint64 jobId, const QString &message, int httpStatus);
    void correctedMaskUploadFailed(qint64 jobId, const QString &message);

private:
    QNetworkAccessManager *m_networkManager;
    QString m_baseUrl;
};
