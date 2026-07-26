package com.cac.backend.service;

import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.stereotype.Service;

@Service
@ConditionalOnProperty(name = "cac.status-cache.mode", havingValue = "none")
public class NoOpJobStatusCache implements JobStatusCache {

    @Override
    public void saveJobStatus(Long jobId, String status, Integer progress) {
        // H2 is authoritative in local mode; WebSocket delivery remains active.
    }
}
