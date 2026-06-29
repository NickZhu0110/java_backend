#!/usr/bin/env python3
"""Temporary SimpleITK bridge from cached medical images to raw HU/mask volumes.

This bridge is intentionally isolated from the Qt viewer architecture. It should
be replaced by native C++ ITK/DCMTK loaders once the prototype graduates.
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import zipfile
from pathlib import Path

import numpy as np

try:
    import SimpleITK as sitk
except ImportError as exc:
    raise SystemExit(
        "SimpleITK is required for temporary preprocessing. "
        "Install it with: python3 -m pip install SimpleITK"
    ) from exc


def image_metadata(image: "sitk.Image", width: int, height: int, depth: int, dtype: str) -> dict:
    return {
        "width": width,
        "height": height,
        "depth": depth,
        "dtype": dtype,
        "axis_order": "zyx",
        "spacing": list(image.GetSpacing()),
        "origin": list(image.GetOrigin()),
        "direction": list(image.GetDirection()),
    }


def extract_zip(zip_path: Path, input_volume_dir: Path) -> Path:
    target_dir = input_volume_dir / "dicom_series"
    if target_dir.exists():
        shutil.rmtree(target_dir)
    target_dir.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path, "r") as archive:
        archive.extractall(target_dir)
    return target_dir


def read_input_volume(input_volume: Path, input_volume_dir: Path) -> tuple["sitk.Image", str]:
    source = input_volume
    source_type = "unknown"
    if input_volume.is_file() and input_volume.suffix.lower() == ".zip":
        source = extract_zip(input_volume, input_volume_dir)
        source_type = "dicom_zip"

    if source.is_dir():
        reader = sitk.ImageSeriesReader()
        series_ids = reader.GetGDCMSeriesIDs(str(source))
        if not series_ids:
            raise RuntimeError(f"No DICOM series found in {source}")
        filenames = reader.GetGDCMSeriesFileNames(str(source), series_ids[0])
        if not filenames:
            raise RuntimeError(f"No DICOM files found for series {series_ids[0]} in {source}")
        reader.SetFileNames(filenames)
        return reader.Execute(), source_type if source_type != "unknown" else "dicom_directory"

    if source.is_file():
        return sitk.ReadImage(str(source)), source.suffix.lower().lstrip(".") or "medical_file"

    raise RuntimeError(f"Input volume does not exist: {input_volume}")


def write_ct_volume(image: "sitk.Image", output_dir: Path, source_type: str) -> dict:
    array = sitk.GetArrayFromImage(image)
    if array.ndim != 3:
        raise RuntimeError(f"Expected a 3D CT volume, got array shape {array.shape}")
    array = array.astype(np.int16, copy=False)

    input_volume_dir = output_dir / "input_volume"
    input_volume_dir.mkdir(parents=True, exist_ok=True)
    raw_path = input_volume_dir / "ct_volume_int16.raw"
    metadata_path = input_volume_dir / "ct_volume_metadata.json"
    array.tofile(raw_path)

    depth, height, width = array.shape
    metadata = image_metadata(image, width, height, depth, "int16")
    metadata.update(
        {
            "source_type": source_type,
            "raw_path": str(raw_path),
            "min_hu": int(array.min()),
            "max_hu": int(array.max()),
        }
    )
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    return metadata


def write_mask_volume(mask_path: Path, output_dir: Path, ct_metadata: dict) -> dict:
    if not mask_path.exists():
        raise RuntimeError(f"Mask file does not exist: {mask_path}")

    image = sitk.ReadImage(str(mask_path))
    array = sitk.GetArrayFromImage(image)
    if array.ndim != 3:
        raise RuntimeError(f"Expected a 3D mask volume, got array shape {array.shape}")
    array = (array > 0).astype(np.uint8)

    raw_path = output_dir / "mask_volume_uint8.raw"
    metadata_path = output_dir / "mask_volume_metadata.json"
    array.tofile(raw_path)

    depth, height, width = array.shape
    unique_values = sorted(int(value) for value in np.unique(array))
    dimensions_match = (
        width == ct_metadata["width"]
        and height == ct_metadata["height"]
        and depth == ct_metadata["depth"]
    )
    if not dimensions_match:
        print(
            "WARNING: mask dimensions do not match CT dimensions. "
            f"CT=({ct_metadata['width']}, {ct_metadata['height']}, {ct_metadata['depth']}), "
            f"mask=({width}, {height}, {depth})",
            file=sys.stderr,
        )

    metadata = image_metadata(image, width, height, depth, "uint8")
    metadata.update(
        {
            "raw_path": str(raw_path),
            "unique_values": unique_values,
            "matches_ct_dimensions": dimensions_match,
        }
    )
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    return metadata


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--case-dir", required=True)
    parser.add_argument("--input-volume", required=True)
    parser.add_argument("--mask", required=True)
    parser.add_argument("--output-dir", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    case_dir = Path(args.case_dir).expanduser().resolve()
    input_volume = Path(args.input_volume).expanduser().resolve()
    mask_path = Path(args.mask).expanduser().resolve()
    output_dir = Path(args.output_dir).expanduser().resolve()
    input_volume_dir = case_dir / "input_volume"

    output_dir.mkdir(parents=True, exist_ok=True)
    image, source_type = read_input_volume(input_volume, input_volume_dir)
    ct_metadata = write_ct_volume(image, output_dir, source_type)
    mask_metadata = write_mask_volume(mask_path, output_dir, ct_metadata)

    print(
        json.dumps(
            {
                "status": "ok",
                "case_dir": str(case_dir),
                "source_type": source_type,
                "ct_dimensions": [
                    ct_metadata["width"],
                    ct_metadata["height"],
                    ct_metadata["depth"],
                ],
                "mask_dimensions": [
                    mask_metadata["width"],
                    mask_metadata["height"],
                    mask_metadata["depth"],
                ],
                "mask_matches_ct_dimensions": mask_metadata["matches_ct_dimensions"],
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
