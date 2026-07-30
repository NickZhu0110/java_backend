package com.cac.backend.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import lombok.Data;

import java.time.LocalDateTime;

@Data
@TableName("analysis_jobs")
public class AnalysisJob {

    @TableId(type = IdType.AUTO)
    private Long id;

    private String modelName;

    private String status;

    private Integer progress;

    private String inputPath;

    private String outputPath;

    private String outputName;

    private LocalDateTime startedAt;

    private LocalDateTime finishedAt;

    private String errorMessage;

    private String workerId;

    private String device;

    private LocalDateTime createdAt;

    private LocalDateTime updatedAt;
}
