# Portable Qt configuration

## Purpose

This directory holds configuration copied into the Windows portable
application layout.

## Key file

- `qt.conf` sets `Plugins=.` so the packaged Qt executable resolves plugins
  from its application-local deployment rather than a developer Qt install.

## Related directory

See [`../README.md`](../README.md) for the Windows packaging workflow.
