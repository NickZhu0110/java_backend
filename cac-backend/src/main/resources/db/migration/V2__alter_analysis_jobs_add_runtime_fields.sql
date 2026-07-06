ALTER TABLE analysis_jobs ADD COLUMN started_at TIMESTAMP;
ALTER TABLE analysis_jobs ADD COLUMN finished_at TIMESTAMP;
ALTER TABLE analysis_jobs ADD COLUMN error_message TEXT;
ALTER TABLE analysis_jobs ADD COLUMN worker_id VARCHAR(128);
ALTER TABLE analysis_jobs ADD COLUMN device VARCHAR(128);
