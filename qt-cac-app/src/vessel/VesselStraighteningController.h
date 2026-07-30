#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QString>

class NiftiVesselSelectionState;

class VesselStraighteningController : public QObject
{
    Q_OBJECT

public:
    explicit VesselStraighteningController(QObject *parent = nullptr);

    bool startAnalysis(const NiftiVesselSelectionState &selection,
                       QString *errorMessage = nullptr);
    bool startStraightening(const QString &pathId,
                            QString *errorMessage = nullptr);
    void cancel();
    void reset();

    bool isRunning() const;
    QString outputDirectory() const;
    QJsonArray candidatePaths() const;
    QJsonObject candidateAnalysis() const;
    QJsonObject latestResult() const;

signals:
    void progressChanged(int percent, const QString &message);
    void runningChanged(bool running);
    void candidatesReady(const QJsonArray &candidatePaths);
    void completed(const QJsonObject &result);
    void failed(const QString &message);
    void cancelled();

private:
    enum class Mode {
        None,
        Analyze,
        Straighten
    };

    bool resolveRuntime(QString *errorMessage);
    bool createRunDirectory(QString *errorMessage);
    void startProcess(Mode mode, const QStringList &arguments);
    void consumeStdout();
    void consumeStderr();
    void consumeLines(QByteArray *buffer, bool standardError);
    void handleJsonEvent(const QJsonObject &event);
    void handleFinished(int exitCode, QProcess::ExitStatus status);
    void finishRunningState();
    bool loadCandidates(QString *errorMessage);
    QString failureSummary(int exitCode, QProcess::ExitStatus status) const;

    QProcess m_process;
    Mode m_mode = Mode::None;
    bool m_cancelRequested = false;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    QStringList m_recentStderr;
    QString m_pythonExecutable;
    QString m_scriptPath;
    QString m_ctPath;
    QString m_segmentationPath;
    QString m_outputDirectory;
    int m_label = 0;
    int m_component = 0;
    QJsonArray m_candidatePaths;
    QJsonObject m_candidateAnalysis;
    QJsonObject m_lastProcessResult;
    QJsonObject m_latestResult;
};
