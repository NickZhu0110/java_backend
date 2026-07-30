package com.cac.backend.service;

import com.cac.backend.dto.CreateJobRequest;
import com.cac.backend.entity.AnalysisJob;
import org.springframework.http.HttpStatus;
import org.springframework.stereotype.Service;
import org.springframework.web.server.ResponseStatusException;

@Service
public class JobService {

    private final JobLifecycleService jobLifecycleService;
    private final AnalysisDispatcher analysisDispatcher;

    public JobService(
            JobLifecycleService jobLifecycleService,
            AnalysisDispatcher analysisDispatcher
    ) {
        this.jobLifecycleService = jobLifecycleService;
        this.analysisDispatcher = analysisDispatcher;
    }

    public Long createJob(CreateJobRequest request) {
        try {
            request.setOutputName(OutputNamePolicy.validate(request.getOutputName()));
        } catch (IllegalArgumentException exception) {
            throw new ResponseStatusException(
                    HttpStatus.BAD_REQUEST, exception.getMessage(), exception);
        }
        AnalysisJob job = jobLifecycleService.createJob(request);
        analysisDispatcher.dispatch(job, request);
        return job.getId();
    }

    public AnalysisJob getJob(Long id) {
        return jobLifecycleService.getJob(id);
    }

    public AnalysisJob updateJobStatus(
            Long id,
            String status,
            Integer progress,
            String errorMessage,
            String workerId,
            String device
    ) {
        return jobLifecycleService.updateJobStatus(
                id, status, progress, errorMessage, workerId, device);
    }
}
