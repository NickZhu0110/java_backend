#ifndef JOBWEBSOCKETCLIENT_H
#define JOBWEBSOCKETCLIENT_H

#include <QObject>
#include <QUrl>

class QWebSocket;

class JobWebSocketClient : public QObject
{
    Q_OBJECT

public:
    explicit JobWebSocketClient(QObject *parent = nullptr);

    void loadSettings();
    void setWebSocketUrl(const QString &url);
    void connectToServer(qint64 jobId);
    void disconnectFromServer();

signals:
    void jobStatusReceived(qint64 jobId, const QString &status, int progress);
    void connectionStatusChanged(const QString &message);
    void errorOccurred(const QString &message);

private slots:
    void handleTextMessage(const QString &message);

private:
    QWebSocket *m_webSocket;
    QUrl m_url;
    qint64 m_currentJobId;
};

#endif // JOBWEBSOCKETCLIENT_H
