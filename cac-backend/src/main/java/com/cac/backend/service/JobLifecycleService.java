package com.cac.backend.service;

import com.cac.backend.dto.CreateJobRequest;
import com.cac.backend.entity.AnalysisJob;
import com.cac.backend.mapper.AnalysisJobMapper;
import com.cac.backend.websocket.JobWebSocketHandler;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;

@Service
public class JobLifecycleService {

    private final AnalysisJobMapper analysisJobMapper;
    private final JobStatusCache jobStatusCache;
    private final JobWebSocketHandler jobWebSocketHandler;

    public JobLifecycleService(
            AnalysisJobMapper analysisJobMapper,
            JobStatusCache jobStatusCache,
            JobWebSocketHandler jobWebSocketHandler
    ) {
        this.analysisJobMapper = analysisJobMapper;
        this.jobStatusCache = jobStatusCache;
        this.jobWebSocketHandler = jobWebSocketHandler;
    }

    public AnalysisJob createJob(CreateJobRequest request) {
        LocalDateTime now = LocalDateTime.now();
        AnalysisJob job = new AnalysisJob();
        job.setModelName(request.getModelName());
        job.setStatus("PENDING");
        job.setProgress(0);
        job.setInputPath(request.getInputPath());
        job.setOutputPath(request.getOutputPath());
        job.setDevice(request.getDevice());
        job.setCreatedAt(now);
        job.setUpdatedAt(now);
        analysisJobMapper.insert(job);
        publish(job);
        return job;
    }

    public AnalysisJob getJob(Long id) {
        AnalysisJob job = analysisJobMapper.selectById(id);
        if (job == null) {
            throw new IllegalArgumentException("Job not found: " + id);
        }
        return job;
    }

    public AnalysisJob updateJobStatus(
            Long id,
            String status,
            Integer progress,
            String errorMessage,
            String workerId,
            String device
    ) {
        AnalysisJob job = getJob(id);
        LocalDateTime now = LocalDateTime.now();
        job.setStatus(status);
        if (progress != null) {
            job.setProgress(progress);
        }
        if (errorMessage != null) {
            job.setErrorMessage(errorMessage);
        }
        if (workerId != null) {
            job.setWorkerId(workerId);
        }
        if (device != null) {
            job.setDevice(device);
        }
        if (job.getStartedAt() == null && isRunningStatus(status)) {
            job.setStartedAt(now);
        }
        if (isTerminalStatus(status)) {
            job.setFinishedAt(now);
        }
        job.setUpdatedAt(now);
        analysisJobMapper.updateById(job);
        publish(job);
        return job;
    }

    private void publish(AnalysisJob job) {
        jobStatusCache.saveJobStatus(job.getId(), job.getStatus(), job.getProgress());
        jobWebSocketHandler.broadcastJobStatus(job.getId(), job.getStatus(), job.getProgress());
    }

    private boolean isRunningStatus(String status) {
        return !"PENDING".equals(status) && !isTerminalStatus(status);
    }

    private boolean isTerminalStatus(String status) {
        return "SUCCESS".equals(status)
                || "COMPLETED".equals(status)
                || "FAILED".equals(status)
                || "CANCELED".equals(status);
    }
}
