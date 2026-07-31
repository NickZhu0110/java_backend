# Viewer Data Models

## Purpose
This directory contains the small in-memory CT and binary-mask data structures shared by the native viewer.
Both models use X-fastest storage with offset `((z * height) + y) * width + x`.

## Key files and entry points
- `VolumeData.h`: stores signed 16-bit CT voxels with dimensions, spacing, origin, and direction.
- `MaskVolume.h`: stores an unsigned 8-bit binary mask with matching geometry.
- `VolumeData::value`: provides checked CT voxel access.
- `MaskVolume::setValue`: normalizes every nonzero value to `1`.

## Related directory
- [`../viewer`](../viewer) loads, renders, samples, and edits these models.
