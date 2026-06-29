#include "io/BackendFileClient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

BackendFileClient::BackendFileClient(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    loadSettings();
}

void BackendFileClient::loadSettings()
{
    QSettings settings;
    setBaseUrl(settings.value(QStringLiteral("server/backendUrl"),
                              QStringLiteral("http://localhost:8080")).toString());
}

void BackendFileClient::setBaseUrl(const QString &baseUrl)
{
    m_baseUrl = baseUrl.trimmed();
    while (m_baseUrl.endsWith(QLatin1Char('/'))) {
        m_baseUrl.chop(1);
    }
    if (m_baseUrl.isEmpty()) {
        m_baseUrl = QStringLiteral("http://localhost:8080");
    }
}

QString BackendFileClient::aiMaskDownloadUrl(qint64 jobId) const
{
    return m_baseUrl + QStringLiteral("/api/jobs/%1/files/ai-mask").arg(jobId);
}

QString BackendFileClient::inputVolumeDownloadUrl(qint64 jobId) const
{
    return m_baseUrl + QStringLiteral("/api/jobs/%1/files/input-volume").arg(jobId);
}

void BackendFileClient::downloadAiMask(qint64 jobId, const QString &destinationPath)
{
    QNetworkRequest request(QUrl(aiMaskDownloadUrl(jobId)));
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, jobId, destinationPath]() {
        const QByteArray body = reply->readAll();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit aiMaskDownloadFailed(jobId,
                                      QStringLiteral("AI mask download failed: %1").arg(reply->errorString()),
                                      httpStatus);
            reply->deleteLater();
            return;
        }

        QDir().mkpath(QFileInfo(destinationPath).absolutePath());
        QFile file(destinationPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit aiMaskDownloadFailed(jobId,
                                      QStringLiteral("AI mask download failed: cannot write %1").arg(destinationPath),
                                      httpStatus);
            reply->deleteLater();
            return;
        }

        const qint64 bytesWritten = file.write(body);
        emit aiMaskDownloaded(jobId, destinationPath, bytesWritten);
        reply->deleteLater();
    });
}

void BackendFileClient::downloadInputVolume(qint64 jobId, const QString &destinationPath)
{
    QNetworkRequest request(QUrl(inputVolumeDownloadUrl(jobId)));
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, jobId, destinationPath]() {
        const QByteArray body = reply->readAll();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit inputVolumeDownloadFailed(jobId,
                                           QStringLiteral("Input volume download failed: %1").arg(reply->errorString()),
                                           httpStatus);
            reply->deleteLater();
            return;
        }

        QDir().mkpath(QFileInfo(destinationPath).absolutePath());
        QFile file(destinationPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            emit inputVolumeDownloadFailed(jobId,
                                           QStringLiteral("Input volume download failed: cannot write %1").arg(destinationPath),
                                           httpStatus);
            reply->deleteLater();
            return;
        }

        const qint64 bytesWritten = file.write(body);
        emit inputVolumeDownloaded(jobId, destinationPath, bytesWritten);
        reply->deleteLater();
    });
}

void BackendFileClient::uploadCorrectedMask(qint64 jobId, const QString &localCorrectedMaskPath)
{
    Q_UNUSED(localCorrectedMaskPath);
    // TODO: Implement POST /api/jobs/{jobId}/files/corrected-mask once backend
    // accepts corrected NRRD mask uploads from the doctor workstation.
    emit correctedMaskUploadFailed(jobId,
                                   QStringLiteral("Backend corrected-mask upload endpoint not implemented yet."));
}
