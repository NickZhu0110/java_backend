ALTER TABLE analysis_jobs
    ADD COLUMN started_at TIMESTAMP,
    ADD COLUMN finished_at TIMESTAMP,
    ADD COLUMN error_message TEXT,
    ADD COLUMN worker_id VARCHAR(128),
    ADD COLUMN device VARCHAR(128);
