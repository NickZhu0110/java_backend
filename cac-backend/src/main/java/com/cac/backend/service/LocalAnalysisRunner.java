package com.cac.backend.service;

import com.cac.backend.dto.CreateJobRequest;
import com.cac.backend.dto.CreateOrUpdateCacResultRequest;
import com.cac.backend.entity.AnalysisJob;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Qualifier;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.stereotype.Service;

import java.io.IOException;
import java.math.BigDecimal;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.stream.Stream;

@Service
@ConditionalOnProperty(name = "cac.execution.mode", havingValue = "local")
public class LocalAnalysisRunner implements AnalysisDispatcher {

    private static final Logger log = LoggerFactory.getLogger(LocalAnalysisRunner.class);
    private static final String WORKER_ID = "local-windows";

    private final JobLifecycleService jobLifecycleService;
    private final CacResultService cacResultService;
    private final ObjectMapper objectMapper;
    private final Executor executor;
    private final Path storageRoot;
    private final String pythonExecutableValue;
    private final String cliPathValue;
    private final String segmentCacsRootValue;
    private final String modelPathValue;
    private final Duration timeout;

    public LocalAnalysisRunner(
            JobLifecycleService jobLifecycleService,
            CacResultService cacResultService,
            ObjectMapper objectMapper,
            @Qualifier("localAnalysisExecutor") Executor executor,
            @Value("${cac.storage.root}") String storageRoot,
            @Value("${cac.local-analysis.python-executable:}") String pythonExecutable,
            @Value("${cac.local-analysis.cli-path:}") String cliPath,
            @Value("${cac.local-analysis.segment-cacs-root:}") String segmentCacsRoot,
            @Value("${cac.local-analysis.model-path:}") String modelPath,
            @Value("${cac.local-analysis.timeout:PT2H}") Duration timeout
    ) {
        this.jobLifecycleService = jobLifecycleService;
        this.cacResultService = cacResultService;
        this.objectMapper = objectMapper;
        this.executor = executor;
        this.storageRoot = Paths.get(storageRoot).toAbsolutePath().normalize();
        this.pythonExecutableValue = pythonExecutable;
        this.cliPathValue = cliPath;
        this.segmentCacsRootValue = segmentCacsRoot;
        this.modelPathValue = modelPath;
        this.timeout = timeout;
    }

    @Override
    public void dispatch(AnalysisJob job, CreateJobRequest request) {
        try {
            executor.execute(() -> executeJob(job.getId(), request));
        } catch (RejectedExecutionException exception) {
            failJob(job.getId(), "Local analysis queue is full.");
        }
    }

    private void executeJob(Long jobId, CreateJobRequest request) {
        Path jobRoot = storageRoot.resolve("jobs").resolve(String.valueOf(jobId)).normalize();
        Path outputRoot = jobRoot.resolve("output").normalize();
        Path processLog = jobRoot.resolve("process.log").normalize();
        Instant startedAt = Instant.now();
        Process process = null;

        try {
            update(jobId, "VALIDATING_INPUT", 5, null);
            Path inputSeries = validateInputSeries(request.getInputPath());
            validateLocalRequest(request);
            RuntimePaths runtimePaths = validateRuntimePaths();
            Path userExportDirectory =
                    prepareUserExportDirectory(inputSeries, request.getOutputPath(), jobId);

            Files.createDirectories(outputRoot);
            writeInputMetadata(jobRoot, inputSeries);

            update(jobId, "PREPARING_INPUT", 15, null);
            List<String> command = buildCommand(runtimePaths, inputSeries, outputRoot);
            writeJobMetadata(jobRoot, startedAt, null, null, "STARTING");

            ProcessBuilder processBuilder = new ProcessBuilder(command);
            processBuilder.directory(jobRoot.toFile());
            processBuilder.redirectErrorStream(true);
            processBuilder.redirectOutput(processLog.toFile());
            Map<String, String> environment = processBuilder.environment();
            environment.put("PYTHONUTF8", "1");
            environment.put("PYTHONIOENCODING", "utf-8");
            environment.put("PYTHONDONTWRITEBYTECODE", "1");
            environment.put("CUDA_VISIBLE_DEVICES", "");

            update(jobId, "RUNNING_INFERENCE", 25, null);
            process = processBuilder.start();
            boolean finished = process.waitFor(timeout.toMillis(), TimeUnit.MILLISECONDS);
            if (!finished) {
                terminateJobProcess(process);
                throw new IllegalStateException(
                        "Inference timed out after " + timeout.toMinutes() + " minutes.");
            }

            int exitCode = process.exitValue();
            writeJobMetadata(jobRoot, startedAt, Instant.now(), exitCode, "PROCESS_FINISHED");
            if (exitCode != 0) {
                throw new IllegalStateException(
                        "Inference process failed with exit code " + exitCode
                                + ". See the job process log.");
            }

            update(jobId, "VALIDATING_OUTPUT", 75, null);
            ValidatedResult validatedResult = validateOutput(outputRoot);
            exportInferenceArtifacts(validatedResult, userExportDirectory);

            update(jobId, "LOADING_RESULT", 90, null);
            persistResult(jobId, validatedResult);
            update(jobId, "COMPLETED", 100, null);
            log.info("Local analysis completed: jobId={}, durationSeconds={}",
                    jobId, Duration.between(startedAt, Instant.now()).toSeconds());
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            if (process != null && process.isAlive()) {
                terminateJobProcess(process);
            }
            failJob(jobId, "Local analysis was interrupted.");
        } catch (Exception exception) {
            writeFailureMetadata(jobRoot, startedAt, exception);
            failJob(jobId, safeError(exception));
            log.warn("Local analysis failed: jobId={}, reason={}", jobId, safeError(exception));
        }
    }

    private Path validateInputSeries(String inputPathValue) throws IOException {
        if (inputPathValue == null || inputPathValue.isBlank()) {
            throw new IllegalArgumentException("A DICOM series directory is required.");
        }
        Path inputSeries = Paths.get(inputPathValue).toAbsolutePath().normalize();
        if (!Files.isDirectory(inputSeries) || !Files.isReadable(inputSeries)) {
            throw new IllegalArgumentException("The selected DICOM series directory is not readable.");
        }
        try (Stream<Path> entries = Files.list(inputSeries)) {
            if (entries.noneMatch(path -> Files.isRegularFile(path) && Files.isReadable(path))) {
                throw new IllegalArgumentException(
                        "The selected directory does not contain readable files.");
            }
        }
        return inputSeries;
    }

    private void validateLocalRequest(CreateJobRequest request) {
        String fileType = request.getFileType();
        if (fileType != null && !fileType.isBlank() && !"dcm".equalsIgnoreCase(fileType)) {
            throw new IllegalArgumentException("Local Windows analysis accepts DICOM series only.");
        }
        String device = request.getDevice();
        if (device != null && !device.isBlank() && !"cpu".equalsIgnoreCase(device)) {
            throw new IllegalArgumentException("Local Windows analysis is CPU-only.");
        }
    }

    private Path prepareUserExportDirectory(
            Path inputSeries,
            String outputPathValue,
            Long jobId
    ) throws IOException {
        if (outputPathValue == null || outputPathValue.isBlank()) {
            throw new IllegalArgumentException("Select an output directory for exported results.");
        }
        Path configured = Paths.get(outputPathValue);
        if (!configured.isAbsolute()) {
            throw new IllegalArgumentException("The output directory must be an absolute path.");
        }
        Path outputBase = configured.normalize();
        if (outputBase.equals(inputSeries) || outputBase.startsWith(inputSeries)) {
            throw new IllegalArgumentException(
                    "The output directory must not be inside the source DICOM directory.");
        }
        Files.createDirectories(outputBase);
        if (!Files.isDirectory(outputBase) || !Files.isWritable(outputBase)) {
            throw new IllegalArgumentException("The selected output directory is not writable.");
        }
        Path jobExportDirectory =
                outputBase.resolve("cac_job_" + jobId).toAbsolutePath().normalize();
        if (!jobExportDirectory.startsWith(outputBase)) {
            throw new IllegalArgumentException("The job export directory is invalid.");
        }
        Files.createDirectories(jobExportDirectory);
        return jobExportDirectory;
    }

    private void exportInferenceArtifacts(
            ValidatedResult result,
            Path userExportDirectory
    ) throws IOException {
        Files.copy(
                result.ctPath(),
                userExportDirectory.resolve("ct.nrrd"),
                StandardCopyOption.REPLACE_EXISTING
        );
        Files.copy(
                result.maskPath(),
                userExportDirectory.resolve("ai_mask.nrrd"),
                StandardCopyOption.REPLACE_EXISTING
        );
        Files.copy(
                result.resultPath(),
                userExportDirectory.resolve("result.json"),
                StandardCopyOption.REPLACE_EXISTING
        );
    }

    private RuntimePaths validateRuntimePaths() {
        Path pythonExecutable = requiredAbsoluteFile(
                pythonExecutableValue, "CAC_PYTHON_EXECUTABLE");
        Path cliPath = requiredAbsoluteFile(cliPathValue, "CAC_LOCAL_CLI_PATH");
        Path segmentCacsRoot = requiredAbsoluteDirectory(
                segmentCacsRootValue, "SEGMENT_CACS_ROOT");
        Path modelPath = requiredAbsoluteFile(modelPathValue, "SEGMENT_CACS_MODEL_PATH");
        return new RuntimePaths(pythonExecutable, cliPath, segmentCacsRoot, modelPath);
    }

    private Path requiredAbsoluteFile(String value, String propertyName) {
        Path path = requiredAbsolutePath(value, propertyName);
        if (!Files.isRegularFile(path) || !Files.isReadable(path)) {
            throw new IllegalStateException(propertyName + " does not reference a readable file.");
        }
        return path;
    }

    private Path requiredAbsoluteDirectory(String value, String propertyName) {
        Path path = requiredAbsolutePath(value, propertyName);
        if (!Files.isDirectory(path) || !Files.isReadable(path)) {
            throw new IllegalStateException(
                    propertyName + " does not reference a readable directory.");
        }
        return path;
    }

    private Path requiredAbsolutePath(String value, String propertyName) {
        if (value == null || value.isBlank()) {
            throw new IllegalStateException(propertyName + " is not configured.");
        }
        Path path = Paths.get(value).toAbsolutePath().normalize();
        if (!path.isAbsolute()) {
            throw new IllegalStateException(propertyName + " must be an absolute path.");
        }
        return path;
    }

    private List<String> buildCommand(
            RuntimePaths runtimePaths,
            Path inputSeries,
            Path outputRoot
    ) {
        List<String> command = new ArrayList<>();
        command.add(runtimePaths.pythonExecutable().toString());
        command.add(runtimePaths.cliPath().toString());
        command.add("--input-series");
        command.add(inputSeries.toString());
        command.add("--output-dir");
        command.add(outputRoot.toString());
        command.add("--segmentcacs-root");
        command.add(runtimePaths.segmentCacsRoot().toString());
        command.add("--model");
        command.add(runtimePaths.modelPath().toString());
        command.add("--device");
        command.add("cpu");
        return command;
    }

    private ValidatedResult validateOutput(Path outputRoot) throws IOException {
        Path resultPath = outputRoot.resolve("result.json").normalize();
        requireInside(outputRoot, resultPath);
        if (!Files.isRegularFile(resultPath) || Files.size(resultPath) == 0) {
            throw new IllegalStateException("Inference did not produce a readable result.json.");
        }

        JsonNode result = objectMapper.readTree(resultPath.toFile());
        JsonNode files = result.path("files");
        Path ctPath = resolveOutputArtifact(outputRoot, files.path("ctVolume").asText());
        Path maskPath = resolveOutputArtifact(outputRoot, files.path("originalMask").asText());
        requireReadableArtifact(ctPath, "CT volume");
        requireReadableArtifact(maskPath, "AI mask");

        JsonNode validation = result.path("validation");
        if (!validation.path("ctReadable").asBoolean(false)
                || !validation.path("maskReadable").asBoolean(false)
                || !validation.path("dimensionsMatch").asBoolean(false)
                || !validation.path("geometryCompatible").asBoolean(false)) {
            throw new IllegalStateException(
                    "Inference output geometry or buffers failed validation.");
        }
        if (!result.path("totalAgatstonScore").isNumber()) {
            throw new IllegalStateException("result.json is missing totalAgatstonScore.");
        }
        return new ValidatedResult(resultPath, ctPath, maskPath, result);
    }

    private Path resolveOutputArtifact(Path outputRoot, String value) {
        if (value == null || value.isBlank()) {
            throw new IllegalStateException("result.json is missing a required artifact path.");
        }
        Path candidate = Paths.get(value);
        if (!candidate.isAbsolute()) {
            candidate = outputRoot.resolve(candidate);
        }
        candidate = candidate.toAbsolutePath().normalize();
        requireInside(outputRoot, candidate);
        return candidate;
    }

    private void requireReadableArtifact(Path path, String label) throws IOException {
        if (!Files.isRegularFile(path) || !Files.isReadable(path) || Files.size(path) == 0) {
            throw new IllegalStateException(label + " is missing or unreadable.");
        }
    }

    private void requireInside(Path root, Path candidate) {
        Path normalizedRoot = root.toAbsolutePath().normalize();
        Path normalizedCandidate = candidate.toAbsolutePath().normalize();
        if (!normalizedCandidate.startsWith(normalizedRoot)) {
            throw new IllegalStateException("An output artifact escaped the managed job directory.");
        }
    }

    private void persistResult(Long jobId, ValidatedResult validated) {
        CreateOrUpdateCacResultRequest request = new CreateOrUpdateCacResultRequest();
        request.setAgatstonScore(
                new BigDecimal(validated.json().path("totalAgatstonScore").asText()));
        request.setRiskGrade(validated.json().path("riskGrade").asText("unknown"));
        request.setResultJsonPath(validated.resultPath().toString());
        request.setCtVolumePath(validated.ctPath().toString());
        request.setAiMaskPath(validated.maskPath().toString());
        request.setCorrectedMaskPath(null);
        request.setReportPath(null);
        cacResultService.saveOrUpdateResult(jobId, request);
    }

    private void writeInputMetadata(Path jobRoot, Path inputSeries) throws IOException {
        long readableFileCount;
        try (Stream<Path> entries = Files.list(inputSeries)) {
            readableFileCount = entries.filter(Files::isRegularFile).count();
        }
        Map<String, Object> metadata = new LinkedHashMap<>();
        metadata.put("inputType", "DICOM_SERIES_DIRECTORY");
        metadata.put("readableFileCount", readableFileCount);
        metadata.put("sourceReferencedReadOnly", true);
        Files.createDirectories(jobRoot);
        objectMapper.writerWithDefaultPrettyPrinter()
                .writeValue(jobRoot.resolve("input.json").toFile(), metadata);
    }

    private void writeJobMetadata(
            Path jobRoot,
            Instant startedAt,
            Instant finishedAt,
            Integer exitCode,
            String state
    ) throws IOException {
        Map<String, Object> metadata = new LinkedHashMap<>();
        metadata.put("state", state);
        metadata.put("startedAt", startedAt.toString());
        if (finishedAt != null) {
            metadata.put("finishedAt", finishedAt.toString());
            metadata.put("durationSeconds", Duration.between(startedAt, finishedAt).toSeconds());
        }
        if (exitCode != null) {
            metadata.put("exitCode", exitCode);
        }
        metadata.put("device", "cpu");
        Files.createDirectories(jobRoot);
        objectMapper.writerWithDefaultPrettyPrinter()
                .writeValue(jobRoot.resolve("job_metadata.json").toFile(), metadata);
    }

    private void writeFailureMetadata(Path jobRoot, Instant startedAt, Exception exception) {
        try {
            Map<String, Object> metadata = new LinkedHashMap<>();
            metadata.put("state", "FAILED");
            metadata.put("startedAt", startedAt.toString());
            metadata.put("finishedAt", Instant.now().toString());
            metadata.put("error", safeError(exception));
            Files.createDirectories(jobRoot);
            objectMapper.writerWithDefaultPrettyPrinter()
                    .writeValue(jobRoot.resolve("job_metadata.json").toFile(), metadata);
        } catch (IOException metadataException) {
            log.warn("Could not write failure metadata: jobIdPath={}", jobRoot.getFileName());
        }
    }

    private void update(Long jobId, String stage, int progress, String errorMessage) {
        jobLifecycleService.updateJobStatus(
                jobId, stage, progress, errorMessage, WORKER_ID, "cpu");
    }

    private void failJob(Long jobId, String errorMessage) {
        jobLifecycleService.updateJobStatus(
                jobId, "FAILED", 0, errorMessage, WORKER_ID, "cpu");
    }

    private void terminateJobProcess(Process process) {
        process.descendants().forEach(handle -> {
            if (handle.isAlive()) {
                handle.destroy();
            }
        });
        process.destroy();
        try {
            if (!process.waitFor(5, TimeUnit.SECONDS)) {
                process.descendants().forEach(handle -> {
                    if (handle.isAlive()) {
                        handle.destroyForcibly();
                    }
                });
                process.destroyForcibly();
            }
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            process.destroyForcibly();
        }
    }

    private String safeError(Exception exception) {
        String message = exception.getMessage();
        if (message == null || message.isBlank()) {
            message = exception.getClass().getSimpleName();
        }
        return message.length() > 500 ? message.substring(0, 500) : message;
    }

    private record RuntimePaths(
            Path pythonExecutable,
            Path cliPath,
            Path segmentCacsRoot,
            Path modelPath
    ) {
    }

    private record ValidatedResult(
            Path resultPath,
            Path ctPath,
            Path maskPath,
            JsonNode json
    ) {
    }
}
