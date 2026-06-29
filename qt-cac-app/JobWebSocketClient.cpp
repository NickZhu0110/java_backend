#include "JobWebSocketClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QWebSocket>

JobWebSocketClient::JobWebSocketClient(QObject *parent)
    : QObject(parent)
    , m_webSocket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , m_currentJobId(-1)
{
    loadSettings();

    connect(m_webSocket, &QWebSocket::connected, this, [this]() {
        emit connectionStatusChanged(QStringLiteral("WebSocket connected"));
    });
    connect(m_webSocket, &QWebSocket::disconnected, this, [this]() {
        emit connectionStatusChanged(QStringLiteral("WebSocket disconnected"));
    });
    connect(m_webSocket, &QWebSocket::textMessageReceived,
            this, &JobWebSocketClient::handleTextMessage);
    connect(m_webSocket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit errorOccurred(QStringLiteral("WebSocket error: %1").arg(m_webSocket->errorString()));
    });
}

void JobWebSocketClient::loadSettings()
{
    QSettings settings;
    setWebSocketUrl(settings.value(QStringLiteral("server/webSocketUrl"),
                                   QStringLiteral("ws://localhost:16006/ws/jobs")).toString());
}

void JobWebSocketClient::setWebSocketUrl(const QString &url)
{
    const QString trimmedUrl = url.trimmed();
    m_url = QUrl(trimmedUrl.isEmpty() ? QStringLiteral("ws://localhost:16006/ws/jobs") : trimmedUrl);
}

void JobWebSocketClient::connectToServer(qint64 jobId)
{
    m_currentJobId = jobId;
    if (m_webSocket->state() == QAbstractSocket::ConnectedState) {
        return;
    }
    m_webSocket->open(m_url);
    emit connectionStatusChanged(QStringLiteral("Connecting to WebSocket..."));
}

void JobWebSocketClient::disconnectFromServer()
{
    m_currentJobId = -1;
    m_webSocket->close();
}

void JobWebSocketClient::handleTextMessage(const QString &message)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit errorOccurred(QStringLiteral("Invalid WebSocket message: %1").arg(message));
        return;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("type")).toString() != QStringLiteral("JOB_STATUS")) {
        return;
    }

    const qint64 jobId = static_cast<qint64>(object.value(QStringLiteral("jobId")).toDouble(-1));
    if (jobId != m_currentJobId) {
        return;
    }

    const QString status = object.value(QStringLiteral("status")).toString();
    const int progress = object.value(QStringLiteral("progress")).toInt(0);
    emit jobStatusReceived(jobId, status, progress);
}
