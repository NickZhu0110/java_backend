# Maven Wrapper Configuration

## Purpose

This directory pins the Maven distribution used by `mvnw` and `mvnw.cmd`.
It allows contributors to build the backend without relying on an arbitrary system Maven version.

## Key file

- `maven-wrapper.properties` selects Maven 3.9.16 and wrapper script behavior.

## Entry points

- Run `.\mvnw.cmd test` or `.\mvnw.cmd package` from `cac-backend` on Windows.
- Run `./mvnw test` or `./mvnw package` on supported Unix-like systems.

## Related directory

- [`../..`](../../README.md) documents the backend module and produced JAR.
