package com.cac.backend.controller;

import com.cac.backend.service.CacResultService;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.core.io.Resource;
import org.springframework.http.ContentDisposition;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.servlet.mvc.method.annotation.StreamingResponseBody;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Comparator;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

@RestController
@RequestMapping("/api/jobs/{jobId}/files")
public class JobFileController {

    private static final Logger log = LoggerFactory.getLogger(JobFileController.class);

    private final CacResultService cacResultService;

    public JobFileController(CacResultService cacResultService) {
        this.cacResultService = cacResultService;
    }

    @GetMapping("/ai-mask")
    public ResponseEntity<Resource> downloadAiMask(@PathVariable Long jobId) throws IOException {
        Resource resource = cacResultService.loadAiMaskResource(jobId);
        long contentLength = resource.contentLength();
        log.info("AI mask download success: jobId={}, bytes={}", jobId, contentLength);

        return ResponseEntity.ok()
                .contentType(MediaType.APPLICATION_OCTET_STREAM)
                .contentLength(contentLength)
                .header(HttpHeaders.CONTENT_DISPOSITION,
                        ContentDisposition.attachment()
                                .filename("ai_mask_v0.nrrd")
                                .build()
                                .toString())
                .body(resource);
    }

    @GetMapping("/input-volume")
    public ResponseEntity<StreamingResponseBody> downloadInputVolume(@PathVariable Long jobId) throws IOException {
        Path path = cacResultService.resolveInputVolumePath(jobId);
        if (Files.isDirectory(path)) {
            StreamingResponseBody body = outputStream -> writeDirectoryZip(path, outputStream);
            log.info("Input volume download success: jobId={}, path={}, mode=zip-directory", jobId, path);
            return ResponseEntity.ok()
                    .contentType(MediaType.parseMediaType("application/zip"))
                    .header(HttpHeaders.CONTENT_DISPOSITION,
                            ContentDisposition.attachment()
                                    .filename(String.format("input_volume_job_%d.zip", jobId))
                                    .build()
                                    .toString())
                    .body(body);
        }

        long contentLength = Files.size(path);
        String filename = path.getFileName() == null
                ? String.format("input_volume_job_%d", jobId)
                : path.getFileName().toString();
        log.info("Input volume download success: jobId={}, path={}, bytes={}, mode=single-file",
                jobId, path, contentLength);

        StreamingResponseBody body = outputStream -> Files.copy(path, outputStream);
        return ResponseEntity.ok()
                .contentType(MediaType.APPLICATION_OCTET_STREAM)
                .contentLength(contentLength)
                .header(HttpHeaders.CONTENT_DISPOSITION,
                        ContentDisposition.attachment()
                                .filename(filename)
                                .build()
                                .toString())
                .body(body);
    }

    private void writeDirectoryZip(Path directory, OutputStream outputStream) throws IOException {
        try (ZipOutputStream zipOutputStream = new ZipOutputStream(outputStream);
             var stream = Files.walk(directory)) {
            stream.filter(Files::isRegularFile)
                    .sorted(Comparator.comparing(Path::toString))
                    .forEach(file -> {
                        Path relative = directory.relativize(file).normalize();
                        try {
                            zipOutputStream.putNextEntry(new ZipEntry(relative.toString()));
                            Files.copy(file, zipOutputStream);
                            zipOutputStream.closeEntry();
                        } catch (IOException e) {
                            throw new RuntimeException("Failed to zip input volume file: " + file, e);
                        }
                    });
        }
    }
}
