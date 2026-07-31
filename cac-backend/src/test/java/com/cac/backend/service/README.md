# Backend Service Tests

## Purpose

This directory contains focused unit tests for package-private service behavior.

## Key file and coverage

- `OutputNamePolicyTests.java` exercises trimming and rejection of path traversal, invalid characters, and Windows reserved device names.
- The tests do not exercise job lifecycle, ProcessBuilder execution, result persistence, corrected-mask upload, or recalculation.
- They use synthetic folder names and do not require medical data or external services.

## Entry point

- Run all backend tests with `.\mvnw.cmd test` from `cac-backend`.
- Run only this class with `.\mvnw.cmd -Dtest=OutputNamePolicyTests test`.

## Related directory

- [`../../../../../../main/java/com/cac/backend/service`](../../../../../../main/java/com/cac/backend/service/README.md) contains the code; [`..`](../README.md) covers tests.
