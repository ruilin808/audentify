package com.github.ruilin808.audentify;

import org.springframework.web.bind.annotation.*;
import org.springframework.web.client.RestTemplate;
import org.springframework.http.*;
import java.util.Map;
import java.util.HashMap;

@RestController
@CrossOrigin(origins = "*") // Allow all origins for development
public class AudioController {

    private final RestTemplate restTemplate = new RestTemplate();

    @GetMapping("/test")
    public String test() {
        return "Spring Boot is running!";
    }

    @PostMapping("/process")
    public Map processAudio(@RequestBody Map<String, String> request) {
        try {
            // Extract URL from simple request
            String url = request.get("url");
            System.out.println("Processing URL: " + url);
            
            // Create full request with default parameters for microservice 1
            Map<String, Object> fullRequest = new HashMap<>();
            fullRequest.put("url", url);
            fullRequest.put("output_format", "wav");
            fullRequest.put("sample_rate", 44100);
            fullRequest.put("keep_intermediate", false);
            fullRequest.put("channels", 2);
            
            // Step 1: Get audio from microservice 1
            System.out.println("Calling microservice 1...");
            HttpHeaders headers1 = new HttpHeaders();
            headers1.setContentType(MediaType.APPLICATION_JSON);
            HttpEntity<Map<String, Object>> entity1 = new HttpEntity<>(fullRequest, headers1);
            
            byte[] audioBytes = restTemplate.postForObject(
                "http://localhost:8000/process-url/stream", 
                entity1, 
                byte[].class
            );
            
            System.out.println("Got audio bytes: " + (audioBytes != null ? audioBytes.length : "null"));
            
            // Step 2: Send to microservice 2 for recognition
            System.out.println("Calling microservice 2...");
            HttpHeaders headers2 = new HttpHeaders();
            headers2.setContentType(MediaType.parseMediaType("audio/wav"));
            HttpEntity<byte[]> entity2 = new HttpEntity<>(audioBytes, headers2);
            
            Map result = restTemplate.postForObject(
                "http://localhost:8080/recognize/stream", 
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
            return errorResponse;
        }
    }
}