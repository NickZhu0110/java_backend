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

    private BigDecimal correctedAgatstonScore;

    private String correctedRiskGrade;

    private String correctedResultJsonPath;

    private LocalDateTime correctedAt;

    private Long maskNonzeroVoxelCount;

    private Long eligibleVoxelCountHU130;

    private Integer maxHUInsideMask;

    private Integer minHUInsideMask;

    private Boolean usedOfficialSegmentCacsScoring;

    private Boolean modelInferenceSkipped;

    private String reportPath;

    private LocalDateTime createdAt;

    private LocalDateTime updatedAt;
}
