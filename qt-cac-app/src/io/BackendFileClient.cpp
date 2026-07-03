#include "io/BackendFileClient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
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

QString BackendFileClient::correctedMaskUploadUrl(qint64 jobId) const
{
    return m_baseUrl + QStringLiteral("/api/jobs/%1/files/corrected-mask").arg(jobId);
}

QString BackendFileClient::recalculateScoreUrl(qint64 jobId) const
{
    return m_baseUrl + QStringLiteral("/api/jobs/%1/recalculate").arg(jobId);
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

void BackendFileClient::uploadCorrectedMask(qint64 jobId, const QString &localCorrectedMaskPath, const QString &localMetadataPath)
{
    qInfo() << "Uploading corrected mask"
            << "jobId=" << jobId
            << "mask=" << localCorrectedMaskPath
            << "metadata=" << localMetadataPath
            << "maskBytes=" << QFileInfo(localCorrectedMaskPath).size()
            << "metadataBytes=" << QFileInfo(localMetadataPath).size();

    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    auto *maskFile = new QFile(localCorrectedMaskPath);
    if (!maskFile->open(QIODevice::ReadOnly)) {
        emit correctedMaskUploadFailed(jobId,
                                       QStringLiteral("Cannot open corrected mask: %1").arg(localCorrectedMaskPath),
                                       0);
        delete maskFile;
        delete multiPart;
        return;
    }
    maskFile->setParent(multiPart);
    QHttpPart maskPart;
    maskPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"mask\"; filename=\"%1\"")
                                    .arg(QFileInfo(localCorrectedMaskPath).fileName())));
    maskPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("application/octet-stream")));
    maskPart.setBodyDevice(maskFile);
    multiPart->append(maskPart);

    auto *metadataFile = new QFile(localMetadataPath);
    if (!metadataFile->open(QIODevice::ReadOnly)) {
        emit correctedMaskUploadFailed(jobId,
                                       QStringLiteral("Cannot open corrected mask metadata: %1").arg(localMetadataPath),
                                       0);
        delete metadataFile;
        delete multiPart;
        return;
    }
    metadataFile->setParent(multiPart);
    QHttpPart metadataPart;
    metadataPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                           QVariant(QStringLiteral("form-data; name=\"metadata\"; filename=\"%1\"")
                                        .arg(QFileInfo(localMetadataPath).fileName())));
    metadataPart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("application/json")));
    metadataPart.setBodyDevice(metadataFile);
    multiPart->append(metadataPart);

    QNetworkRequest request(QUrl(correctedMaskUploadUrl(jobId)));
    QNetworkReply *reply = m_networkManager->post(request, multiPart);
    multiPart->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, jobId]() {
        const QByteArray body = reply->readAll();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit correctedMaskUploadFailed(jobId,
                                           QStringLiteral("Corrected mask upload failed: %1; body=%2")
                                               .arg(reply->errorString(), QString::fromUtf8(body)),
                                           httpStatus);
            reply->deleteLater();
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(body);
        emit correctedMaskUploaded(jobId, document.object());
        reply->deleteLater();
    });
}

void BackendFileClient::requestScoreRecalculation(qint64 jobId)
{
    QNetworkRequest request(QUrl(recalculateScoreUrl(jobId)));
    QNetworkReply *reply = m_networkManager->post(request, QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply, jobId]() {
        const QByteArray body = reply->readAll();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            emit scoreRecalculationFailed(jobId,
                                          QStringLiteral("Score recalculation failed: %1; body=%2")
                                              .arg(reply->errorString(), QString::fromUtf8(body)),
                                          httpStatus);
            reply->deleteLater();
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(body);
        emit scoreRecalculated(jobId, document.object());
        reply->deleteLater();
    });
}
