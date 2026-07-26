package com.cac.backend.service;

import org.springframework.data.redis.core.StringRedisTemplate;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.stereotype.Service;

@Service
@ConditionalOnProperty(
        name = "cac.status-cache.mode",
        havingValue = "redis",
        matchIfMissing = true
)
public class JobCacheService implements JobStatusCache {

    private final StringRedisTemplate redisTemplate;

    public JobCacheService(StringRedisTemplate redisTemplate) {
        this.redisTemplate = redisTemplate;
    }

    @Override
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
