#include "BackendClient.h"

#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

BackendClient::BackendClient(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
    loadSettings();
}

void BackendClient::loadSettings()
{
    QSettings settings;
    setBaseUrl(settings.value(QStringLiteral("server/backendUrl"),
                              QStringLiteral("http://localhost:8080")).toString());
}

void BackendClient::setBaseUrl(const QString &baseUrl)
{
    m_baseUrl = baseUrl.trimmed();
    while (m_baseUrl.endsWith(QLatin1Char('/'))) {
        m_baseUrl.chop(1);
    }
    if (m_baseUrl.isEmpty()) {
        m_baseUrl = QStringLiteral("http://localhost:8080");
    }
}

void BackendClient::createJob(const QJsonObject &payload)
{
    QNetworkRequest request(QUrl(m_baseUrl + QStringLiteral("/api/jobs")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_networkManager->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleCreateJobReply(reply);
    });
}

void BackendClient::getJob(qint64 jobId)
{
    QNetworkReply *reply = sendGet(QStringLiteral("/api/jobs/%1").arg(jobId));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleJsonReply(reply, [this](const QJsonObject &job) {
            emit jobFetched(job);
        });
    });
}

void BackendClient::getJobResult(qint64 jobId)
{
    QNetworkReply *reply = sendGet(QStringLiteral("/api/jobs/%1/result").arg(jobId));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleJsonReply(reply, [this](const QJsonObject &result) {
            emit jobResultFetched(result);
        });
    });
}

QNetworkReply *BackendClient::sendGet(const QString &path)
{
    QNetworkRequest request(QUrl(m_baseUrl + path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return m_networkManager->get(request);
}

void BackendClient::handleCreateJobReply(QNetworkReply *reply)
{
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(QStringLiteral("Create job failed: %1\n%2")
                               .arg(reply->errorString(), QString::fromUtf8(body)));
        reply->deleteLater();
        return;
    }

    qint64 jobId = -1;
    bool ok = false;
    jobId = QString::fromUtf8(body).trimmed().toLongLong(&ok);

    if (!ok) {
        const QJsonDocument document = QJsonDocument::fromJson(body);
        if (document.isObject()) {
            const QJsonValue idValue = document.object().value(QStringLiteral("id"));
            if (idValue.isDouble()) {
                jobId = static_cast<qint64>(idValue.toDouble());
                ok = true;
            } else if (idValue.isString()) {
                jobId = idValue.toString().toLongLong(&ok);
            }
        }
    }

    if (!ok || jobId <= 0) {
        emit errorOccurred(QStringLiteral("Create job response did not contain a valid job id: %1")
                               .arg(QString::fromUtf8(body)));
        reply->deleteLater();
        return;
    }

    emit jobCreated(jobId);
    reply->deleteLater();
}

void BackendClient::handleJsonReply(QNetworkReply *reply, const std::function<void(const QJsonObject &)> &onSuccess)
{
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
        emit errorOccurred(QStringLiteral("Backend request failed: %1\n%2")
                               .arg(reply->errorString(), QString::fromUtf8(body)));
        reply->deleteLater();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit errorOccurred(QStringLiteral("Backend returned invalid JSON: %1")
                               .arg(QString::fromUtf8(body)));
        reply->deleteLater();
        return;
    }

    onSuccess(document.object());
    reply->deleteLater();
}
