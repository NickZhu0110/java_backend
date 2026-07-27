#include "vessel/VesselStraighteningController.h"

#include "viewer/NiftiVesselSelectionState.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

namespace {

QString configuredPath(const char *name)
{
    return QString::fromLocal8Bit(qgetenv(name)).trimmed();
}

QString localApplicationData()
{
    const QString configured =
        QString::fromLocal8Bit(qgetenv("LOCALAPPDATA")).trimmed();
    return configured.isEmpty()
        ? QStandardPaths::writableLocation(
              QStandardPaths::GenericDataLocation)
        : configured;
}

QString findScriptFrom(const QString &startingDirectory)
{
    QDir directory(startingDirectory);
    for (int depth = 0; depth < 8; ++depth) {
        const QString candidate = directory.absoluteFilePath(
            QStringLiteral(
                "python-worker/vessel_straightening/vmtk_straighten_vessel.py"));
        if (QFileInfo::exists(candidate)) {
            return QFileInfo(candidate).absoluteFilePath();
        }
        if (!directory.cdUp()) {
            break;
        }
    }
    return {};
}

} // namespace

VesselStraighteningController::VesselStraighteningController(QObject *parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &VesselStraighteningController::consumeStdout);
    connect(&m_process, &QProcess::readyReadStandardError,
            this, &VesselStraighteningController::consumeStderr);
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            &VesselStraighteningController::handleFinished);
    connect(&m_process, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            const QString message = QStringLiteral(
                "Could not start the isolated VMTK process: %1")
                                        .arg(m_process.errorString());
            finishRunningState();
            emit failed(message);
        }
    });
}

bool VesselStraighteningController::startAnalysis(
    const NiftiVesselSelectionState &selection,
    QString *errorMessage)
{
    if (isRunning()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Another vessel-processing job is already running.");
        }
        return false;
    }
    if (!selection.selectionUsableForProcessing()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Select a valid vessel label and connected component first.");
        }
        return false;
    }
    const MultiStructureVolume *volume =
        selection.currentMultiStructureVolume();
    if (!volume || !QFileInfo::exists(volume->ctPath)
        || !QFileInfo::exists(volume->segmentationPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "The current NIfTI source paths are unavailable.");
        }
        return false;
    }
    if (!resolveRuntime(errorMessage)) {
        return false;
    }
    m_ctPath = QFileInfo(volume->ctPath).absoluteFilePath();
    m_segmentationPath =
        QFileInfo(volume->segmentationPath).absoluteFilePath();
    m_label = selection.selectedVesselLabel();
    m_component = selection.selectedComponent();
    m_candidatePaths = {};
    m_latestResult = {};
    if (!createRunDirectory(errorMessage)) {
        return false;
    }

    const QStringList arguments = {
        m_scriptPath,
        QStringLiteral("--mode"), QStringLiteral("analyze"),
        QStringLiteral("--ct"), m_ctPath,
        QStringLiteral("--segmentation"), m_segmentationPath,
        QStringLiteral("--label"), QString::number(m_label),
        QStringLiteral("--component"), QString::number(m_component),
        QStringLiteral("--output-dir"), m_outputDirectory
    };
    startProcess(Mode::Analyze, arguments);
    return true;
}

bool VesselStraighteningController::startStraightening(
    const QString &pathId,
    QString *errorMessage)
{
    if (isRunning()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Another vessel-processing job is already running.");
        }
        return false;
    }
    bool knownPath = false;
    for (const QJsonValue &value : m_candidatePaths) {
        if (value.toObject().value(QStringLiteral("path_id")).toString()
            == pathId) {
            knownPath = true;
            break;
        }
    }
    if (!knownPath || m_outputDirectory.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Select a valid centerline path from the current analysis.");
        }
        return false;
    }
    const QStringList arguments = {
        m_scriptPath,
        QStringLiteral("--mode"), QStringLiteral("straighten"),
        QStringLiteral("--ct"), m_ctPath,
        QStringLiteral("--segmentation"), m_segmentationPath,
        QStringLiteral("--label"), QString::number(m_label),
        QStringLiteral("--component"), QString::number(m_component),
        QStringLiteral("--path-id"), pathId,
        QStringLiteral("--output-dir"), m_outputDirectory,
        QStringLiteral("--cross-section-size-mm"), QStringLiteral("32"),
        QStringLiteral("--cross-section-spacing-mm"), QStringLiteral("0.5"),
        QStringLiteral("--longitudinal-spacing-mm"), QStringLiteral("0.5")
    };
    startProcess(Mode::Straighten, arguments);
    return true;
}

void VesselStraighteningController::cancel()
{
    if (!isRunning()) {
        return;
    }
    m_cancelRequested = true;
    m_process.terminate();
    QTimer::singleShot(3000, this, [this]() {
        if (m_cancelRequested && isRunning()) {
            m_process.kill();
        }
    });
}

void VesselStraighteningController::reset()
{
    if (isRunning()) {
        cancel();
    }
    m_candidatePaths = {};
    m_latestResult = {};
    m_lastProcessResult = {};
    m_ctPath.clear();
    m_segmentationPath.clear();
    m_outputDirectory.clear();
    m_label = 0;
    m_component = 0;
}

bool VesselStraighteningController::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

QString VesselStraighteningController::outputDirectory() const
{
    return m_outputDirectory;
}

QJsonArray VesselStraighteningController::candidatePaths() const
{
    return m_candidatePaths;
}

QJsonObject VesselStraighteningController::latestResult() const
{
    return m_latestResult;
}

bool VesselStraighteningController::resolveRuntime(QString *errorMessage)
{
    m_pythonExecutable =
        configuredPath("CAC_VMTK_PYTHON_EXECUTABLE");
    if (m_pythonExecutable.isEmpty()) {
        m_pythonExecutable = QDir(localApplicationData()).absoluteFilePath(
            QStringLiteral("CAC/vmtk-python/python.exe"));
    }
    if (!QFileInfo(m_pythonExecutable).isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "VMTK Python was not found. Set CAC_VMTK_PYTHON_EXECUTABLE "
                "to the isolated python.exe path. Resolved path: %1")
                                .arg(m_pythonExecutable);
        }
        return false;
    }

    m_scriptPath =
        configuredPath("CAC_VESSEL_STRAIGHTENING_SCRIPT");
    if (m_scriptPath.isEmpty()) {
        m_scriptPath = findScriptFrom(QDir::currentPath());
    }
    if (m_scriptPath.isEmpty()) {
        m_scriptPath = findScriptFrom(
            QCoreApplication::applicationDirPath());
    }
    if (!QFileInfo(m_scriptPath).isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "The vessel-processing CLI was not found. Set "
                "CAC_VESSEL_STRAIGHTENING_SCRIPT to its absolute path.");
        }
        return false;
    }
    return true;
}

bool VesselStraighteningController::createRunDirectory(
    QString *errorMessage)
{
    QString dataRoot = configuredPath("CAC_DATA_ROOT");
    if (dataRoot.isEmpty()) {
        dataRoot = QDir(localApplicationData())
                       .absoluteFilePath(QStringLiteral("CAC/data"));
    }
    const QByteArray datasetDigest =
        QCryptographicHash::hash(
            (QDir::cleanPath(m_ctPath).toCaseFolded()
             + QLatin1Char('\n')
             + QDir::cleanPath(m_segmentationPath).toCaseFolded())
                .toUtf8(),
            QCryptographicHash::Sha256)
            .toHex()
            .left(16);
    const QString datasetKey =
        QStringLiteral("dataset-%1").arg(
            QString::fromLatin1(datasetDigest));
    const QString runId =
        QDateTime::currentDateTimeUtc().toString(
            QStringLiteral("yyyyMMdd-HHmmss-zzz"))
        + QLatin1Char('-')
        + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    QDir root(dataRoot);
    m_outputDirectory = root.absoluteFilePath(
        QStringLiteral("vessel-straightening/%1/%2")
            .arg(datasetKey, runId));
    if (!QDir().mkpath(m_outputDirectory)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Could not create the vessel output directory: %1")
                                .arg(m_outputDirectory);
        }
        m_outputDirectory.clear();
        return false;
    }
    return true;
}

void VesselStraighteningController::startProcess(
    Mode mode,
    const QStringList &arguments)
{
    m_mode = mode;
    m_cancelRequested = false;
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_recentStderr.clear();
    m_lastProcessResult = {};
    QProcessEnvironment environment =
        QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUTF8"),
                       QStringLiteral("1"));
    environment.insert(QStringLiteral("PYTHONIOENCODING"),
                       QStringLiteral("utf-8"));
    m_process.setProcessEnvironment(environment);
    m_process.setProgram(m_pythonExecutable);
    m_process.setArguments(arguments);
    m_process.setWorkingDirectory(
        QFileInfo(m_scriptPath).absolutePath());
    emit runningChanged(true);
    emit progressChanged(0, mode == Mode::Analyze
        ? QStringLiteral("Starting VMTK centerline analysis...")
        : QStringLiteral("Starting VMTK Curved MPR..."));
    m_process.start();
}

void VesselStraighteningController::consumeStdout()
{
    m_stdoutBuffer += m_process.readAllStandardOutput();
    consumeLines(&m_stdoutBuffer, false);
}

void VesselStraighteningController::consumeStderr()
{
    m_stderrBuffer += m_process.readAllStandardError();
    consumeLines(&m_stderrBuffer, true);
}

void VesselStraighteningController::consumeLines(
    QByteArray *buffer,
    bool standardError)
{
    for (;;) {
        const int lineEnd = buffer->indexOf('\n');
        if (lineEnd < 0) {
            break;
        }
        const QByteArray rawLine = buffer->left(lineEnd).trimmed();
        buffer->remove(0, lineEnd + 1);
        if (rawLine.isEmpty()) {
            continue;
        }
        QJsonParseError parseError;
        const QJsonDocument document =
            QJsonDocument::fromJson(rawLine, &parseError);
        if (parseError.error == QJsonParseError::NoError
            && document.isObject()) {
            handleJsonEvent(document.object());
            continue;
        }
        if (standardError) {
            m_recentStderr.push_back(
                QString::fromUtf8(rawLine));
            while (m_recentStderr.size() > 12) {
                m_recentStderr.removeFirst();
            }
        }
    }
}

void VesselStraighteningController::handleJsonEvent(
    const QJsonObject &event)
{
    const QString eventName =
        event.value(QStringLiteral("event")).toString();
    if (eventName == QStringLiteral("progress")) {
        emit progressChanged(
            event.value(QStringLiteral("percent")).toInt(),
            event.value(QStringLiteral("message")).toString());
    } else if (eventName == QStringLiteral("result")) {
        m_lastProcessResult = event;
    } else if (eventName == QStringLiteral("error")) {
        const QString message =
            event.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            m_recentStderr.push_back(message);
        }
    }
}

void VesselStraighteningController::handleFinished(
    int exitCode,
    QProcess::ExitStatus status)
{
    consumeStdout();
    consumeStderr();
    if (!m_stdoutBuffer.trimmed().isEmpty()) {
        m_stdoutBuffer += '\n';
        consumeLines(&m_stdoutBuffer, false);
    }
    if (!m_stderrBuffer.trimmed().isEmpty()) {
        m_stderrBuffer += '\n';
        consumeLines(&m_stderrBuffer, true);
    }
    const Mode completedMode = m_mode;
    const bool cancelledByUser = m_cancelRequested;
    finishRunningState();
    if (cancelledByUser) {
        emit cancelled();
        return;
    }
    if (status != QProcess::NormalExit || exitCode != 0) {
        emit failed(failureSummary(exitCode, status));
        return;
    }
    if (completedMode == Mode::Analyze) {
        QString error;
        if (!loadCandidates(&error)) {
            emit failed(error);
            return;
        }
        emit candidatesReady(m_candidatePaths);
        return;
    }
    if (completedMode == Mode::Straighten) {
        if (m_lastProcessResult.isEmpty()) {
            emit failed(QStringLiteral(
                "The vessel process exited successfully but returned no result."));
            return;
        }
        m_latestResult = m_lastProcessResult;
        emit completed(m_latestResult);
    }
}

void VesselStraighteningController::finishRunningState()
{
    m_mode = Mode::None;
    m_cancelRequested = false;
    emit runningChanged(false);
}

bool VesselStraighteningController::loadCandidates(
    QString *errorMessage)
{
    QFile file(QDir(m_outputDirectory).absoluteFilePath(
        QStringLiteral("centerline_candidates.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "Could not read VMTK centerline candidates: %1")
                                .arg(file.errorString());
        }
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document =
        QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !document.isObject()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "VMTK centerline candidate JSON is invalid: %1")
                                .arg(parseError.errorString());
        }
        return false;
    }
    m_candidatePaths =
        document.object()
            .value(QStringLiteral("candidate_paths"))
            .toArray();
    if (m_candidatePaths.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "VMTK analysis produced no selectable paths.");
        }
        return false;
    }
    return true;
}

QString VesselStraighteningController::failureSummary(
    int exitCode,
    QProcess::ExitStatus status) const
{
    QString message = QStringLiteral(
        "VMTK vessel processing failed (exit code %1, %2).")
                          .arg(exitCode)
                          .arg(status == QProcess::CrashExit
                                   ? QStringLiteral("process crashed")
                                   : QStringLiteral("normal exit"));
    if (!m_recentStderr.isEmpty()) {
        message += QStringLiteral("\n\n%1")
                       .arg(m_recentStderr.join(QLatin1Char('\n')));
    }
    message += QStringLiteral("\n\nProcess log: %1")
                   .arg(QDir(m_outputDirectory).absoluteFilePath(
                       QStringLiteral("process.log")));
    return message;
}
