#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

class BackendClient;
class BackendFileClient;
class CTViewerWidget;
class JobWebSocketClient;
class QCloseEvent;
class QJsonObject;
class QLineEdit;
class QTimer;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupInitialState();
    void connectSignals();
    void appendLog(const QString &message);
    bool validateInputs() const;
    void submitJob();
    void resetForm();
    void setAdvancedOverridesEnabled(bool enabled);
    void setBackendConnected(bool connected, const QString &statusText);
    void setJobInProgress(bool inProgress);
    void setNiftiReviewModeActive(bool active);
    void handleJobStatusUpdate(qint64 jobId,
                               const QString &status,
                               int progress,
                               const QString &errorMessage = QString());
    void updateSubmitButton();
    QString selectedDeviceValue() const;
    void browseDirectory(QLineEdit *lineEdit);
    void browseFile(QLineEdit *lineEdit, const QString &filter);
    void displayResult(const QJsonObject &result);
    void updateResultLabels(const QJsonObject &result);
    void openServerSettings();
    void setupCtViewer();
    void prepareCaseCacheForInput(const QString &inputPath);
    void handleJobResultFiles(const QJsonObject &result);
    void tryLoadCurrentCaseFromCache();
    void loadMostRecentCaseCacheIfAvailable();
    bool writeJsonFile(const QString &path, const QJsonObject &object) const;

    Ui::MainWindow *ui;
    BackendClient *m_backendClient;
    BackendFileClient *m_backendFileClient;
    JobWebSocketClient *m_webSocketClient;
    QTimer *m_jobPollTimer;
    CTViewerWidget *m_ctViewerWidget;
    qint64 m_currentJobId;
    QString m_currentJobInputPath;
    QString m_currentCaseKey;
    QString m_currentServerResultJsonPath;
    QString m_currentServerAiMaskPath;
    QString m_pendingCorrectedMaskMetadataPath;
    int m_pendingCorrectedMaskVersion;
    bool m_backendConnected;
    bool m_jobInProgress;
    bool m_niftiReviewActive;
    bool m_caseCacheLoadDeferredDuringNiftiReview;
    bool m_resultFetchRequested;
    QString m_lastJobStatus;
    int m_lastJobProgress;
    qint64 m_scoreRecalculationDeferredJobId;
};
#endif // MAINWINDOW_H
