#ifndef BACKENDCLIENT_H
#define BACKENDCLIENT_H

#include <QObject>
#include <QJsonObject>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

class BackendClient : public QObject
{
    Q_OBJECT

public:
    explicit BackendClient(QObject *parent = nullptr);

    void loadSettings();
    void setBaseUrl(const QString &baseUrl);
    void createJob(const QJsonObject &payload);
    void getJob(qint64 jobId);
    void getJobResult(qint64 jobId);

signals:
    void jobCreated(qint64 jobId);
    void jobFetched(const QJsonObject &job);
    void jobResultFetched(const QJsonObject &result);
    void errorOccurred(const QString &message);

private:
    QNetworkReply *sendGet(const QString &path);
    void handleCreateJobReply(QNetworkReply *reply);
    void handleJsonReply(QNetworkReply *reply, const std::function<void(const QJsonObject &)> &onSuccess);

    QNetworkAccessManager *m_networkManager;
    QString m_baseUrl;
};

#endif // BACKENDCLIENT_H
