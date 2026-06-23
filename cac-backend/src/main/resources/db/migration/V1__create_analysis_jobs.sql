CREATE TABLE analysis_jobs (
                               id BIGSERIAL PRIMARY KEY,
                               model_name VARCHAR(64) NOT NULL,
                               status VARCHAR(32) NOT NULL,
                               progress INT DEFAULT 0,
                               input_path TEXT,
                               output_path TEXT,
                               created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                               updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);