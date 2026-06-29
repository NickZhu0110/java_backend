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
public class LocalDirectAnalysisService {

    private static final Logger log = LoggerFactory.getLogger(LocalDirectAnalysisService.class);
    private static final String LOCAL_WORKER_ID = "local-direct";

    private final AnalysisJobMapper analysisJobMapper;
    private final JobCacheService jobCacheService;
    private final JobWebSocketHandler jobWebSocketHandler;
    private final CacResultService cacResultService;
    private final ObjectMapper objectMapper;

    @Value("${app.analysis.mode:kafka}")
    private String analysisMode;

    @Value("${app.analysis.local-direct.python-exe:}")
    private String pythonExe;

    @Value("${app.analysis.local-direct.wrapper-path:}")
    private String wrapperPath;

    @Value("${app.analysis.local-direct.segmentcacs-src:}")
    private String segmentcacsSrc;

    @Value("${app.analysis.local-direct.model-path:}")
    private String modelPath;

    @Value("${app.analysis.local-direct.file-type:dcm}")
    private String fileType;

    @Value("${app.analysis.local-direct.device:cpu}")
    private String device;

    @Value("${app.analysis.local-direct.use-zero-module:false}")
    private boolean useZeroModule;

    public LocalDirectAnalysisService(
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

    public boolean isEnabled() {
        return "local_direct".equalsIgnoreCase(analysisMode);
    }

    public void dispatch(AnalysisJob job) {
        CompletableFuture.runAsync(() -> runJob(job));
    }

    private void runJob(AnalysisJob job) {
        try {
            validateConfig();

            Path outputDir = Path.of(job.getOutputPath());
            Files.createDirectories(outputDir);
            updateStatus(job.getId(), "RUNNING", 5, null);

            Path backendLog = outputDir.resolve("backend-direct.log");
            Process process = new ProcessBuilder(buildCommand(job))
                    .redirectErrorStream(true)
                    .redirectOutput(backendLog.toFile())
                    .start();

            int exitCode = process.waitFor();
            if (exitCode != 0) {
                throw new IllegalStateException(
                        "SEGMENT-CACS exited with code " + exitCode
                                + ". See " + normalizePath(backendLog)
                );
            }

            Path resultJson = outputDir.resolve("result.json");
            if (!Files.exists(resultJson)) {
                throw new IllegalStateException(
                        "Missing result.json at " + normalizePath(resultJson)
                );
            }

            saveResult(job.getId(), outputDir, resultJson);
            updateStatus(job.getId(), "SUCCESS", 100, null);
        } catch (Exception ex) {
            log.error("Local direct analysis failed for job {}", job.getId(), ex);
            updateStatus(job.getId(), "FAILED", 100, safeMessage(ex));
        }
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

    private List<String> buildCommand(AnalysisJob job) {
        List<String> command = new ArrayList<>();
        command.add(pythonExe);
        command.add(wrapperPath);
        command.add("--segmentcacs-src");
        command.add(segmentcacsSrc);
        command.add("--model");
        command.add(modelPath);
        command.add("--input");
        command.add(job.getInputPath());
        command.add("--output");
        command.add(job.getOutputPath());
        command.add("--file-type");
        command.add(fileType);
        command.add("--device");
        command.add(device);

        if (useZeroModule) {
            command.add("--use-zero-module");
        }

        return command;
    }

    private void validateConfig() {
        requireConfigured(pythonExe, "SEGMENTCACS_PYTHON_EXE");
        requireConfigured(wrapperPath, "SEGMENTCACS_WRAPPER");
        requireConfigured(segmentcacsSrc, "SEGMENTCACS_SRC");
        requireConfigured(modelPath, "MODEL_PATH");
    }

    private void requireConfigured(String value, String name) {
        if (value == null || value.isBlank()) {
            throw new IllegalStateException("Missing required local direct setting: " + name);
        }
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
