package com.cac.backend.service;

import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.cac.backend.dto.CacResultResponse;
import com.cac.backend.dto.CorrectedMaskUploadResponse;
import com.cac.backend.dto.CreateOrUpdateCacResultRequest;
import com.cac.backend.entity.AnalysisJob;
import com.cac.backend.entity.CacResult;
import com.cac.backend.entity.CorrectedMaskVersion;
import com.cac.backend.mapper.AnalysisJobMapper;
import com.cac.backend.mapper.CacResultMapper;
import com.cac.backend.mapper.CorrectedMaskVersionMapper;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.beans.factory.annotation.Value;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.core.io.FileSystemResource;
import org.springframework.core.io.Resource;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.web.multipart.MultipartFile;
import org.springframework.web.server.ResponseStatusException;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.math.BigDecimal;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.security.DigestInputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.LocalDateTime;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;

@Service
public class CacResultService {

    private static final Logger log = LoggerFactory.getLogger(CacResultService.class);
    private static final long RECALCULATE_TIMEOUT_SECONDS = 180;

    private final AnalysisJobMapper analysisJobMapper;
    private final CacResultMapper cacResultMapper;
    private final CorrectedMaskVersionMapper correctedMaskVersionMapper;
    private final ObjectMapper objectMapper;

    @Value("${cac.storage.root:storage}")
    private String storageRoot;

    @Value("${cac.recalculate.python-executable:}")
    private String recalculatePythonExecutable;

    @Value("${cac.recalculate.script-path:../python-worker/recalculate_agatston.py}")
    private String recalculateScriptPath;

    @Value("${cac.recalculate.segmentcacs-src:}")
    private String recalculateSegmentCacsSrc;

    public CacResultService(
            AnalysisJobMapper analysisJobMapper,
            CacResultMapper cacResultMapper,
            CorrectedMaskVersionMapper correctedMaskVersionMapper,
            ObjectMapper objectMapper
    ) {
        this.analysisJobMapper = analysisJobMapper;
        this.cacResultMapper = cacResultMapper;
        this.correctedMaskVersionMapper = correctedMaskVersionMapper;
        this.objectMapper = objectMapper;
    }

    public CacResultResponse saveOrUpdateResult(Long jobId, CreateOrUpdateCacResultRequest request) {
        ensureJobExists(jobId);

        CacResult result = findByJobId(jobId);
        LocalDateTime now = LocalDateTime.now();

        if (result == null) {
            result = new CacResult();
            result.setJobId(jobId);
            result.setCreatedAt(now);
        }

        result.setAgatstonScore(request.getAgatstonScore());
        result.setRiskGrade(request.getRiskGrade());
        result.setResultJsonPath(request.getResultJsonPath());
        result.setCtVolumePath(request.getCtVolumePath());
        result.setAiMaskPath(request.getAiMaskPath());
        result.setCorrectedMaskPath(request.getCorrectedMaskPath());
        result.setReportPath(request.getReportPath());
        result.setUpdatedAt(now);

        if (result.getId() == null) {
            cacResultMapper.insert(result);
        } else {
            cacResultMapper.updateById(result);
        }

        return toResponse(result);
    }

    public CacResultResponse getResult(Long jobId) {
        ensureJobExists(jobId);

        CacResult result = findByJobId(jobId);
        if (result == null) {
            throw new ResponseStatusException(
                    HttpStatus.NOT_FOUND,
                    "CAC result not found for job: " + jobId
            );
        }

        return toResponse(result);
    }

    public Resource loadAiMaskResource(Long jobId) {
        AnalysisJob job = ensureJobExists(jobId);
        if (!isSuccessfulStatus(job.getStatus())) {
            log.warn("AI mask download rejected: jobId={}, status={}", jobId, job.getStatus());
            throw new ResponseStatusException(
                    HttpStatus.CONFLICT,
                    "AI mask is available only after job completion. Current status: " + job.getStatus()
            );
        }

        CacResult result = findByJobId(jobId);
        if (result == null || result.getAiMaskPath() == null || result.getAiMaskPath().isBlank()) {
            log.warn("AI mask download failed: jobId={}, aiMaskPath missing", jobId);
            throw new ResponseStatusException(
                    HttpStatus.NOT_FOUND,
                    "AI mask path not found for job: " + jobId
            );
        }

        Path path = Paths.get(result.getAiMaskPath()).toAbsolutePath().normalize();
        boolean exists = Files.exists(path);
        boolean readable = Files.isReadable(path);
        log.info("AI mask download check: jobId={}, path={}, exists={}, readable={}",
                jobId, path, exists, readable);

        // TODO: Before production, restrict artifact reads to a configured storage
        // root such as cac.storage.root instead of trusting any persisted path.
        if (!exists) {
            log.warn("AI mask download failed: jobId={}, path={}, reason=file missing", jobId, path);
            throw new ResponseStatusException(
                    HttpStatus.NOT_FOUND,
                    "AI mask file does not exist for job: " + jobId
            );
        }
        if (!readable) {
            log.error("AI mask download failed: jobId={}, path={}, reason=file not readable", jobId, path);
            throw new ResponseStatusException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "AI mask file is not readable for job: " + jobId
            );
        }

        log.info("AI mask download ready: jobId={}, path={}", jobId, path);
        return new FileSystemResource(path);
    }

    public Path resolveInputVolumePath(Long jobId) {
        AnalysisJob job = ensureJobExists(jobId);
        CacResult result = findByJobId(jobId);
        if (result != null
                && result.getCtVolumePath() != null
                && !result.getCtVolumePath().isBlank()) {
            Path ctPath = Paths.get(result.getCtVolumePath()).toAbsolutePath().normalize();
            if (!Files.isRegularFile(ctPath) || !Files.isReadable(ctPath)) {
                throw new ResponseStatusException(
                        HttpStatus.NOT_FOUND,
                        "Generated CT volume is not readable for job: " + jobId
                );
            }
            return ctPath;
        }
        if (job.getInputPath() == null || job.getInputPath().isBlank()) {
            log.warn("Input volume download failed: jobId={}, inputPath missing", jobId);
            throw new ResponseStatusException(
                    HttpStatus.NOT_FOUND,
                    "Input path not found for job: " + jobId
            );
        }

        Path path = Paths.get(job.getInputPath()).toAbsolutePath().normalize();
        boolean exists = Files.exists(path);
        boolean readable = Files.isReadable(path);
        log.info("Input volume download check: jobId={}, path={}, exists={}, readable={}, isDirectory={}",
                jobId, path, exists, readable, Files.isDirectory(path));

        // TODO: Before production, restrict input artifact reads to a configured
        // storage root such as cac.storage.root instead of trusting any persisted path.
        if (!exists) {
            log.warn("Input volume download failed: jobId={}, path={}, reason=missing", jobId, path);
            throw new ResponseStatusException(
                    HttpStatus.NOT_FOUND,
                    "Input volume does not exist for job: " + jobId
            );
        }
        if (!readable) {
            log.error("Input volume download failed: jobId={}, path={}, reason=not readable", jobId, path);
            throw new ResponseStatusException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "Input volume is not readable for job: " + jobId
            );
        }

        return path;
    }

    @Transactional
    public CorrectedMaskUploadResponse saveCorrectedMaskUpload(
            Long jobId,
            MultipartFile maskFile,
            MultipartFile metadataFile
    ) {
        AnalysisJob job = ensureSuccessfulJob(jobId, "Corrected mask upload");
        CacResult result = ensureResultExists(jobId);

        if (maskFile == null || maskFile.isEmpty()) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Corrected mask file is empty.");
        }
        if (metadataFile == null || metadataFile.isEmpty()) {
            throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Corrected mask metadata file is empty.");
        }

        try {
            JsonNode metadata = objectMapper.readTree(metadataFile.getInputStream());
            int version = metadata.path("version").asInt(0);
            if (version <= 0) {
                throw new ResponseStatusException(HttpStatus.BAD_REQUEST, "Corrected mask metadata version is missing.");
            }
            int width = metadata.path("width").asInt(0);
            int height = metadata.path("height").asInt(0);
            int depth = metadata.path("depth").asInt(0);
            if (width <= 0 || height <= 0 || depth <= 0) {
                throw new ResponseStatusException(
                        HttpStatus.BAD_REQUEST,
                        "Corrected mask metadata dimensions are invalid."
                );
            }
            long expectedMaskBytes;
            try {
                expectedMaskBytes = Math.multiplyExact(
                        Math.multiplyExact((long) width, (long) height),
                        (long) depth
                );
            } catch (ArithmeticException e) {
                throw new ResponseStatusException(
                        HttpStatus.BAD_REQUEST,
                        "Corrected mask dimensions are too large.",
                        e
                );
            }
            if (maskFile.getSize() != expectedMaskBytes) {
                throw new ResponseStatusException(
                        HttpStatus.BAD_REQUEST,
                        "Corrected mask byte size does not match metadata dimensions."
                );
            }
            boolean versionExists = correctedMaskVersionMapper.exists(
                    new LambdaQueryWrapper<CorrectedMaskVersion>()
                            .eq(CorrectedMaskVersion::getJobId, jobId)
                            .eq(CorrectedMaskVersion::getVersion, version)
            );
            if (versionExists) {
                throw new ResponseStatusException(
                        HttpStatus.CONFLICT,
                        "Corrected mask version already exists for job " + jobId + ": v" + version
                );
            }
            String maskNonzeroVoxelCount = metadata.path("maskNonzeroVoxelCount").asText("-");
            String eligibleVoxelCountHU130 = metadata.path("eligibleVoxelCountHU130").asText("-");
            String workingMaskChecksum = metadata.path("workingMaskChecksum").asText("-");

            Path outputDir = storageRoot()
                    .resolve("jobs")
                    .resolve(String.valueOf(jobId))
                    .resolve("corrected_masks")
                    .resolve("v" + version)
                    .normalize();
            Files.createDirectories(outputDir);

            Path correctedMaskPath = outputDir.resolve(String.format("corrected_mask_v%d.raw", version));
            Path correctedMetadataPath = outputDir.resolve(String.format("corrected_mask_v%d_metadata.json", version));
            if (Files.exists(correctedMaskPath) || Files.exists(correctedMetadataPath)) {
                throw new ResponseStatusException(
                        HttpStatus.CONFLICT,
                        "Corrected mask files already exist for job " + jobId + ": v" + version
                );
            }

            maskFile.transferTo(correctedMaskPath);
            metadataFile.transferTo(correctedMetadataPath);
            String checksumSha256 = sha256(correctedMaskPath);
            Path userExportDirectory = userExportDirectory(job, true);
            Path exportedRawPath = null;
            Path exportedMetadataPath = null;
            if (userExportDirectory != null) {
                Path correctedExportDirectory =
                        userExportDirectory.resolve("corrected_masks").normalize();
                Files.createDirectories(correctedExportDirectory);
                exportedRawPath = correctedExportDirectory.resolve(
                        correctedMaskPath.getFileName());
                exportedMetadataPath = correctedExportDirectory.resolve(
                        correctedMetadataPath.getFileName());
                Files.copy(
                        correctedMaskPath,
                        exportedRawPath,
                        StandardCopyOption.REPLACE_EXISTING
                );
                Files.copy(
                        correctedMetadataPath,
                        exportedMetadataPath,
                        StandardCopyOption.REPLACE_EXISTING
                );
            }

            CorrectedMaskVersion maskVersion = new CorrectedMaskVersion();
            maskVersion.setJobId(jobId);
            maskVersion.setVersion(version);
            maskVersion.setMaskPath(correctedMaskPath.toString());
            maskVersion.setMetadataPath(correctedMetadataPath.toString());
            maskVersion.setChecksumSha256(checksumSha256);
            maskVersion.setCreatedAt(LocalDateTime.now());
            correctedMaskVersionMapper.insert(maskVersion);

            result.setCorrectedMaskPath(correctedMaskPath.toString());
            result.setUpdatedAt(LocalDateTime.now());
            cacResultMapper.updateById(result);

            CorrectedMaskUploadResponse response = new CorrectedMaskUploadResponse();
            response.setJobId(job.getId());
            response.setVersion(version);
            response.setCorrectedMaskPath(correctedMaskPath.toString());
            response.setCorrectedMaskMetadataPath(correctedMetadataPath.toString());
            response.setCorrectedMaskBytes(Files.size(correctedMaskPath));
            response.setMetadataBytes(Files.size(correctedMetadataPath));
            response.setExportDirectory(
                    userExportDirectory == null ? null : userExportDirectory.toString());
            response.setExportedCorrectedRawPath(
                    exportedRawPath == null ? null : exportedRawPath.toString());
            response.setExportedCorrectedMetadataPath(
                    exportedMetadataPath == null ? null : exportedMetadataPath.toString());

            log.info("Corrected mask upload success: jobId={}, version={}, mask={}, metadata={}, bytes={}/{}, maskNonzeroVoxelCount={}, eligibleVoxelCountHU130={}, clientChecksum={}, sha256={}",
                    jobId, version, correctedMaskPath, correctedMetadataPath,
                    response.getCorrectedMaskBytes(), response.getMetadataBytes(),
                    maskNonzeroVoxelCount, eligibleVoxelCountHU130, workingMaskChecksum, checksumSha256);
            return response;
        } catch (IOException e) {
            log.error("Corrected mask upload failed: jobId={}", jobId, e);
            throw new ResponseStatusException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "Failed to store corrected mask for job: " + jobId,
                    e
            );
        }
    }

    public CacResultResponse recalculateScoreFromCorrectedMask(Long jobId) {
        AnalysisJob job = ensureSuccessfulJob(jobId, "Corrected-mask recalculation");
        CacResult result = ensureResultExists(jobId);

        if (result.getCorrectedMaskPath() == null || result.getCorrectedMaskPath().isBlank()) {
            throw new ResponseStatusException(
                    HttpStatus.CONFLICT,
                    "Upload a corrected mask before recalculating job: " + jobId
            );
        }

        Path inputPath = resolveInputVolumePath(jobId);
        Path correctedMaskPath = Paths.get(result.getCorrectedMaskPath()).toAbsolutePath().normalize();
        Path correctedMetadataPath = correctedMaskPath.resolveSibling(
                correctedMaskPath.getFileName().toString().replace(".raw", "_metadata.json")
        );

        ensureReadableFile(correctedMaskPath, "Corrected mask file");
        ensureReadableFile(correctedMetadataPath, "Corrected mask metadata file");

        Path scriptPath = resolveConfiguredPath(recalculateScriptPath);
        ensureReadableFile(scriptPath, "Recalculation script");
        Path pythonExecutable = resolveAbsoluteExecutable(recalculatePythonExecutable);
        Path segmentCacsSrc = resolveSegmentCacsSource();
        Path userExportDirectory;
        try {
            userExportDirectory = userExportDirectory(job, true);
        } catch (IOException e) {
            throw new ResponseStatusException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "Failed to prepare the selected output directory.",
                    e
            );
        }

        Path outputDir = storageRoot()
                .resolve("jobs")
                .resolve(String.valueOf(jobId))
                .resolve("recalculate")
                .normalize();
        try {
            Files.createDirectories(outputDir);
        } catch (IOException e) {
            throw new ResponseStatusException(HttpStatus.INTERNAL_SERVER_ERROR, "Failed to create recalculation output directory.", e);
        }

        int version = correctedMaskVersion(correctedMetadataPath);
        Path outputJsonPath = outputDir.resolve(String.format("recalculate_result_v%d.json", version));
        long correctedMaskBytes;
        try {
            correctedMaskBytes = Files.size(correctedMaskPath);
        } catch (IOException e) {
            correctedMaskBytes = -1;
        }
        BigDecimal oldCorrectedScore = result.getCorrectedAgatstonScore();
        List<String> command = new ArrayList<>(List.of(
                pythonExecutable.toString(),
                scriptPath.toString(),
                "--input-volume", inputPath.toString(),
                "--corrected-mask", correctedMaskPath.toString(),
                "--metadata", correctedMetadataPath.toString(),
                "--output", outputJsonPath.toString(),
                "--segmentcacs-src", segmentCacsSrc.toString()
        ));
        Path exportedCorrectedMaskPath = null;
        if (userExportDirectory != null) {
            exportedCorrectedMaskPath = userExportDirectory.resolve(
                    String.format("corrected_mask_v%d.nrrd", version));
            command.add("--export-mask");
            command.add(exportedCorrectedMaskPath.toString());
        }

        log.info("Recalculate using DB current correctedMaskPath: {}", correctedMaskPath);
        log.info("Corrected-mask recalculation starting: jobId={}, version={}, input={}, correctedMask={}, metadata={}, maskBytes={}, output={}, oldCorrectedScore={}",
                jobId, version, inputPath, correctedMaskPath, correctedMetadataPath, correctedMaskBytes, outputJsonPath, oldCorrectedScore);
        runRecalculationProcess(command);

        try {
            JsonNode payload = objectMapper.readTree(outputJsonPath.toFile());
            BigDecimal agatstonScore = payload.path("agatstonScore").isNumber()
                    ? payload.path("agatstonScore").decimalValue()
                    : null;
            String riskGrade = payload.path("riskGrade").asText(null);

            result.setCorrectedMaskPath(correctedMaskPath.toString());
            result.setCorrectedAgatstonScore(agatstonScore);
            result.setCorrectedRiskGrade(riskGrade);
            result.setCorrectedResultJsonPath(outputJsonPath.toString());
            result.setCorrectedAt(LocalDateTime.now());
            result.setUpdatedAt(LocalDateTime.now());
            cacResultMapper.updateById(result);
            if (userExportDirectory != null) {
                Path exportedCorrectedResultPath = userExportDirectory.resolve(
                        String.format("recalculate_result_v%d.json", version));
                Files.copy(
                        outputJsonPath,
                        exportedCorrectedResultPath,
                        StandardCopyOption.REPLACE_EXISTING
                );
            }

            log.info("Corrected-mask recalculation success: jobId={}, version={}, oldCorrectedScore={}, newCorrectedScore={}, riskGrade={}, resultJson={}",
                    jobId, version, oldCorrectedScore, agatstonScore, riskGrade, outputJsonPath);
            CacResultResponse response = toResponse(result);
            response.setMaskNonzeroVoxelCount(longValue(payload, "maskNonzeroVoxelCount"));
            response.setEligibleVoxelCountHU130(longValue(payload, "eligibleVoxelCountHU130"));
            response.setMaxHUInsideMask(intValue(payload, "maxHUInsideMask"));
            response.setMinHUInsideMask(intValue(payload, "minHUInsideMask"));
            response.setUsedOfficialSegmentCacsScoring(payload.path("usedOfficialSegmentCacsScoring").asBoolean(false));
            response.setModelInferenceSkipped(payload.path("modelInferenceSkipped").asBoolean(false));
            response.setExportDirectory(
                    userExportDirectory == null ? null : userExportDirectory.toString());
            response.setExportedCorrectedMaskPath(
                    exportedCorrectedMaskPath == null
                            ? null
                            : exportedCorrectedMaskPath.toString());
            if (userExportDirectory != null) {
                response.setExportedCorrectedResultJsonPath(
                        userExportDirectory.resolve(
                                String.format("recalculate_result_v%d.json", version)).toString());
            }
            return response;
        } catch (IOException e) {
            log.error("Failed to read recalculation output: jobId={}, output={}", jobId, outputJsonPath, e);
            throw new ResponseStatusException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "Failed to read recalculation result for job: " + jobId,
                    e
            );
        }
    }

    private AnalysisJob ensureJobExists(Long jobId) {
        AnalysisJob job = analysisJobMapper.selectById(jobId);
        if (job == null) {
            log.warn("Job lookup failed: jobId={} not found", jobId);
            throw new ResponseStatusException(
                    HttpStatus.NOT_FOUND,
                    "Job not found: " + jobId
            );
        }
        return job;
    }

    private AnalysisJob ensureSuccessfulJob(Long jobId, String operationName) {
        AnalysisJob job = ensureJobExists(jobId);
        if (!isSuccessfulStatus(job.getStatus())) {
            log.warn("{} rejected: jobId={}, status={}", operationName, jobId, job.getStatus());
            throw new ResponseStatusException(
                    HttpStatus.CONFLICT,
                    operationName + " is available only after job SUCCESS. Current status: " + job.getStatus()
            );
        }
        return job;
    }

    private CacResult ensureResultExists(Long jobId) {
        CacResult result = findByJobId(jobId);
        if (result == null) {
            throw new ResponseStatusException(
                    HttpStatus.NOT_FOUND,
                    "CAC result not found for job: " + jobId
            );
        }
        return result;
    }

    private Path storageRoot() {
        return resolveConfiguredPath(storageRoot);
    }

    private Path resolveConfiguredPath(String value) {
        Path path = Paths.get(value).toAbsolutePath().normalize();
        return path;
    }

    private Path resolveAbsoluteExecutable(String value) {
        if (value == null || value.isBlank()) {
            throw new ResponseStatusException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "CAC_PYTHON_EXECUTABLE is not configured for recalculation."
            );
        }
        Path path = Paths.get(value);
        if (!path.isAbsolute()) {
            throw new ResponseStatusException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "CAC_PYTHON_EXECUTABLE must be an absolute path."
            );
        }
        path = path.normalize();
        ensureReadableFile(path, "Configured Python executable");
        return path;
    }

    private void ensureReadableFile(Path path, String label) {
        if (!Files.exists(path)) {
            throw new ResponseStatusException(HttpStatus.NOT_FOUND, label + " does not exist: " + path);
        }
        if (!Files.isRegularFile(path) || !Files.isReadable(path)) {
            throw new ResponseStatusException(HttpStatus.INTERNAL_SERVER_ERROR, label + " is not readable: " + path);
        }
    }

    private int correctedMaskVersion(Path correctedMetadataPath) {
        try {
            JsonNode metadata = objectMapper.readTree(correctedMetadataPath.toFile());
            int version = metadata.path("version").asInt(1);
            return Math.max(version, 1);
        } catch (IOException e) {
            throw new ResponseStatusException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "Failed to read corrected mask version from metadata: " + correctedMetadataPath,
                    e
            );
        }
    }

    private void runRecalculationProcess(List<String> command) {
        ProcessBuilder processBuilder = new ProcessBuilder(command);
        processBuilder.redirectErrorStream(true);

        try {
            Process process = processBuilder.start();
            ByteArrayOutputStream combinedOutput = new ByteArrayOutputStream();
            CompletableFuture<Void> outputReader = CompletableFuture.runAsync(() -> {
                try {
                    process.getInputStream().transferTo(combinedOutput);
                } catch (IOException e) {
                    throw new RuntimeException(e);
                }
            });

            boolean finished = process.waitFor(RECALCULATE_TIMEOUT_SECONDS, TimeUnit.SECONDS);
            if (!finished) {
                process.destroyForcibly();
                throw new ResponseStatusException(
                        HttpStatus.INTERNAL_SERVER_ERROR,
                        "Corrected-mask recalculation timed out."
                );
            }
            outputReader.join();

            String outputText = combinedOutput.toString();
            log.info("Corrected-mask recalculation output: {}", trimForLog(outputText));

            if (process.exitValue() != 0) {
                throw new ResponseStatusException(
                        HttpStatus.INTERNAL_SERVER_ERROR,
                        "Corrected-mask recalculation failed: " + trimForLog(outputText)
                );
            }
        } catch (IOException e) {
            throw new ResponseStatusException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "Failed to start corrected-mask recalculation process.",
                    e
            );
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new ResponseStatusException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "Corrected-mask recalculation was interrupted.",
                    e
            );
        }
    }

    private String trimForLog(String value) {
        if (value == null || value.isBlank()) {
            return "";
        }
        String trimmed = value.trim();
        return trimmed.length() <= 4000 ? trimmed : trimmed.substring(0, 4000) + "...<truncated>";
    }

    private Long longValue(JsonNode node, String fieldName) {
        JsonNode value = node.path(fieldName);
        return value.isNumber() ? value.asLong() : null;
    }

    private Integer intValue(JsonNode node, String fieldName) {
        JsonNode value = node.path(fieldName);
        return value.isNumber() ? value.asInt() : null;
    }

    private CacResult findByJobId(Long jobId) {
        return cacResultMapper.selectOne(
                new LambdaQueryWrapper<CacResult>()
                        .eq(CacResult::getJobId, jobId)
                        .last("LIMIT 1")
        );
    }

    private CacResultResponse toResponse(CacResult result) {
        CacResultResponse response = new CacResultResponse();
        response.setId(result.getId());
        response.setJobId(result.getJobId());
        response.setAgatstonScore(result.getAgatstonScore());
        response.setRiskGrade(result.getRiskGrade());
        response.setResultJsonPath(result.getResultJsonPath());
        response.setCtVolumePath(result.getCtVolumePath());
        response.setAiMaskPath(result.getAiMaskPath());
        response.setCorrectedMaskPath(result.getCorrectedMaskPath());
        response.setCorrectedAgatstonScore(result.getCorrectedAgatstonScore());
        response.setCorrectedRiskGrade(result.getCorrectedRiskGrade());
        response.setCorrectedResultJsonPath(result.getCorrectedResultJsonPath());
        response.setCorrectedAt(result.getCorrectedAt());
        response.setReportPath(result.getReportPath());
        response.setCreatedAt(result.getCreatedAt());
        response.setUpdatedAt(result.getUpdatedAt());
        applyExportPaths(response, result);
        return response;
    }

    private Path userExportDirectory(AnalysisJob job, boolean create) throws IOException {
        if (job == null || job.getOutputPath() == null || job.getOutputPath().isBlank()) {
            return null;
        }
        Path configured = Paths.get(job.getOutputPath());
        if (!configured.isAbsolute()) {
            return null;
        }
        Path outputBase = configured.normalize();
        Path legacyManagedOutput = storageRoot()
                .resolve("jobs")
                .resolve(String.valueOf(job.getId()))
                .resolve("output")
                .normalize();
        if (outputBase.equals(legacyManagedOutput)) {
            return null;
        }
        String outputFolderName = job.getOutputName() == null || job.getOutputName().isBlank()
                ? "cac_job_" + job.getId()
                : OutputNamePolicy.validate(job.getOutputName());
        Path exportDirectory = outputBase
                .resolve(outputFolderName)
                .toAbsolutePath()
                .normalize();
        if (!exportDirectory.startsWith(outputBase)) {
            throw new IOException("The selected output directory is invalid.");
        }
        if (create) {
            Files.createDirectories(exportDirectory);
            if (!Files.isDirectory(exportDirectory) || !Files.isWritable(exportDirectory)) {
                throw new IOException("The selected output directory is not writable.");
            }
        }
        return exportDirectory;
    }

    private void applyExportPaths(CacResultResponse response, CacResult result) {
        AnalysisJob job = analysisJobMapper.selectById(result.getJobId());
        try {
            Path exportDirectory = userExportDirectory(job, false);
            if (exportDirectory == null) {
                return;
            }
            response.setExportDirectory(exportDirectory.toString());
            Path ctPath = exportDirectory.resolve("ct.nrrd");
            Path aiMaskPath = exportDirectory.resolve("ai_mask.nrrd");
            Path resultJsonPath = exportDirectory.resolve("result.json");
            if (Files.isRegularFile(ctPath)) {
                response.setExportedCtVolumePath(ctPath.toString());
            }
            if (Files.isRegularFile(aiMaskPath)) {
                response.setExportedAiMaskPath(aiMaskPath.toString());
            }
            if (Files.isRegularFile(resultJsonPath)) {
                response.setExportedResultJsonPath(resultJsonPath.toString());
            }
            if (result.getCorrectedMaskPath() != null
                    && !result.getCorrectedMaskPath().isBlank()) {
                Path correctedPath = Paths.get(result.getCorrectedMaskPath());
                Path versionDirectory = correctedPath.getParent();
                String versionName = versionDirectory == null
                        ? ""
                        : versionDirectory.getFileName().toString();
                if (versionName.matches("v\\d+")) {
                    Path exportedCorrectedMask = exportDirectory.resolve(
                            "corrected_mask_" + versionName + ".nrrd");
                    if (Files.isRegularFile(exportedCorrectedMask)) {
                        response.setExportedCorrectedMaskPath(
                                exportedCorrectedMask.toString());
                    }
                    Path exportedCorrectedResult = exportDirectory.resolve(
                            "recalculate_result_" + versionName + ".json");
                    if (Files.isRegularFile(exportedCorrectedResult)) {
                        response.setExportedCorrectedResultJsonPath(
                                exportedCorrectedResult.toString());
                    }
                }
            }
        } catch (IOException e) {
            log.warn("Could not resolve user export paths for jobId={}: {}",
                    result.getJobId(), e.getMessage());
        }
    }

    private Path resolveSegmentCacsSource() {
        if (recalculateSegmentCacsSrc == null || recalculateSegmentCacsSrc.isBlank()) {
            throw new ResponseStatusException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "SEGMENT-CACS source path is not configured for recalculation."
            );
        }
        Path configured = resolveConfiguredPath(recalculateSegmentCacsSrc);
        Path sourcePath = Files.isDirectory(configured.resolve("src"))
                ? configured.resolve("src").normalize()
                : configured;
        if (!Files.isDirectory(sourcePath) || !Files.isReadable(sourcePath)) {
            throw new ResponseStatusException(
                    HttpStatus.INTERNAL_SERVER_ERROR,
                    "SEGMENT-CACS source directory is not readable: " + sourcePath
            );
        }
        return sourcePath;
    }

    private String sha256(Path path) throws IOException {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            try (DigestInputStream input = new DigestInputStream(Files.newInputStream(path), digest)) {
                input.transferTo(java.io.OutputStream.nullOutputStream());
            }
            return HexFormat.of().formatHex(digest.digest());
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException("SHA-256 is unavailable.", e);
        }
    }

    private boolean isSuccessfulStatus(String status) {
        return "SUCCESS".equals(status) || "COMPLETED".equals(status);
    }
}
