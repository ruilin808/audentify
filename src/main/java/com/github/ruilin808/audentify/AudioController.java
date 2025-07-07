package com.github.ruilin808.audentify;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.client.RestTemplate;
import org.springframework.http.*;
import java.util.Map;
import java.util.HashMap;

@RestController
@CrossOrigin(origins = "*") // Allow all origins for development
public class AudioController {

    private final RestTemplate restTemplate = new RestTemplate();

    // Inject microservice URLs from configuration
    @Value("${microservice1.url:http://localhost:8000}")
    private String microservice1Url;

    @Value("${microservice2.url:http://localhost:8080}")
    private String microservice2Url;

    @GetMapping("/test")
    public String test() {
        return "Spring Boot is running!";
    }

    @GetMapping("/config")
    public Map<String, String> getConfig() {
        Map<String, String> config = new HashMap<>();
        config.put("microservice1Url", microservice1Url);
        config.put("microservice2Url", microservice2Url);
        config.put("status", "running");
        return config;
    }

    @PostMapping("/process")
    public Map processAudio(@RequestBody Map<String, String> request) {
        try {
            // Extract URL from simple request
            String url = request.get("url");
            System.out.println("Processing URL: " + url);
            System.out.println("Using microservice1: " + microservice1Url);
            System.out.println("Using microservice2: " + microservice2Url);
            
            // Create simplified request for the new endpoint
            Map<String, Object> audioRequest = new HashMap<>();
            audioRequest.put("url", url);
            
            // Step 1: Get processed audio from microservice 1 using advanced vocal preservation
            System.out.println("Calling microservice 1 - removing speech while preserving vocals...");
            HttpHeaders headers1 = new HttpHeaders();
            headers1.setContentType(MediaType.APPLICATION_JSON);
            HttpEntity<Map<String, Object>> entity1 = new HttpEntity<>(audioRequest, headers1);
            
            byte[] audioBytes = restTemplate.postForObject(
                microservice1Url + "/remove-speech-preserve-vocals", 
                entity1, 
                byte[].class
            );
            
            System.out.println("Got processed audio bytes: " + (audioBytes != null ? audioBytes.length : "null"));
            
            // Step 2: Send processed audio to microservice 2 for recognition
            System.out.println("Calling microservice 2 for audio recognition...");
            HttpHeaders headers2 = new HttpHeaders();
            headers2.setContentType(MediaType.parseMediaType("audio/wav"));
            HttpEntity<byte[]> entity2 = new HttpEntity<>(audioBytes, headers2);
            
            Map result = restTemplate.postForObject(
                microservice2Url + "/recognize/stream", 
                entity2, 
                Map.class
            );
            
            System.out.println("Final result: " + result);
            return result;
            
        } catch (Exception e) {
            System.err.println("Error occurred: " + e.getMessage());
            e.printStackTrace();
            
            Map<String, Object> errorResponse = new HashMap<>();
            errorResponse.put("error", "Processing failed");
            errorResponse.put("message", e.getMessage());
            errorResponse.put("success", false);
            errorResponse.put("microservice1Url", microservice1Url);
            errorResponse.put("microservice2Url", microservice2Url);
            return errorResponse;
        }
    }
    
    // Optional: Add additional endpoints for other processing methods
    @PostMapping("/process/instrumental")
    public Map processInstrumental(@RequestBody Map<String, String> request) {
        try {
            String url = request.get("url");
            System.out.println("Extracting instrumental from URL: " + url);
            System.out.println("Using microservice1: " + microservice1Url);
            System.out.println("Using microservice2: " + microservice2Url);
            
            Map<String, Object> audioRequest = new HashMap<>();
            audioRequest.put("url", url);
            
            HttpHeaders headers1 = new HttpHeaders();
            headers1.setContentType(MediaType.APPLICATION_JSON);
            HttpEntity<Map<String, Object>> entity1 = new HttpEntity<>(audioRequest, headers1);
            
            byte[] audioBytes = restTemplate.postForObject(
                microservice1Url + "/extract-instrumental", 
                entity1, 
                byte[].class
            );
            
            System.out.println("Got instrumental audio bytes: " + (audioBytes != null ? audioBytes.length : "null"));
            
            HttpHeaders headers2 = new HttpHeaders();
            headers2.setContentType(MediaType.parseMediaType("audio/wav"));
            HttpEntity<byte[]> entity2 = new HttpEntity<>(audioBytes, headers2);
            
            Map result = restTemplate.postForObject(
                microservice2Url + "/recognize/stream", 
                entity2, 
                Map.class
            );
            
            System.out.println("Instrumental recognition result: " + result);
            return result;
            
        } catch (Exception e) {
            System.err.println("Error in instrumental processing: " + e.getMessage());
            e.printStackTrace();
            
            Map<String, Object> errorResponse = new HashMap<>();
            errorResponse.put("error", "Instrumental processing failed");
            errorResponse.put("message", e.getMessage());
            errorResponse.put("success", false);
            errorResponse.put("microservice1Url", microservice1Url);
            errorResponse.put("microservice2Url", microservice2Url);
            return errorResponse;
        }
    }
    
    @PostMapping("/process/simple")
    public Map processSimple(@RequestBody Map<String, String> request) {
        try {
            String url = request.get("url");
            System.out.println("Simple speech removal from URL: " + url);
            System.out.println("Using microservice1: " + microservice1Url);
            System.out.println("Using microservice2: " + microservice2Url);
            
            Map<String, Object> audioRequest = new HashMap<>();
            audioRequest.put("url", url);
            
            HttpHeaders headers1 = new HttpHeaders();
            headers1.setContentType(MediaType.APPLICATION_JSON);
            HttpEntity<Map<String, Object>> entity1 = new HttpEntity<>(audioRequest, headers1);
            
            byte[] audioBytes = restTemplate.postForObject(
                microservice1Url + "/remove-commentary", 
                entity1, 
                byte[].class
            );
            
            System.out.println("Got simple processed audio bytes: " + (audioBytes != null ? audioBytes.length : "null"));
            
            HttpHeaders headers2 = new HttpHeaders();
            headers2.setContentType(MediaType.parseMediaType("audio/wav"));
            HttpEntity<byte[]> entity2 = new HttpEntity<>(audioBytes, headers2);
            
            Map result = restTemplate.postForObject(
                microservice2Url + "/recognize/stream", 
                entity2, 
                Map.class
            );
            
            System.out.println("Simple processing result: " + result);
            return result;
            
        } catch (Exception e) {
            System.err.println("Error in simple processing: " + e.getMessage());
            e.printStackTrace();
            
            Map<String, Object> errorResponse = new HashMap<>();
            errorResponse.put("error", "Simple processing failed");
            errorResponse.put("message", e.getMessage());
            errorResponse.put("success", false);
            errorResponse.put("microservice1Url", microservice1Url);
            errorResponse.put("microservice2Url", microservice2Url);
            return errorResponse;
        }
    }

    // Health check endpoint
    @GetMapping("/health")
    public Map<String, Object> health() {
        Map<String, Object> health = new HashMap<>();
        health.put("status", "UP");
        health.put("microservice1", microservice1Url);
        health.put("microservice2", microservice2Url);
        
        // Test connectivity to microservices
        try {
            restTemplate.getForObject(microservice1Url + "/", String.class);
            health.put("microservice1Status", "UP");
        } catch (Exception e) {
            health.put("microservice1Status", "DOWN");
            health.put("microservice1Error", e.getMessage());
        }
        
        try {
            restTemplate.getForObject(microservice2Url + "/health", Object.class);
            health.put("microservice2Status", "UP");
        } catch (Exception e) {
            health.put("microservice2Status", "DOWN");
            health.put("microservice2Error", e.getMessage());
        }
        
        return health;
    }
}