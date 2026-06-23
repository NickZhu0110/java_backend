package com.cac.backend.dto;

import jakarta.validation.constraints.Max;
import jakarta.validation.constraints.Min;
import jakarta.validation.constraints.NotBlank;
import lombok.Data;

@Data
public class UpdateJobStatusRequest {

    @NotBlank
    private String status;

    @Min(0)
    @Max(100)
    private Integer progress;

    private String errorMessage;

    private String workerId;

    private String device;
}
