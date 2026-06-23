package com.cac.backend.service;

import com.baomidou.mybatisplus.core.conditions.query.LambdaQueryWrapper;
import com.cac.backend.dto.CacResultResponse;
import com.cac.backend.dto.CreateOrUpdateCacResultRequest;
import com.cac.backend.entity.AnalysisJob;
import com.cac.backend.entity.CacResult;
import com.cac.backend.mapper.AnalysisJobMapper;
import com.cac.backend.mapper.CacResultMapper;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Service;
import org.springframework.web.server.ResponseStatusException;

import java.time.LocalDateTime;

@Service
public class CacResultService {

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

    private void ensureJobExists(Long jobId) {
        AnalysisJob job = analysisJobMapper.selectById(jobId);
        if (job == null) {
            throw new ResponseStatusException(
                    HttpStatus.NOT_FOUND,
                    "Job not found: " + jobId
            );
        }
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
