package com.cac.backend.service;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

class OutputNamePolicyTests {

    @Test
    void acceptsAndTrimsOrdinaryPatientName() {
        assertEquals("patient_5", OutputNamePolicy.validate("  patient_5  "));
    }

    @Test
    void rejectsPathTraversalAndInvalidWindowsCharacters() {
        assertThrows(IllegalArgumentException.class, () -> OutputNamePolicy.validate(".."));
        assertThrows(IllegalArgumentException.class, () -> OutputNamePolicy.validate("patient/5"));
        assertThrows(IllegalArgumentException.class, () -> OutputNamePolicy.validate("patient:5"));
    }

    @Test
    void rejectsReservedWindowsDeviceNames() {
        assertThrows(IllegalArgumentException.class, () -> OutputNamePolicy.validate("CON"));
        assertThrows(IllegalArgumentException.class, () -> OutputNamePolicy.validate("com1.txt"));
    }
}
