package com.cac.backend.service;

import com.cac.backend.dto.AnalysisRequestedEvent;
import com.cac.backend.entity.AnalysisJob;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.boot.autoconfigure.condition.ConditionalOnProperty;
import org.springframework.kafka.core.KafkaTemplate;
import org.springframework.stereotype.Service;

@Service
@ConditionalOnProperty(
        name = "cac.execution.mode",
        havingValue = "kafka",
        matchIfMissing = true
)
public class AnalysisEventProducer implements AnalysisDispatcher {

    private static final Logger log = LoggerFactory.getLogger(AnalysisEventProducer.class);

    private static final String ANALYSIS_REQUESTED_TOPIC = "cac.analysis.requested";

    private final KafkaTemplate<String, AnalysisRequestedEvent> kafkaTemplate;

    public AnalysisEventProducer(KafkaTemplate<String, AnalysisRequestedEvent> kafkaTemplate) {
        this.kafkaTemplate = kafkaTemplate;
    }

    @Override
    public void dispatch(AnalysisJob job, com.cac.backend.dto.CreateJobRequest request) {
        publishAnalysisRequested(job);
    }

    public void publishAnalysisRequested(AnalysisJob job) {
        AnalysisRequestedEvent event = new AnalysisRequestedEvent(
                job.getId(),
                job.getModelName(),
                job.getInputPath(),
                job.getOutputPath()
        );

        kafkaTemplate.send(
                ANALYSIS_REQUESTED_TOPIC,
                String.valueOf(job.getId()),
                event
        ).whenComplete((result, ex) -> {
            if (ex != null) {
                log.error("Failed to publish Kafka event for job {}", job.getId(), ex);
            } else {
                log.info("Published Kafka event to {} for job {}",
                        ANALYSIS_REQUESTED_TOPIC,
                        job.getId());
            }
        });
    }
}
