CREATE TABLE cac_results (
    id BIGSERIAL PRIMARY KEY,
    job_id BIGINT NOT NULL,
    agatston_score NUMERIC(12, 2),
    risk_grade VARCHAR(32),
    result_json_path TEXT,
    ai_mask_path TEXT,
    corrected_mask_path TEXT,
    report_path TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_cac_results_job
        FOREIGN KEY (job_id)
        REFERENCES analysis_jobs(id)
        ON DELETE CASCADE,
    CONSTRAINT uk_cac_results_job_id UNIQUE (job_id)
);
