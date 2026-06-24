#include "mainwindow.h"
#include "BackendClient.h"
#include "JobWebSocketClient.h"
#include "ServerSettingsDialog.h"
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QCheckBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_backendClient(new BackendClient(this))
    , m_webSocketClient(new JobWebSocketClient(this))
    , m_currentJobId(-1)
    , m_backendConnected(false)
{
    ui->setupUi(this);
    setupInitialState();
    connectSignals();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupInitialState()
{
    setWindowTitle(QStringLiteral("CAC Analysis Client"));
    resize(1200, 800);
    setMinimumSize(900, 600);

    ui->fileTypeComboBox->clear();
    ui->fileTypeComboBox->addItems({QStringLiteral("dcm"),
                                    QStringLiteral("mhd"),
                                    QStringLiteral("nii"),
                                    QStringLiteral("nrrd")});
    ui->fileTypeComboBox->setCurrentText(QStringLiteral("dcm"));

    ui->deviceComboBox->clear();
    ui->deviceComboBox->addItem(QStringLiteral("CPU"), QStringLiteral("cpu"));
    ui->deviceComboBox->addItem(QStringLiteral("CUDA GPU"), QStringLiteral("cuda"));
    ui->deviceComboBox->setCurrentIndex(0);

    ui->enableAdvancedOverridesCheckBox->setChecked(false);
    ui->useZeroModuleCheckBox->setChecked(false);
    ui->progressBar->setRange(0, 100);
    setAdvancedOverridesEnabled(false);
    setBackendConnected(false, QStringLiteral("Backend: Not connected"));
    resetForm();
}

void MainWindow::connectSignals()
{
    connect(ui->segmentcacsSrcBrowseButton, &QPushButton::clicked, this, [this]() {
        browseDirectory(ui->segmentcacsSrcLineEdit);
    });
    connect(ui->modelBrowseButton, &QPushButton::clicked, this, [this]() {
        browseFile(ui->modelLineEdit, QStringLiteral("PyTorch model (*.pt);;All files (*)"));
    });
    connect(ui->inputPathBrowseButton, &QPushButton::clicked, this, [this]() {
        browseDirectory(ui->inputPathLineEdit);
    });
    connect(ui->outputPathBrowseButton, &QPushButton::clicked, this, [this]() {
        browseDirectory(ui->outputPathLineEdit);
    });
    connect(ui->submitButton, &QPushButton::clicked, this, &MainWindow::submitJob);
    connect(ui->resetButton, &QPushButton::clicked, this, &MainWindow::resetForm);
    connect(ui->enableAdvancedOverridesCheckBox, &QCheckBox::toggled,
            this, &MainWindow::setAdvancedOverridesEnabled);
    connect(ui->actionServerSettings, &QAction::triggered,
            this, &MainWindow::openServerSettings);

    connect(m_backendClient, &BackendClient::jobCreated, this, [this](qint64 jobId) {
        m_currentJobId = jobId;
        ui->jobIdLabel->setText(QStringLiteral("Job ID: %1").arg(jobId));
        ui->statusLabel->setText(QStringLiteral("Status: SUBMITTED"));
        ui->progressBar->setValue(0);
        appendLog(QStringLiteral("Job created: %1").arg(jobId));
        m_webSocketClient->connectToServer(jobId);
    });
    connect(m_backendClient, &BackendClient::jobResultFetched,
            this, &MainWindow::displayResult);
    connect(m_backendClient, &BackendClient::errorOccurred, this, [this](const QString &message) {
        appendLog(message);
        QMessageBox::warning(this, QStringLiteral("Backend Error"), message);
        updateSubmitButton();
    });

    connect(m_webSocketClient, &JobWebSocketClient::jobStatusReceived,
            this, [this](qint64 jobId, const QString &status, int progress) {
        ui->jobIdLabel->setText(QStringLiteral("Job ID: %1").arg(jobId));
        ui->statusLabel->setText(QStringLiteral("Status: %1").arg(status));
        ui->progressBar->setValue(progress);
        appendLog(QStringLiteral("Job %1: %2 (%3%)").arg(jobId).arg(status).arg(progress));

        if (status.compare(QStringLiteral("SUCCESS"), Qt::CaseInsensitive) == 0) {
            ui->progressBar->setValue(100);
            appendLog(QStringLiteral("Fetching result..."));
            m_backendClient->getJobResult(jobId);
            m_webSocketClient->disconnectFromServer();
        } else if (status.compare(QStringLiteral("FAILED"), Qt::CaseInsensitive) == 0) {
            updateSubmitButton();
            m_webSocketClient->disconnectFromServer();
        }
    });
    connect(m_webSocketClient, &JobWebSocketClient::connectionStatusChanged,
            this, [this](const QString &message) {
        appendLog(message);
    });
    connect(m_webSocketClient, &JobWebSocketClient::errorOccurred, this, [this](const QString &message) {
        appendLog(message);
    });
}

void MainWindow::appendLog(const QString &message)
{
    ui->resultTextEdit->append(message);
}

bool MainWindow::validateInputs() const
{
    QStringList missingFields;
    if (ui->inputPathLineEdit->text().trimmed().isEmpty()) {
        missingFields << QStringLiteral("input path");
    }
    if (ui->outputPathLineEdit->text().trimmed().isEmpty()) {
        missingFields << QStringLiteral("output path");
    }

    if (!missingFields.isEmpty()) {
        QMessageBox::warning(const_cast<MainWindow *>(this),
                             QStringLiteral("Missing Required Fields"),
                             QStringLiteral("Please fill: %1").arg(missingFields.join(QStringLiteral(", "))));
        return false;
    }

    return true;
}

void MainWindow::submitJob()
{
    if (!m_backendConnected) {
        QMessageBox::warning(this, QStringLiteral("Backend Not Connected"),
                             QStringLiteral("Backend is not connected. Please configure and test server settings first."));
        return;
    }

    if (!validateInputs()) {
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("modelName"), QStringLiteral("SEGMENT-CACS"));
    payload.insert(QStringLiteral("inputPath"), ui->inputPathLineEdit->text().trimmed());
    payload.insert(QStringLiteral("outputPath"), ui->outputPathLineEdit->text().trimmed());
    payload.insert(QStringLiteral("fileType"), ui->fileTypeComboBox->currentText());
    payload.insert(QStringLiteral("device"), selectedDeviceValue());

    if (ui->enableAdvancedOverridesCheckBox->isChecked()) {
        const QString segmentcacsSrc = ui->segmentcacsSrcLineEdit->text().trimmed();
        const QString modelPath = ui->modelLineEdit->text().trimmed();

        // segmentcacsSrc, modelPath, and useZeroModule are optional frontend-provided
        // overrides. The backend or Python Worker may override or ignore these values
        // depending on server configuration.
        if (!segmentcacsSrc.isEmpty()) {
            payload.insert(QStringLiteral("segmentcacsSrc"), segmentcacsSrc);
        }
        if (!modelPath.isEmpty()) {
            payload.insert(QStringLiteral("modelPath"), modelPath);
        }
        payload.insert(QStringLiteral("useZeroModule"), ui->useZeroModuleCheckBox->isChecked());
    }

    ui->submitButton->setEnabled(false);
    ui->statusLabel->setText(QStringLiteral("Status: SUBMITTING"));
    appendLog(QStringLiteral("Submitting analysis job..."));
    appendLog(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));

    m_backendClient->createJob(payload);
}

void MainWindow::resetForm()
{
    m_currentJobId = -1;
    ui->segmentcacsSrcLineEdit->clear();
    ui->modelLineEdit->clear();
    ui->inputPathLineEdit->clear();
    ui->outputPathLineEdit->clear();
    ui->fileTypeComboBox->setCurrentText(QStringLiteral("dcm"));
    ui->deviceComboBox->setCurrentIndex(0);
    ui->enableAdvancedOverridesCheckBox->setChecked(false);
    ui->useZeroModuleCheckBox->setChecked(false);

    ui->jobIdLabel->setText(QStringLiteral("Job ID: -"));
    ui->statusLabel->setText(QStringLiteral("Status: IDLE"));
    ui->progressBar->setValue(0);

    ui->agatstonScoreLabel->setText(QStringLiteral("Agatston Score: -"));
    ui->riskGradeLabel->setText(QStringLiteral("Risk Grade: -"));
    ui->resultJsonPathLabel->setText(QStringLiteral("Result JSON Path: -"));
    ui->aiMaskPathLabel->setText(QStringLiteral("AI Mask Path: -"));
    ui->correctedMaskPathLabel->setText(QStringLiteral("Corrected Mask Path: -"));
    ui->reportPathLabel->setText(QStringLiteral("Report Path: -"));
    ui->resultTextEdit->clear();
    updateSubmitButton();

    m_webSocketClient->disconnectFromServer();
}

void MainWindow::setAdvancedOverridesEnabled(bool enabled)
{
    ui->segmentcacsSrcLineEdit->setEnabled(enabled);
    ui->segmentcacsSrcBrowseButton->setEnabled(enabled);
    ui->modelLineEdit->setEnabled(enabled);
    ui->modelBrowseButton->setEnabled(enabled);
    ui->useZeroModuleCheckBox->setEnabled(enabled);
}

void MainWindow::setBackendConnected(bool connected, const QString &statusText)
{
    m_backendConnected = connected;
    ui->backendStatusLabel->setText(statusText);
    updateSubmitButton();
}

void MainWindow::updateSubmitButton()
{
    ui->submitButton->setEnabled(m_backendConnected);
}

QString MainWindow::selectedDeviceValue() const
{
    const QString value = ui->deviceComboBox->currentData().toString();
    return value.isEmpty() ? QStringLiteral("cpu") : value;
}

void MainWindow::browseDirectory(QLineEdit *lineEdit)
{
    const QString directory = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Directory"), lineEdit->text());
    if (!directory.isEmpty()) {
        lineEdit->setText(directory);
    }
}

void MainWindow::browseFile(QLineEdit *lineEdit, const QString &filter)
{
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Select File"), lineEdit->text(), filter);
    if (!file.isEmpty()) {
        lineEdit->setText(file);
    }
}

void MainWindow::displayResult(const QJsonObject &result)
{
    ui->agatstonScoreLabel->setText(QStringLiteral("Agatston Score: %1")
                                        .arg(result.value(QStringLiteral("agatstonScore")).toVariant().toString()));
    ui->riskGradeLabel->setText(QStringLiteral("Risk Grade: %1")
                                    .arg(result.value(QStringLiteral("riskGrade")).toString(QStringLiteral("-"))));
    ui->resultJsonPathLabel->setText(QStringLiteral("Result JSON Path: %1")
                                         .arg(result.value(QStringLiteral("resultJsonPath")).toString(QStringLiteral("-"))));
    ui->aiMaskPathLabel->setText(QStringLiteral("AI Mask Path: %1")
                                     .arg(result.value(QStringLiteral("aiMaskPath")).toString(QStringLiteral("-"))));
    ui->correctedMaskPathLabel->setText(QStringLiteral("Corrected Mask Path: %1")
                                            .arg(result.value(QStringLiteral("correctedMaskPath")).toString(QStringLiteral("-"))));
    ui->reportPathLabel->setText(QStringLiteral("Report Path: %1")
                                     .arg(result.value(QStringLiteral("reportPath")).toString(QStringLiteral("-"))));

    appendLog(QStringLiteral("Result received:"));
    appendLog(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented)));
    updateSubmitButton();
}

void MainWindow::openServerSettings()
{
    ServerSettingsDialog dialog(this);
    connect(&dialog, &ServerSettingsDialog::connectionTested,
            this, [this](bool connected, const QString &backendUrl, const QString &webSocketUrl) {
        if (connected) {
            m_backendClient->setBaseUrl(backendUrl);
            m_webSocketClient->setWebSocketUrl(webSocketUrl);
            setBackendConnected(true, QStringLiteral("Backend: Connected"));
            appendLog(QStringLiteral("Backend connection test successful"));
        } else {
            setBackendConnected(false, QStringLiteral("Backend: Connection failed"));
            appendLog(QStringLiteral("Backend connection test failed"));
        }
    });
    if (dialog.exec() == QDialog::Accepted) {
        m_backendClient->setBaseUrl(dialog.backendUrl());
        m_webSocketClient->setWebSocketUrl(dialog.webSocketUrl());
        appendLog(QStringLiteral("Server settings saved"));
    }
}
