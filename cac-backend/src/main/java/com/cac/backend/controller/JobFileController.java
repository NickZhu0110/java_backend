package com.cac.backend.controller;

import com.cac.backend.service.CacResultService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.core.io.Resource;
import org.springframework.http.ContentDisposition;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import java.io.IOException;

@RestController
@RequestMapping("/api/jobs/{jobId}/files")
public class JobFileController {

    private static final Logger log = LoggerFactory.getLogger(JobFileController.class);

    private final CacResultService cacResultService;

    public JobFileController(CacResultService cacResultService) {
        this.cacResultService = cacResultService;
    }

    @GetMapping("/ai-mask")
    public ResponseEntity<Resource> downloadAiMask(@PathVariable Long jobId) throws IOException {
        Resource resource = cacResultService.loadAiMaskResource(jobId);
        long contentLength = resource.contentLength();
        log.info("AI mask download success: jobId={}, bytes={}", jobId, contentLength);

        return ResponseEntity.ok()
                .contentType(MediaType.APPLICATION_OCTET_STREAM)
                .contentLength(contentLength)
                .header(HttpHeaders.CONTENT_DISPOSITION,
                        ContentDisposition.attachment()
                                .filename("ai_mask_v0.nrrd")
                                .build()
                                .toString())
                .body(resource);
    }
}
