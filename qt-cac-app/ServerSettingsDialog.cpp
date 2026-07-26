#include "ServerSettingsDialog.h"
#include "ui_ServerSettingsDialog.h"

#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

namespace {
QString normalizedBaseUrl(const QString &url)
{
    QString normalized = url.trimmed();
    while (normalized.endsWith(QLatin1Char('/'))) {
        normalized.chop(1);
    }
    return normalized;
}
}

ServerSettingsDialog::ServerSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::ServerSettingsDialog)
    , m_networkManager(new QNetworkAccessManager(this))
{
    ui->setupUi(this);
    ui->sshPasswordLineEdit->setEchoMode(QLineEdit::Password);
    loadSettings();

    connect(ui->testConnectionButton, &QPushButton::clicked,
            this, &ServerSettingsDialog::testConnection);
    connect(ui->saveButton, &QPushButton::clicked, this, [this]() {
        if (backendUrl().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Missing Backend URL"),
                                 QStringLiteral("Backend URL cannot be empty."));
            return;
        }
        if (webSocketUrl().isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Missing WebSocket URL"),
                                 QStringLiteral("WebSocket URL cannot be empty."));
            return;
        }
        saveSettings();
        accept();
    });
    connect(ui->cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

ServerSettingsDialog::~ServerSettingsDialog()
{
    delete ui;
}

QString ServerSettingsDialog::backendUrl() const
{
    return normalizedBaseUrl(ui->backendUrlLineEdit->text());
}

QString ServerSettingsDialog::webSocketUrl() const
{
    return ui->webSocketUrlLineEdit->text().trimmed();
}

void ServerSettingsDialog::loadSettings()
{
    QSettings settings;
    ui->backendUrlLineEdit->setText(settings.value(QStringLiteral("server/backendUrl"),
                                                   QStringLiteral("http://127.0.0.1:6006")).toString());
    ui->webSocketUrlLineEdit->setText(settings.value(QStringLiteral("server/webSocketUrl"),
                                                     QStringLiteral("ws://127.0.0.1:6006/ws/jobs")).toString());
    ui->sshHostLineEdit->setText(settings.value(QStringLiteral("ssh/host")).toString());
    ui->sshPortSpinBox->setValue(settings.value(QStringLiteral("ssh/port"), 22).toInt());
    ui->sshUsernameLineEdit->setText(settings.value(QStringLiteral("ssh/username")).toString());

    const bool rememberPassword = settings.value(QStringLiteral("ssh/rememberPassword"), false).toBool();
    ui->rememberPasswordCheckBox->setChecked(rememberPassword);
    ui->sshPasswordLineEdit->setText(rememberPassword
                                         ? settings.value(QStringLiteral("ssh/password")).toString()
                                         : QString());
}

void ServerSettingsDialog::saveSettings()
{
    QSettings settings;
    settings.setValue(QStringLiteral("server/backendUrl"), backendUrl());
    settings.setValue(QStringLiteral("server/webSocketUrl"), webSocketUrl());
    settings.setValue(QStringLiteral("ssh/host"), ui->sshHostLineEdit->text().trimmed());
    settings.setValue(QStringLiteral("ssh/port"), ui->sshPortSpinBox->value());
    settings.setValue(QStringLiteral("ssh/username"), ui->sshUsernameLineEdit->text().trimmed());
    settings.setValue(QStringLiteral("ssh/rememberPassword"), ui->rememberPasswordCheckBox->isChecked());

    if (ui->rememberPasswordCheckBox->isChecked()) {
        // QSettings is not secure encrypted storage; saving this password is only for local development.
        settings.setValue(QStringLiteral("ssh/password"), ui->sshPasswordLineEdit->text());
    } else {
        settings.remove(QStringLiteral("ssh/password"));
    }
}

void ServerSettingsDialog::testConnection()
{
    if (backendUrl().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Missing Backend URL"),
                             QStringLiteral("Backend URL cannot be empty."));
        return;
    }

    ui->testConnectionButton->setEnabled(false);
    QNetworkRequest request(QUrl(backendUrl() + QStringLiteral("/actuator/health")));
    request.setTransferTimeout(5000);
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleTestReply(reply, false);
    });
}

void ServerSettingsDialog::handleTestReply(QNetworkReply *reply, bool triedFallback)
{
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool success = (httpStatus >= 200 && httpStatus < 300) || httpStatus == 404;
    const QString errorString = reply->errorString();
    reply->deleteLater();

    if (success) {
        saveSettings();
        ui->testConnectionButton->setEnabled(true);
        emit connectionTested(true, backendUrl(), webSocketUrl());
        QMessageBox::information(this, QStringLiteral("Connection Test"),
                                 QStringLiteral("Backend connection successful"));
        return;
    }

    if (!triedFallback) {
        QNetworkRequest fallbackRequest(QUrl(backendUrl() + QStringLiteral("/api/jobs/1")));
        fallbackRequest.setTransferTimeout(5000);
        QNetworkReply *fallbackReply = m_networkManager->get(fallbackRequest);
        connect(fallbackReply, &QNetworkReply::finished, this, [this, fallbackReply]() {
            handleTestReply(fallbackReply, true);
        });
        return;
    }

    ui->testConnectionButton->setEnabled(true);
    emit connectionTested(false, backendUrl(), webSocketUrl());
    QMessageBox::warning(this, QStringLiteral("Connection Test"),
                         QStringLiteral("Backend connection failed: %1").arg(errorString));
}
