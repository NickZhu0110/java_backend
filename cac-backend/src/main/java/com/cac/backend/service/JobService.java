package com.cac.backend.service;

import com.cac.backend.dto.CreateJobRequest;
import com.cac.backend.entity.AnalysisJob;
import com.cac.backend.mapper.AnalysisJobMapper;
import com.cac.backend.websocket.JobWebSocketHandler;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;

@Service
public class JobService {

    private final AnalysisJobMapper analysisJobMapper;
    private final JobCacheService jobCacheService;
    private final JobWebSocketHandler jobWebSocketHandler;
    private final AnalysisEventProducer analysisEventProducer;

    public JobService(
            AnalysisJobMapper analysisJobMapper,
            JobCacheService jobCacheService,
            JobWebSocketHandler jobWebSocketHandler,
            AnalysisEventProducer analysisEventProducer
    ) {
        this.analysisJobMapper = analysisJobMapper;
        this.jobCacheService = jobCacheService;
        this.jobWebSocketHandler = jobWebSocketHandler;
        this.analysisEventProducer = analysisEventProducer;
    }

    public Long createJob(CreateJobRequest request) {
        AnalysisJob job = new AnalysisJob();

        job.setModelName(request.getModelName());
        job.setStatus("PENDING");
        job.setProgress(0);
        job.setInputPath(request.getInputPath());
        job.setOutputPath(request.getOutputPath());
        job.setCreatedAt(LocalDateTime.now());
        job.setUpdatedAt(LocalDateTime.now());

        // 1. Save to PostgreSQL
        analysisJobMapper.insert(job);

        // 2. Save realtime status to Redis
        jobCacheService.saveJobStatus(
                job.getId(),
                job.getStatus(),
                job.getProgress()
        );

        // 3. Push realtime status through WebSocket
        jobWebSocketHandler.broadcastJobStatus(
                job.getId(),
                job.getStatus(),
                job.getProgress()
        );

        // 4. Publish Kafka task event for future Python worker
        analysisEventProducer.publishAnalysisRequested(job);

        return job.getId();
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
        AnalysisJob job = analysisJobMapper.selectById(id);

        if (job == null) {
            throw new IllegalArgumentException("Job not found: " + id);
        }

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

        if ("RUNNING".equals(status) && job.getStartedAt() == null) {
            job.setStartedAt(now);
        }

        if (isTerminalStatus(status) && job.getFinishedAt() == null) {
            job.setFinishedAt(now);
        }

        job.setUpdatedAt(now);

        // 1. Update PostgreSQL
        analysisJobMapper.updateById(job);

        // 2. Update Redis
        jobCacheService.saveJobStatus(
                job.getId(),
                job.getStatus(),
                job.getProgress()
        );

        // 3. Push WebSocket message
        jobWebSocketHandler.broadcastJobStatus(
                job.getId(),
                job.getStatus(),
                job.getProgress()
        );

        return job;
    }

    private boolean isTerminalStatus(String status) {
        return "SUCCESS".equals(status)
                || "FAILED".equals(status)
                || "CANCELED".equals(status);
    }
}
