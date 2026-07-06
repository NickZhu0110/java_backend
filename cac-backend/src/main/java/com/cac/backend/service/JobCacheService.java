package com.cac.backend.service;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.data.redis.core.StringRedisTemplate;
import org.springframework.stereotype.Service;

@Service
public class JobCacheService {

    private static final Logger log = LoggerFactory.getLogger(JobCacheService.class);

    private final StringRedisTemplate redisTemplate;

    public JobCacheService(StringRedisTemplate redisTemplate) {
        this.redisTemplate = redisTemplate;
    }

    public void saveJobStatus(Long jobId, String status, Integer progress) {
        try {
            redisTemplate.opsForValue().set("job:" + jobId + ":status", status);

            if (progress != null) {
                redisTemplate.opsForValue().set("job:" + jobId + ":progress", progress.toString());
            }
        } catch (Exception ex) {
            log.warn("Skipping Redis job cache update for job {}: {}", jobId, ex.getMessage());
        }
    }

    public String getJobStatus(Long jobId) {
        try {
            return redisTemplate.opsForValue().get("job:" + jobId + ":status");
        } catch (Exception ex) {
            log.warn("Skipping Redis job cache read for job {}: {}", jobId, ex.getMessage());
            return null;
        }
    }

    public String getJobProgress(Long jobId) {
        try {
            return redisTemplate.opsForValue().get("job:" + jobId + ":progress");
        } catch (Exception ex) {
            log.warn("Skipping Redis job cache read for job {}: {}", jobId, ex.getMessage());
            return null;
        }
    }
}
