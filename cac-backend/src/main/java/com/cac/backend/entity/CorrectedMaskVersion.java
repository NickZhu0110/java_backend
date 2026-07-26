package com.cac.backend.entity;

import com.baomidou.mybatisplus.annotation.IdType;
import com.baomidou.mybatisplus.annotation.TableId;
import com.baomidou.mybatisplus.annotation.TableName;
import lombok.Data;

import java.time.LocalDateTime;

@Data
@TableName("corrected_mask_versions")
public class CorrectedMaskVersion {

    @TableId(type = IdType.AUTO)
    private Long id;

    private Long jobId;

    private Integer version;

    private String maskPath;

    private String metadataPath;

    private String checksumSha256;

    private LocalDateTime createdAt;
}
