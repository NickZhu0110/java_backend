package com.cac.backend.dto;

import lombok.Data;

@Data
public class CorrectedMaskUploadResponse {

    private Long jobId;

    private Integer version;

    private String correctedMaskPath;

    private String correctedMaskMetadataPath;

    private Long correctedMaskBytes;

    private Long metadataBytes;
}
