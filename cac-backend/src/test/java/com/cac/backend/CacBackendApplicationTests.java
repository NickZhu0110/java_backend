package com.cac.backend;

import org.junit.jupiter.api.Test;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.ActiveProfiles;

@ActiveProfiles("local-windows")
@SpringBootTest(properties = {
		"spring.datasource.url=jdbc:h2:mem:cac-context-test;DB_CLOSE_DELAY=-1",
		"server.port=0"
})
class CacBackendApplicationTests {

	@Test
	void contextLoads() {
	}

}
