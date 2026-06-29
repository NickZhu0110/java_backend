#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class BackendClient;
class BackendFileClient;
class CTViewerWidget;
class JobWebSocketClient;
class QJsonObject;
class QLineEdit;

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

private:
    void setupInitialState();
    void connectSignals();
    void appendLog(const QString &message);
    bool validateInputs() const;
    void submitJob();
    void resetForm();
    void setAdvancedOverridesEnabled(bool enabled);
    void setBackendConnected(bool connected, const QString &statusText);
    void updateSubmitButton();
    QString selectedDeviceValue() const;
    void browseDirectory(QLineEdit *lineEdit);
    void browseFile(QLineEdit *lineEdit, const QString &filter);
    void displayResult(const QJsonObject &result);
    void openServerSettings();
    void embedCtViewer();
    void prepareCaseCacheForInput(const QString &inputPath);
    void handleJobResultFiles(const QJsonObject &result);
    void tryLoadCurrentCaseFromCache();
    void loadMostRecentCaseCacheIfAvailable();
    bool writeJsonFile(const QString &path, const QJsonObject &object) const;

    Ui::MainWindow *ui;
    BackendClient *m_backendClient;
    BackendFileClient *m_backendFileClient;
    JobWebSocketClient *m_webSocketClient;
    CTViewerWidget *m_ctViewerWidget;
    qint64 m_currentJobId;
    QString m_currentJobInputPath;
    QString m_currentCaseKey;
    QString m_currentServerResultJsonPath;
    QString m_currentServerAiMaskPath;
    bool m_backendConnected;
};
#endif // MAINWINDOW_H
