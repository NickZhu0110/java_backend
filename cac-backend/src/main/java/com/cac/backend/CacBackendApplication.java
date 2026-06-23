package com.cac.backend;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.mybatis.spring.annotation.MapperScan;

@MapperScan("com.cac.backend.mapper")
@SpringBootApplication
public class CacBackendApplication {

	public static void main(String[] args) {
		SpringApplication.run(CacBackendApplication.class, args);
	}

}
