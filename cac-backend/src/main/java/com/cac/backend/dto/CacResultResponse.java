package com.cac.backend.dto;

import lombok.Data;

import java.math.BigDecimal;
import java.time.LocalDateTime;

@Data
public class CacResultResponse {

    private Long id;

    private Long jobId;

    private BigDecimal agatstonScore;

    private String riskGrade;

    private String resultJsonPath;

    private String aiMaskPath;

    private String correctedMaskPath;

    private String reportPath;

    private LocalDateTime createdAt;

    private LocalDateTime updatedAt;
}
