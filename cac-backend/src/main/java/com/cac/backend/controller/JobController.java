package com.cac.backend.controller;

import com.cac.backend.dto.CreateJobRequest;
import com.cac.backend.dto.UpdateJobStatusRequest;
import com.cac.backend.entity.AnalysisJob;
import com.cac.backend.service.JobService;
import jakarta.validation.Valid;
import org.springframework.web.bind.annotation.*;

@RestController
@RequestMapping("/api/jobs")
public class JobController {

    private final JobService jobService;

    public JobController(JobService jobService) {
        this.jobService = jobService;
    }

    @PostMapping
    public Long createJob(@Valid @RequestBody CreateJobRequest request) {
        return jobService.createJob(request);
    }

    @GetMapping("/{id}")
    public AnalysisJob getJob(@PathVariable Long id) {
        return jobService.getJob(id);
    }

    @PatchMapping("/{id}/status")
    public AnalysisJob updateJobStatus(
            @PathVariable Long id,
            @Valid @RequestBody UpdateJobStatusRequest request
    ) {
        return jobService.updateJobStatus(
                id,
                request.getStatus(),
                request.getProgress(),
                request.getErrorMessage(),
                request.getWorkerId(),
                request.getDevice()
        );
    }
}
