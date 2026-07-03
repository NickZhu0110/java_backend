#ifndef SERVERSETTINGSDIALOG_H
#define SERVERSETTINGSDIALOG_H

#include <QDialog>

class QNetworkAccessManager;
class QNetworkReply;

namespace Ui {
class ServerSettingsDialog;
}

class ServerSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ServerSettingsDialog(QWidget *parent = nullptr);
    ~ServerSettingsDialog() override;

    QString backendUrl() const;
    QString webSocketUrl() const;

signals:
    void connectionTested(bool connected, const QString &backendUrl, const QString &webSocketUrl);

private:
    void loadSettings();
    void saveSettings();
    void testConnection();
    void handleTestReply(QNetworkReply *reply, bool triedFallback);

    Ui::ServerSettingsDialog *ui;
    QNetworkAccessManager *m_networkManager;
};

#endif // SERVERSETTINGSDIALOG_H
