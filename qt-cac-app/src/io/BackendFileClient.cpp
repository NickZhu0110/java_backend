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
#include <QSharedPointer>
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
    QString configuredUrl = qEnvironmentVariable("CAC_BACKEND_URL").trimmed();
    if (configuredUrl.isEmpty()) {
        configuredUrl = settings.value(QStringLiteral("server/backendUrl"),
                                       QStringLiteral("http://127.0.0.1:6006")).toString();
    }
    if (configuredUrl == QStringLiteral("http://localhost:8080")) {
        configuredUrl = QStringLiteral("http://127.0.0.1:6006");
    }
    setBaseUrl(configuredUrl);
}

void BackendFileClient::setBaseUrl(const QString &baseUrl)
{
    m_baseUrl = baseUrl.trimmed();
    while (m_baseUrl.endsWith(QLatin1Char('/'))) {
        m_baseUrl.chop(1);
    }
    if (m_baseUrl.isEmpty()) {
        m_baseUrl = QStringLiteral("http://127.0.0.1:6006");
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
    downloadToFile(aiMaskDownloadUrl(jobId), jobId, destinationPath, true);
}

void BackendFileClient::downloadInputVolume(qint64 jobId, const QString &destinationPath)
{
    downloadToFile(inputVolumeDownloadUrl(jobId), jobId, destinationPath, false);
}

void BackendFileClient::downloadToFile(const QString &url,
                                       qint64 jobId,
                                       const QString &destinationPath,
                                       bool aiMask)
{
    QDir().mkpath(QFileInfo(destinationPath).absolutePath());
    auto file = QSharedPointer<QFile>::create(destinationPath);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString message =
            QStringLiteral("Cannot write download destination: %1").arg(destinationPath);
        if (aiMask) {
            emit aiMaskDownloadFailed(jobId, message, 0);
        } else {
            emit inputVolumeDownloadFailed(jobId, message, 0);
        }
        return;
    }

    QNetworkRequest request{QUrl(url)};
    QNetworkReply *reply = m_networkManager->get(request);
    auto bytesWritten = QSharedPointer<qint64>::create(0);
    auto writeFailed = QSharedPointer<bool>::create(false);
    const auto drainReply = [reply, file, bytesWritten, writeFailed]() {
        const QByteArray chunk = reply->readAll();
        if (chunk.isEmpty() || *writeFailed) {
            return;
        }
        const qint64 written = file->write(chunk);
        if (written != chunk.size()) {
            *writeFailed = true;
            return;
        }
        *bytesWritten += written;
    };

    connect(reply, &QNetworkReply::readyRead, this, drainReply);
    connect(reply, &QNetworkReply::finished, this,
            [this,
             reply,
             file,
             bytesWritten,
             writeFailed,
             drainReply,
             jobId,
             destinationPath,
             aiMask]() {
        drainReply();
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        file->close();
        if (reply->error() != QNetworkReply::NoError || *writeFailed) {
            QFile::remove(destinationPath);
            const QString message = *writeFailed
                ? QStringLiteral("Download failed while writing the destination file.")
                : QStringLiteral("Download failed: %1").arg(reply->errorString());
            if (aiMask) {
                emit aiMaskDownloadFailed(jobId, message, httpStatus);
            } else {
                emit inputVolumeDownloadFailed(jobId, message, httpStatus);
            }
        } else if (aiMask) {
            emit aiMaskDownloaded(jobId, destinationPath, *bytesWritten);
        } else {
            emit inputVolumeDownloaded(jobId, destinationPath, *bytesWritten);
        }
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
