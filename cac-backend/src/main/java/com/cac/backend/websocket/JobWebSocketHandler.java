package com.cac.backend.websocket;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.socket.CloseStatus;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;

import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

@Component
public class JobWebSocketHandler extends TextWebSocketHandler {

    private static final Logger log = LoggerFactory.getLogger(JobWebSocketHandler.class);

    private final Set<WebSocketSession> sessions = ConcurrentHashMap.newKeySet();
    private final ObjectMapper objectMapper;

    public JobWebSocketHandler(ObjectMapper objectMapper) {
        this.objectMapper = objectMapper;
    }

    @Override
    public void afterConnectionEstablished(WebSocketSession session) {
        sessions.add(session);
        log.info("WebSocket connected: sessionId={}, activeSessions={}", session.getId(), sessions.size());
    }

    @Override
    public void afterConnectionClosed(WebSocketSession session, CloseStatus status) {
        sessions.remove(session);
        log.info(
                "WebSocket closed: sessionId={}, status={}, activeSessions={}",
                session.getId(),
                status,
                sessions.size()
        );
    }

    @Override
    public void handleTransportError(WebSocketSession session, Throwable exception) {
        sessions.remove(session);
        log.warn("WebSocket transport error: sessionId={}", session.getId(), exception);
    }

    public void broadcastJobStatus(Long jobId, String status, Integer progress) {
        Map<String, Object> message = new LinkedHashMap<>();
        message.put("type", "JOB_STATUS");
        message.put("jobId", jobId);
        message.put("status", status);
        message.put("progress", progress);

        try {
            String json = objectMapper.writeValueAsString(message);
            for (WebSocketSession session : sessions) {
                sendMessage(session, json);
            }
        } catch (IOException e) {
            log.warn("Failed to serialize WebSocket job status message: jobId={}", jobId, e);
        }
    }

    private void sendMessage(WebSocketSession session, String json) {
        if (!session.isOpen()) {
            sessions.remove(session);
            return;
        }

        try {
            synchronized (session) {
                session.sendMessage(new TextMessage(json));
            }
        } catch (IOException e) {
            sessions.remove(session);
            log.warn("Failed to send WebSocket message: sessionId={}", session.getId(), e);
        }
    }
}
