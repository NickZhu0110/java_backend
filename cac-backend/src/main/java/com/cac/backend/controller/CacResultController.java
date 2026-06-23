package com.cac.backend.controller;

import com.cac.backend.dto.CacResultResponse;
import com.cac.backend.dto.CreateOrUpdateCacResultRequest;
import com.cac.backend.service.CacResultService;
import jakarta.validation.Valid;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
@RequestMapping("/api/jobs/{jobId}/result")
public class CacResultController {

    private final CacResultService cacResultService;

    public CacResultController(CacResultService cacResultService) {
        this.cacResultService = cacResultService;
    }

    @PostMapping
    public CacResultResponse saveOrUpdateResult(
            @PathVariable Long jobId,
            @Valid @RequestBody CreateOrUpdateCacResultRequest request
    ) {
        return cacResultService.saveOrUpdateResult(jobId, request);
    }

    @GetMapping
    public CacResultResponse getResult(@PathVariable Long jobId) {
        return cacResultService.getResult(jobId);
    }
}
