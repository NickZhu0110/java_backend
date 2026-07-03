package com.cac.backend.dto;

import jakarta.validation.constraints.NotBlank;
import lombok.Data;

@Data
public class CreateJobRequest {

    @NotBlank
    private String modelName;

    @NotBlank
    private String inputPath;

    @NotBlank
    private String outputPath;
}