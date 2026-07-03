ALTER TABLE cac_results
    ADD COLUMN corrected_agatston_score NUMERIC(12, 2),
    ADD COLUMN corrected_risk_grade VARCHAR(32),
    ADD COLUMN corrected_result_json_path TEXT,
    ADD COLUMN corrected_at TIMESTAMP;
