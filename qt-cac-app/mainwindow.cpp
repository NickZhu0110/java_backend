#include "mainwindow.h"
#include "BackendClient.h"
#include "cache/JobFileCacheManager.h"
#include "io/BackendFileClient.h"
#include "viewer/CTViewerWidget.h"
#include "JobWebSocketClient.h"
#include "ServerSettingsDialog.h"
#include "ui_mainwindow.h"

#include <QFileDialog>
#include <QBoxLayout>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_backendClient(new BackendClient(this))
    , m_backendFileClient(new BackendFileClient(this))
    , m_webSocketClient(new JobWebSocketClient(this))
    , m_ctViewerWidget(nullptr)
    , m_currentJobId(-1)
    , m_backendConnected(false)
{
    ui->setupUi(this);
    embedCtViewer();
    setupInitialState();
    connectSignals();
    loadMostRecentCaseCacheIfAvailable();
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

    connect(m_backendFileClient, &BackendFileClient::aiMaskDownloaded,
            this, [this](qint64 jobId, const QString &destinationPath, qint64 bytesWritten) {
        appendLog(QStringLiteral("Saved AI mask to local cache: %1").arg(destinationPath));
        appendLog(QStringLiteral("Downloaded AI mask bytes: %1").arg(bytesWritten));

        if (m_ctViewerWidget) {
            m_ctViewerWidget->loadMaskFromLocalPath(destinationPath);
        }

        JobFileCacheManager cacheManager;
        const QString caseKey = m_currentCaseKey.isEmpty()
            ? cacheManager.caseKeyFromInputPath(m_currentJobInputPath)
            : m_currentCaseKey;

        QJsonObject metadata;
        metadata.insert(QStringLiteral("jobId"), QString::number(jobId));
        metadata.insert(QStringLiteral("caseKey"), caseKey);
        metadata.insert(QStringLiteral("originalInputPath"), m_currentJobInputPath);
        metadata.insert(QStringLiteral("serverResultJsonPath"), m_currentServerResultJsonPath);
        metadata.insert(QStringLiteral("serverAiMaskPath"), m_currentServerAiMaskPath);
        metadata.insert(QStringLiteral("localCaseCacheDir"), cacheManager.getCaseCacheDir(caseKey));
        metadata.insert(QStringLiteral("localAiMaskPath"), destinationPath);
        metadata.insert(QStringLiteral("downloadedBytes"), QString::number(bytesWritten));
        metadata.insert(QStringLiteral("downloadedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        cacheManager.writeJobMetadata(jobId, metadata);
        cacheManager.writeCaseMetadata(caseKey, metadata);
        tryLoadCurrentCaseFromCache();
    });

    connect(m_backendFileClient, &BackendFileClient::aiMaskDownloadFailed,
            this, [this](qint64 jobId, const QString &message, int httpStatus) {
        appendLog(QStringLiteral("AI mask download failed for job %1, HTTP status %2: %3")
                      .arg(jobId)
                      .arg(httpStatus)
                      .arg(message));
        appendLog(QStringLiteral("Synthetic viewer fallback remains active."));
    });

    connect(m_backendFileClient, &BackendFileClient::inputVolumeDownloaded,
            this, [this](qint64 jobId, const QString &destinationPath, qint64 bytesWritten) {
        appendLog(QStringLiteral("Saved input volume to local cache: %1").arg(destinationPath));
        appendLog(QStringLiteral("Downloaded input volume bytes: %1").arg(bytesWritten));
        appendLog(QStringLiteral("Input volume artifact is cached; attempting case load when mask is also available."));

        JobFileCacheManager cacheManager;
        const QString caseKey = m_currentCaseKey.isEmpty()
            ? cacheManager.caseKeyFromInputPath(m_currentJobInputPath)
            : m_currentCaseKey;
        QJsonObject metadata;
        metadata.insert(QStringLiteral("jobId"), QString::number(jobId));
        metadata.insert(QStringLiteral("caseKey"), caseKey);
        metadata.insert(QStringLiteral("serverInputPath"), m_currentJobInputPath);
        metadata.insert(QStringLiteral("localInputVolumePath"), destinationPath);
        metadata.insert(QStringLiteral("inputDownloadStatus"), QStringLiteral("downloaded"));
        metadata.insert(QStringLiteral("downloadedBytes"), QString::number(bytesWritten));
        metadata.insert(QStringLiteral("downloadedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        metadata.insert(QStringLiteral("note"),
                        QStringLiteral("Current compatibility mode downloads server-side input volume. Temporary SimpleITK preprocessing is isolated behind CaseVolumeLoader."));
        cacheManager.writeCaseMetadata(caseKey, metadata);

        if (m_ctViewerWidget) {
            m_ctViewerWidget->loadVolumeFromLocalPath(destinationPath);
        }
        tryLoadCurrentCaseFromCache();
    });

    connect(m_backendFileClient, &BackendFileClient::inputVolumeDownloadFailed,
            this, [this](qint64 jobId, const QString &message, int httpStatus) {
        appendLog(QStringLiteral("Input volume download failed for job %1, HTTP status %2: %3")
                      .arg(jobId)
                      .arg(httpStatus)
                      .arg(message));
        appendLog(QStringLiteral("Real CT display remains blocked; synthetic viewer fallback remains active."));
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
    m_currentJobInputPath = ui->inputPathLineEdit->text().trimmed();
    prepareCaseCacheForInput(m_currentJobInputPath);
    payload.insert(QStringLiteral("modelName"), QStringLiteral("SEGMENT-CACS"));
    payload.insert(QStringLiteral("inputPath"), m_currentJobInputPath);
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
    m_currentJobInputPath.clear();
    m_currentCaseKey.clear();
    m_currentServerResultJsonPath.clear();
    m_currentServerAiMaskPath.clear();
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
        if (lineEdit == ui->inputPathLineEdit) {
            m_currentJobInputPath = directory;
            prepareCaseCacheForInput(directory);
        }
    }
}

void MainWindow::browseFile(QLineEdit *lineEdit, const QString &filter)
{
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Select File"), lineEdit->text(), filter);
    if (!file.isEmpty()) {
        lineEdit->setText(file);
        if (lineEdit == ui->inputPathLineEdit) {
            m_currentJobInputPath = file;
            prepareCaseCacheForInput(file);
        }
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
    handleJobResultFiles(result);
    updateSubmitButton();
}

void MainWindow::openServerSettings()
{
    ServerSettingsDialog dialog(this);
    connect(&dialog, &ServerSettingsDialog::connectionTested,
            this, [this](bool connected, const QString &backendUrl, const QString &webSocketUrl) {
        if (connected) {
            m_backendClient->setBaseUrl(backendUrl);
            m_backendFileClient->setBaseUrl(backendUrl);
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
        m_backendFileClient->setBaseUrl(dialog.backendUrl());
        m_webSocketClient->setWebSocketUrl(dialog.webSocketUrl());
        appendLog(QStringLiteral("Server settings saved"));
    }
}

void MainWindow::embedCtViewer()
{
    QWidget *placeholder = ui->dicomViewerPlaceholderLabel;
    QWidget *parentWidget = placeholder ? placeholder->parentWidget() : nullptr;
    QLayout *parentLayout = parentWidget ? parentWidget->layout() : nullptr;
    auto *boxLayout = qobject_cast<QBoxLayout *>(parentLayout);
    if (!placeholder || !parentWidget || !parentLayout || !boxLayout) {
        appendLog(QStringLiteral("CT viewer placeholder not found; keeping existing UI."));
        return;
    }

    const int index = parentLayout->indexOf(placeholder);
    if (index < 0) {
        appendLog(QStringLiteral("CT viewer placeholder is not in its parent layout; keeping existing UI."));
        return;
    }

    m_ctViewerWidget = new CTViewerWidget(parentWidget);
    m_ctViewerWidget->setObjectName(QStringLiteral("ctViewerWidget"));

    parentLayout->removeWidget(placeholder);
    placeholder->hide();
    placeholder->deleteLater();
    boxLayout->insertWidget(index, m_ctViewerWidget);
}

void MainWindow::prepareCaseCacheForInput(const QString &inputPath)
{
    if (inputPath.trimmed().isEmpty()) {
        return;
    }

    JobFileCacheManager cacheManager;
    m_currentCaseKey = cacheManager.caseKeyFromInputPath(inputPath);
    cacheManager.ensureCaseCacheDir(m_currentCaseKey);

    const QString caseCacheDir = cacheManager.getCaseCacheDir(m_currentCaseKey);
    const QString inputVolumeDir = cacheManager.localInputVolumeDir(m_currentCaseKey);
    QFileInfo inputInfo(inputPath);

    QJsonObject metadata;
    metadata.insert(QStringLiteral("caseKey"), m_currentCaseKey);
    metadata.insert(QStringLiteral("originalInputPath"), inputPath);
    metadata.insert(QStringLiteral("localCaseCacheDir"), caseCacheDir);
    metadata.insert(QStringLiteral("localInputVolumeDir"), inputVolumeDir);
    metadata.insert(QStringLiteral("inputReferenceMode"), true);
    metadata.insert(QStringLiteral("inputExistsOnThisMac"), inputInfo.exists());
    metadata.insert(QStringLiteral("inputIsDir"), inputInfo.isDir());
    metadata.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    metadata.insert(QStringLiteral("note"),
                    QStringLiteral("Current compatibility mode keeps the submitted input path unchanged for the remote backend. Large CT input is referenced in metadata instead of copied."));
    cacheManager.writeCaseMetadata(m_currentCaseKey, metadata);

    appendLog(QStringLiteral("Case key: %1").arg(m_currentCaseKey));
    appendLog(QStringLiteral("Creating/reusing local case cache: %1").arg(caseCacheDir));
    appendLog(QStringLiteral("Input reference mode active. Original input path: %1").arg(inputPath));
}

void MainWindow::handleJobResultFiles(const QJsonObject &result)
{
    if (m_currentJobId <= 0) {
        appendLog(QStringLiteral("No current job id; skipping local result cache setup."));
        return;
    }

    JobFileCacheManager cacheManager;
    const QString inputPath = m_currentJobInputPath.isEmpty()
        ? ui->inputPathLineEdit->text().trimmed()
        : m_currentJobInputPath;
    if (m_currentCaseKey.isEmpty()) {
        m_currentCaseKey = cacheManager.caseKeyFromInputPath(inputPath);
    }

    cacheManager.ensureCaseCacheDir(m_currentCaseKey);
    cacheManager.ensureJobCacheDir(m_currentJobId);

    const QString caseCacheDir = cacheManager.getCaseCacheDir(m_currentCaseKey);
    const QString resultJsonPath = cacheManager.localResultJsonPath(m_currentCaseKey);
    m_currentServerResultJsonPath = result.value(QStringLiteral("resultJsonPath")).toString();
    m_currentServerAiMaskPath = result.value(QStringLiteral("aiMaskPath")).toString();

    writeJsonFile(resultJsonPath, result);

    const QString localAiMaskPath = QFile::exists(cacheManager.localAiMaskPath(m_currentCaseKey))
        ? cacheManager.localAiMaskJobPath(m_currentCaseKey, m_currentJobId)
        : cacheManager.localAiMaskPath(m_currentCaseKey);
    const QString localInputVolumePath = cacheManager.localInputVolumeZipPath(m_currentCaseKey, m_currentJobId);
    const QString downloadUrl = m_backendFileClient->aiMaskDownloadUrl(m_currentJobId);
    const QString inputDownloadUrl = m_backendFileClient->inputVolumeDownloadUrl(m_currentJobId);

    appendLog(QStringLiteral("Job ID: %1").arg(m_currentJobId));
    appendLog(QStringLiteral("Server input path: %1").arg(inputPath.isEmpty() ? QStringLiteral("-") : inputPath));
    appendLog(QStringLiteral("Server AI mask path: %1").arg(m_currentServerAiMaskPath.isEmpty() ? QStringLiteral("-") : m_currentServerAiMaskPath));
    appendLog(QStringLiteral("Case key: %1").arg(m_currentCaseKey));
    appendLog(QStringLiteral("Local case cache: %1").arg(caseCacheDir));
    appendLog(QStringLiteral("Saved result JSON to local cache: %1").arg(resultJsonPath));
    appendLog(QStringLiteral("AI mask download URL: %1").arg(downloadUrl));
    appendLog(QStringLiteral("Local AI mask target: %1").arg(localAiMaskPath));
    appendLog(QStringLiteral("Input volume download URL: %1").arg(inputDownloadUrl));
    appendLog(QStringLiteral("Local input volume target: %1").arg(localInputVolumePath));
    appendLog(QStringLiteral("Viewer will load from local case cache after input volume and mask are present."));

    QJsonObject jobMetadata;
    jobMetadata.insert(QStringLiteral("jobId"), QString::number(m_currentJobId));
    jobMetadata.insert(QStringLiteral("caseKey"), m_currentCaseKey);
    jobMetadata.insert(QStringLiteral("originalInputPath"), inputPath);
    jobMetadata.insert(QStringLiteral("serverResultJsonPath"), m_currentServerResultJsonPath);
    jobMetadata.insert(QStringLiteral("serverAiMaskPath"), m_currentServerAiMaskPath);
    jobMetadata.insert(QStringLiteral("serverInputPath"), inputPath);
    jobMetadata.insert(QStringLiteral("localCaseCacheDir"), caseCacheDir);
    jobMetadata.insert(QStringLiteral("localResultJsonPath"), resultJsonPath);
    jobMetadata.insert(QStringLiteral("localAiMaskPath"), localAiMaskPath);
    jobMetadata.insert(QStringLiteral("localInputVolumePath"), localInputVolumePath);
    jobMetadata.insert(QStringLiteral("downloadUrl"), downloadUrl);
    jobMetadata.insert(QStringLiteral("inputDownloadUrl"), inputDownloadUrl);
    jobMetadata.insert(QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    jobMetadata.insert(QStringLiteral("downloadedAt"), QString());
    cacheManager.writeJobMetadata(m_currentJobId, jobMetadata);

    // Server-local paths such as /root/autodl-tmp/... are metadata only. The Qt
    // client must download artifacts through backend endpoints into case cache.
    m_backendFileClient->downloadAiMask(m_currentJobId, localAiMaskPath);
    m_backendFileClient->downloadInputVolume(m_currentJobId, localInputVolumePath);
}

void MainWindow::tryLoadCurrentCaseFromCache()
{
    if (!m_ctViewerWidget || m_currentCaseKey.isEmpty()) {
        return;
    }

    JobFileCacheManager cacheManager;
    const QString caseDir = cacheManager.getCaseCacheDir(m_currentCaseKey);
    const QString canonicalMaskPath = cacheManager.localAiMaskPath(m_currentCaseKey);
    const QString inputVolumeDir = cacheManager.localInputVolumeDir(m_currentCaseKey);
    const bool hasMask = QFileInfo::exists(canonicalMaskPath)
        || !QDir(QDir(caseDir).filePath(QStringLiteral("ai_masks"))).entryInfoList({QStringLiteral("*.nrrd")}, QDir::Files).isEmpty();
    const bool hasRawCt = QFileInfo::exists(QDir(inputVolumeDir).filePath(QStringLiteral("ct_volume_int16.raw")))
        && QFileInfo::exists(QDir(inputVolumeDir).filePath(QStringLiteral("ct_volume_metadata.json")));
    const bool hasInputArtifact = hasRawCt
        || QFileInfo::exists(QDir(inputVolumeDir).filePath(QStringLiteral("dicom_series")))
        || !QDir(inputVolumeDir).entryInfoList({QStringLiteral("input_volume_job_*.zip"),
                                                QStringLiteral("*.nrrd"),
                                                QStringLiteral("*.nii"),
                                                QStringLiteral("*.nii.gz"),
                                                QStringLiteral("*.mhd")},
                                               QDir::Files).isEmpty();

    if (!hasMask || !hasInputArtifact) {
        appendLog(QStringLiteral("Case cache is not ready for real CT display yet. hasMask=%1 hasInput=%2")
                      .arg(hasMask)
                      .arg(hasInputArtifact));
        return;
    }

    appendLog(QStringLiteral("Loading CT viewer from local case cache: %1").arg(caseDir));
    m_ctViewerWidget->loadJobFilesFromCache(caseDir);
}

void MainWindow::loadMostRecentCaseCacheIfAvailable()
{
    if (!m_ctViewerWidget) {
        return;
    }

    JobFileCacheManager cacheManager;
    const QDir casesDir(QDir(cacheManager.baseCacheDir()).filePath(QStringLiteral("cases")));
    const QFileInfoList caseDirs = casesDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time);
    for (const QFileInfo &caseInfo : caseDirs) {
        const QString caseDir = caseInfo.absoluteFilePath();
        const QString inputVolumeDir = QDir(caseDir).filePath(QStringLiteral("input_volume"));
        const bool hasMask = QFileInfo::exists(QDir(caseDir).filePath(QStringLiteral("ai_mask_v0.nrrd")))
            || !QDir(QDir(caseDir).filePath(QStringLiteral("ai_masks"))).entryInfoList({QStringLiteral("*.nrrd")}, QDir::Files).isEmpty();
        const bool hasRawCt = QFileInfo::exists(QDir(inputVolumeDir).filePath(QStringLiteral("ct_volume_int16.raw")))
            && QFileInfo::exists(QDir(inputVolumeDir).filePath(QStringLiteral("ct_volume_metadata.json")));
        const bool hasInputArtifact = hasRawCt
            || QFileInfo::exists(QDir(inputVolumeDir).filePath(QStringLiteral("dicom_series")))
            || !QDir(inputVolumeDir).entryInfoList({QStringLiteral("input_volume_job_*.zip"),
                                                    QStringLiteral("*.nrrd"),
                                                    QStringLiteral("*.nii"),
                                                    QStringLiteral("*.nii.gz"),
                                                    QStringLiteral("*.mhd")},
                                                   QDir::Files).isEmpty();
        if (hasMask && hasInputArtifact) {
            m_currentCaseKey = caseInfo.fileName();
            appendLog(QStringLiteral("Found cached case for viewer startup: %1").arg(caseDir));
            m_ctViewerWidget->loadJobFilesFromCache(caseDir);
            return;
        }
    }
}

bool MainWindow::writeJsonFile(const QString &path, const QJsonObject &object) const
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    return true;
}
