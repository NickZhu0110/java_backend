package com.cac.backend.config;

import com.cac.backend.websocket.JobWebSocketHandler;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

@Configuration
@EnableWebSocket
public class WebSocketConfig implements WebSocketConfigurer {

    private static final Logger log = LoggerFactory.getLogger(WebSocketConfig.class);

    private final JobWebSocketHandler jobWebSocketHandler;

    public WebSocketConfig(JobWebSocketHandler jobWebSocketHandler) {
        this.jobWebSocketHandler = jobWebSocketHandler;
    }

    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
        registry.addHandler(jobWebSocketHandler, "/ws/jobs")
                .setAllowedOrigins("*");

        log.info("Registered WebSocket endpoint: /ws/jobs");
    }
}
