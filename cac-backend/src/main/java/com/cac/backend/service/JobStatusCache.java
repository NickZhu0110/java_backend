package com.cac.backend.service;

public interface JobStatusCache {

    void saveJobStatus(Long jobId, String status, Integer progress);
}
