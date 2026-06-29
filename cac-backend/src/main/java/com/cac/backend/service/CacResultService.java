package com.cac.backend.service;

import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.cac.backend.dto.CacResultResponse;
import com.cac.backend.dto.CreateOrUpdateCacResultRequest;
import com.cac.backend.entity.AnalysisJob;
import com.cac.backend.entity.CacResult;
import com.cac.backend.mapper.AnalysisJobMapper;
import com.cac.backend.mapper.CacResultMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.core.io.FileSystemResource;
import org.springframework.core.io.Resource;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Service;
import org.springframework.web.server.ResponseStatusException;

import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.LocalDateTime;

@Service
public class CacResultService {

    private static final Logger log = LoggerFactory.getLogger(CacResultService.class);

    private final AnalysisJobMapper analysisJobMapper;
    private final CacResultMapper cacResultMapper;

    public CacResultService(
            AnalysisJobMapper analysisJobMapper,
            CacResultMapper cacResultMapper
    ) {
        this.analysisJobMapper = analysisJobMapper;
        this.cacResultMapper = cacResultMapper;
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
        if (!"SUCCESS".equals(job.getStatus())) {
            log.warn("AI mask download rejected: jobId={}, status={}", jobId, job.getStatus());
            throw new ResponseStatusException(
                    HttpStatus.CONFLICT,
                    "AI mask is available only after job SUCCESS. Current status: " + job.getStatus()
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
        response.setAiMaskPath(result.getAiMaskPath());
        response.setCorrectedMaskPath(result.getCorrectedMaskPath());
        response.setReportPath(result.getReportPath());
        response.setCreatedAt(result.getCreatedAt());
        response.setUpdatedAt(result.getUpdatedAt());
        return response;
    }
}
