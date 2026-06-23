package com.cac.backend.service;

import org.springframework.data.redis.core.StringRedisTemplate;
import org.springframework.stereotype.Service;

@Service
public class JobCacheService {

    private final StringRedisTemplate redisTemplate;

    public JobCacheService(StringRedisTemplate redisTemplate) {
        this.redisTemplate = redisTemplate;
    }

    public void saveJobStatus(Long jobId, String status, Integer progress) {
        redisTemplate.opsForValue().set("job:" + jobId + ":status", status);

        if (progress != null) {
            redisTemplate.opsForValue().set("job:" + jobId + ":progress", progress.toString());
        }
    }

    public String getJobStatus(Long jobId) {
        return redisTemplate.opsForValue().get("job:" + jobId + ":status");
    }

    public String getJobProgress(Long jobId) {
        return redisTemplate.opsForValue().get("job:" + jobId + ":progress");
    }
}