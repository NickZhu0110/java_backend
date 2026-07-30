package com.cac.backend.service;

import java.util.Set;
import java.util.regex.Pattern;

final class OutputNamePolicy {

    private static final int MAX_LENGTH = 100;
    private static final Pattern INVALID_CHARACTERS =
            Pattern.compile("[<>:\"/\\\\|?*\\x00-\\x1F]");
    private static final Pattern RESERVED_DEVICE_NAME =
            Pattern.compile(
                    "^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$",
                    Pattern.CASE_INSENSITIVE);
    private static final Set<String> DOT_NAMES = Set.of(".", "..");

    private OutputNamePolicy() {
    }

    static String validate(String value) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException("Enter an output name.");
        }

        String name = value.trim();
        if (name.length() > MAX_LENGTH) {
            throw new IllegalArgumentException(
                    "The output name must be " + MAX_LENGTH + " characters or fewer.");
        }
        if (DOT_NAMES.contains(name)
                || name.endsWith(".")
                || name.endsWith(" ")
                || INVALID_CHARACTERS.matcher(name).find()
                || RESERVED_DEVICE_NAME.matcher(name).matches()) {
            throw new IllegalArgumentException(
                    "The output name is not a valid Windows folder name.");
        }
        return name;
    }
}
