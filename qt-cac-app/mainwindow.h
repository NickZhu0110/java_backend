#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

class BackendClient;
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

    Ui::MainWindow *ui;
    BackendClient *m_backendClient;
    JobWebSocketClient *m_webSocketClient;
    qint64 m_currentJobId;
    bool m_backendConnected;
};
#endif // MAINWINDOW_H
