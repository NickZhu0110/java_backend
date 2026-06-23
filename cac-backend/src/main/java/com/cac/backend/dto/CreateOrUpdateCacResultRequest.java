package com.cac.backend.dto;

import jakarta.validation.constraints.DecimalMin;
import lombok.Data;

import java.math.BigDecimal;

@Data
public class CreateOrUpdateCacResultRequest {

    @DecimalMin("0.0")
    private BigDecimal agatstonScore;

    private String riskGrade;

    private String resultJsonPath;

    private String aiMaskPath;

    private String correctedMaskPath;

    private String reportPath;
}
