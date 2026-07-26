package com.cac.backend.service;

import com.cac.backend.dto.CreateJobRequest;
import com.cac.backend.entity.AnalysisJob;

public interface AnalysisDispatcher {

    void dispatch(AnalysisJob job, CreateJobRequest request);
}
