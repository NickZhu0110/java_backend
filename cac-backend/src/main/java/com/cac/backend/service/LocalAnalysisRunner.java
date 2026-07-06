package com.cac.backend.service;

import com.cac.backend.dto.CreateOrUpdateCacResultRequest;
import com.cac.backend.entity.AnalysisJob;
import com.cac.backend.mapper.AnalysisJobMapper;
import com.cac.backend.websocket.JobWebSocketHandler;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Profile;
import org.springframework.stereotype.Service;

import java.io.IOException;
import java.math.BigDecimal;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.LocalDateTime;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;

@Service
@Profile("local")
public class LocalAnalysisRunner {

    private static final Logger log = LoggerFactory.getLogger(LocalAnalysisRunner.class);
    private static final String LOCAL_WORKER_ID = "local-process-runner";

    private final AnalysisJobMapper analysisJobMapper;
    private final JobCacheService jobCacheService;
    private final JobWebSocketHandler jobWebSocketHandler;
    private final CacResultService cacResultService;
    private final ObjectMapper objectMapper;

    @Value("${app.local-analysis.python-exe}")
    private String pythonExe;

    @Value("${app.local-analysis.wrapper-path}")
    private String wrapperPath;

    @Value("${app.local-analysis.segmentcacs-src}")
    private String segmentcacsSrc;

    @Value("${app.local-analysis.model-path}")
    private String modelPath;

    @Value("${app.local-analysis.data-root}")
    private String dataRoot;

    @Value("${app.local-analysis.file-type:dcm}")
    private String fileType;

    @Value("${app.local-analysis.device:cpu}")
    private String device;

    @Value("${app.local-analysis.use-zero-module:false}")
    private boolean useZeroModule;

    public LocalAnalysisRunner(
            AnalysisJobMapper analysisJobMapper,
            JobCacheService jobCacheService,
            JobWebSocketHandler jobWebSocketHandler,
            CacResultService cacResultService,
            ObjectMapper objectMapper
    ) {
        this.analysisJobMapper = analysisJobMapper;
        this.jobCacheService = jobCacheService;
        this.jobWebSocketHandler = jobWebSocketHandler;
        this.cacResultService = cacResultService;
        this.objectMapper = objectMapper;
    }

    public void dispatch(AnalysisJob job) {
        CompletableFuture.runAsync(() -> runJob(job));
    }

    private void runJob(AnalysisJob job) {
        try {
            validateConfig();

            Path inputPath = resolveJobPath(job.getInputPath());
            Path outputDir = resolveJobPath(job.getOutputPath());
            Files.createDirectories(outputDir);

            Path logFile = logsRoot().resolve(job.getId() + ".log");
            Files.createDirectories(logFile.getParent());

            updateStatus(job.getId(), "RUNNING", 5, null);

            Process process = new ProcessBuilder(buildCommand(inputPath, outputDir))
                    .redirectErrorStream(true)
                    .redirectOutput(logFile.toFile())
                    .start();

            int exitCode = process.waitFor();
            if (exitCode != 0) {
                throw new IllegalStateException(
                        "SEGMENT-CACS exited with code " + exitCode
                                + ". See " + normalizePath(logFile)
                );
            }

            Path resultJson = outputDir.resolve("result.json");
            if (!Files.exists(resultJson)) {
                throw new IllegalStateException(
                        "Missing result.json at " + normalizePath(resultJson)
                );
            }

            updateStatus(job.getId(), "RUNNING", 80, null);
            saveResult(job.getId(), outputDir, resultJson);
            updateStatus(job.getId(), "SUCCESS", 100, null);
        } catch (Exception ex) {
            log.error("Local analysis failed for job {}", job.getId(), ex);
            updateStatus(job.getId(), "FAILED", 100, safeMessage(ex));
        }
    }

    private List<String> buildCommand(Path inputPath, Path outputDir) {
        List<String> command = new ArrayList<>();
        command.add(pythonExe);
        command.add(wrapperPath);
        command.add("--segmentcacs-src");
        command.add(segmentcacsSrc);
        command.add("--model");
        command.add(modelPath);
        command.add("--input");
        command.add(normalizePath(inputPath));
        command.add("--output");
        command.add(normalizePath(outputDir));
        command.add("--file-type");
        command.add(fileType);
        command.add("--device");
        command.add(device);

        if (useZeroModule) {
            command.add("--use-zero-module");
        }

        return command;
    }

    private void saveResult(Long jobId, Path outputDir, Path resultJson) throws IOException {
        JsonNode root = objectMapper.readTree(resultJson.toFile());
        CreateOrUpdateCacResultRequest request = new CreateOrUpdateCacResultRequest();

        if (root.has("totalAgatstonScore") && !root.get("totalAgatstonScore").isNull()) {
            request.setAgatstonScore(BigDecimal.valueOf(root.get("totalAgatstonScore").asDouble()));
        }

        request.setRiskGrade(textOrNull(root, "riskGrade"));
        request.setResultJsonPath(normalizePath(resultJson));

        JsonNode files = root.path("files");
        request.setAiMaskPath(resolveOutputFile(outputDir, textOrNull(files, "originalMask")));
        request.setCorrectedMaskPath(resolveOutputFile(outputDir, textOrNull(files, "correctedMask")));
        request.setReportPath(resolveOutputFile(outputDir, textOrNull(files, "runLog")));

        cacResultService.saveOrUpdateResult(jobId, request);
    }

    private void validateConfig() throws IOException {
        requireConfigured(pythonExe, "app.local-analysis.python-exe");
        requireConfigured(wrapperPath, "app.local-analysis.wrapper-path");
        requireConfigured(segmentcacsSrc, "app.local-analysis.segmentcacs-src");
        requireConfigured(modelPath, "app.local-analysis.model-path");
        requireConfigured(dataRoot, "app.local-analysis.data-root");
        requireFile(pythonExe, "python executable");
        requireFile(wrapperPath, "wrapper path");
        requireDirectory(segmentcacsSrc, "segmentcacs src");
        requireFile(modelPath, "model path");
        Files.createDirectories(logsRoot());
    }

    private void requireConfigured(String value, String name) {
        if (value == null || value.isBlank()) {
            throw new IllegalStateException("Missing required local analysis setting: " + name);
        }
    }

    private void requireFile(String value, String name) {
        Path path = Path.of(value);
        if (!Files.isRegularFile(path)) {
            throw new IllegalStateException(name + " does not exist: " + normalizePath(path));
        }
    }

    private void requireDirectory(String value, String name) {
        Path path = Path.of(value);
        if (!Files.isDirectory(path)) {
            throw new IllegalStateException(name + " does not exist: " + normalizePath(path));
        }
    }

    private Path resolveJobPath(String value) {
        Path path = Path.of(value);
        if (path.isAbsolute()) {
            return path.normalize();
        }
        return dataRootPath().resolve(path).normalize();
    }

    private Path dataRootPath() {
        return Path.of(dataRoot);
    }

    private Path logsRoot() {
        return dataRootPath().resolve("logs").resolve("jobs");
    }

    private void updateStatus(Long jobId, String status, Integer progress, String errorMessage) {
        AnalysisJob job = analysisJobMapper.selectById(jobId);
        if (job == null) {
            throw new IllegalArgumentException("Job not found: " + jobId);
        }

        LocalDateTime now = LocalDateTime.now();
        job.setStatus(status);
        job.setProgress(progress);
        job.setWorkerId(LOCAL_WORKER_ID);
        job.setDevice(device);
        job.setErrorMessage(errorMessage);

        if ("RUNNING".equals(status) && job.getStartedAt() == null) {
            job.setStartedAt(now);
        }

        if (isTerminalStatus(status) && job.getFinishedAt() == null) {
            job.setFinishedAt(now);
        }

        job.setUpdatedAt(now);
        analysisJobMapper.updateById(job);
        jobCacheService.saveJobStatus(jobId, status, progress);
        jobWebSocketHandler.broadcastJobStatus(jobId, status, progress);
    }

    private boolean isTerminalStatus(String status) {
        return "SUCCESS".equals(status)
                || "FAILED".equals(status)
                || "CANCELED".equals(status);
    }

    private String resolveOutputFile(Path outputDir, String relativePath) {
        if (relativePath == null || relativePath.isBlank()) {
            return null;
        }
        return normalizePath(outputDir.resolve(relativePath).normalize());
    }

    private String normalizePath(Path path) {
        return path.toString().replace('\\', '/');
    }

    private String textOrNull(JsonNode node, String fieldName) {
        JsonNode child = node.get(fieldName);
        if (child == null || child.isNull()) {
            return null;
        }
        String value = child.asText();
        return value == null || value.isBlank() ? null : value;
    }

    private String safeMessage(Exception ex) {
        String message = ex.getMessage();
        return (message == null || message.isBlank()) ? ex.getClass().getSimpleName() : message;
    }
}
