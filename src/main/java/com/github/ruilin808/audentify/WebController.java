package com.github.ruilin808.audentify;

import org.springframework.stereotype.Controller;
import org.springframework.web.bind.annotation.GetMapping;

@Controller
public class WebController {
    
    @GetMapping("/")
    public String home() {
        return "forward:/index.html";
    }
    
    @GetMapping("/app")
    public String app() {
        return "forward:/index.html";
    }
}