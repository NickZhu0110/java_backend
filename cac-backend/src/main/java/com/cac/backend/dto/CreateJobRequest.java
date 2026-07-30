package com.cac.backend.dto;

import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Size;
import lombok.Data;

@Data
public class CreateJobRequest {

    @NotBlank
    private String modelName;

    @NotBlank
    private String inputPath;

    private String outputPath;

    @NotBlank
    @Size(max = 100)
    private String outputName;

    private String fileType = "dcm";

    private String device = "cpu";

    private String segmentcacsSrc;

    private String modelPath;

    private Boolean useZeroModule;
}
