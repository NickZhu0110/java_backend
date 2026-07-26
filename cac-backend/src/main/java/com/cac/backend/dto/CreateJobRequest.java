package com.cac.backend.dto;

import jakarta.validation.constraints.NotBlank;
import lombok.Data;

@Data
public class CreateJobRequest {

    @NotBlank
    private String modelName;

    @NotBlank
    private String inputPath;

    private String outputPath;

    private String fileType = "dcm";

    private String device = "cpu";

    private String segmentcacsSrc;

    private String modelPath;

    private Boolean useZeroModule;
}
