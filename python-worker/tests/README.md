# Python tests

## Purpose

This directory contains synthetic automated tests for the experimental VMTK
vessel pipeline; no patient data belongs here.

## Key file and command

- `test_vessel_straightening.py` covers VMTK runtime allocation, component
  ranking, geometry mapping, candidate filtering, errors, and two-stage output.
- Run: `<vmtk-python> -m pytest python-worker/tests/test_vessel_straightening.py -q`
- The current parameterized suite collects nine tests.

## Related directory

See [`../vessel_straightening/`](../vessel_straightening/). These tests do not
clinically validate CPR or replace Qt visual acceptance.
