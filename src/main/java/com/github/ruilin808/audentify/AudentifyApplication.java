package com.github.ruilin808.audentify;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.context.annotation.ComponentScan;

@SpringBootApplication
@ComponentScan(basePackages = "com.github.ruilin808.audentify")

public class AudentifyApplication {

	public static void main(String[] args) {
		SpringApplication.run(AudentifyApplication.class, args);
	}

}
