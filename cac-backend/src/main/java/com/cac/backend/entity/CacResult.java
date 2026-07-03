package com.cac.backend.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import lombok.Data;

import java.math.BigDecimal;
import java.time.LocalDateTime;

@Data
@TableName("cac_results")
public class CacResult {

    @TableId(type = IdType.AUTO)
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

    private String reportPath;

    private LocalDateTime createdAt;

    private LocalDateTime updatedAt;
}
