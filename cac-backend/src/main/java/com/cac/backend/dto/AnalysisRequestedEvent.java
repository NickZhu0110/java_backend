package com.cac.backend.dto;

import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@NoArgsConstructor
@AllArgsConstructor
public class AnalysisRequestedEvent {

    private Long jobId;

    private String modelName;

    private String inputPath;

    private String outputPath;
}