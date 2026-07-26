#!/usr/bin/env python3
"""Recalculate CAC score from a corrected mask using SEGMENT-CACS scoring.

This script does not run model inference. It imports the official scoring
functions from SEGMENT-CACS and applies them to the original CT HU volume plus
the corrected mask.
"""

from __future__ import annotations

import argparse
import importlib
import json
import os
import sys
from pathlib import Path
from typing import Any

import numpy as np


DEFAULT_SEGMENTCACS_SRC = os.getenv("SEGMENTCACS_SRC")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Recalculate CAC score from corrected mask using SEGMENT-CACS scoring")
    parser.add_argument("--input", "--input-volume", dest="input_volume", required=True, help="Trusted backend job input path")
    parser.add_argument("--mask", "--corrected-mask", dest="corrected_mask", required=True, help="Corrected mask path, raw uint8 or medical image")
    parser.add_argument("--metadata", help="Corrected raw mask metadata JSON path")
    parser.add_argument("--output", required=True, help="Output recalculation JSON path")
    parser.add_argument("--segmentcacs-src", default=DEFAULT_SEGMENTCACS_SRC, help="SEGMENT-CACS src directory")
    parser.add_argument("--export-mask", help="Optional user-visible corrected mask NRRD path")
    return parser.parse_args()


def import_segmentcacs_scoring(segmentcacs_src: Path):
    sys.path.insert(0, str(segmentcacs_src))
    module = importlib.import_module("segment_cacs_predict")
    return module.computeAgatstonArtery


def read_ct_volume(path: Path):
    import SimpleITK as sitk

    if path.is_dir():
        reader = sitk.ImageSeriesReader()
        series_ids = reader.GetGDCMSeriesIDs(str(path))
        if not series_ids:
            raise RuntimeError(f"No DICOM series found in input directory: {path}")
        files = reader.GetGDCMSeriesFileNames(str(path), series_ids[0])
        reader.SetFileNames(files)
        image_sitk = reader.Execute()
    else:
        image_sitk = sitk.ReadImage(str(path))

    image_org = sitk.GetArrayFromImage(image_sitk)
    spacing = image_sitk.GetSpacing()
    return image_org, spacing, image_sitk


def write_mask_image(mask: np.ndarray, reference_image, output_path: Path) -> None:
    import SimpleITK as sitk

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_image = sitk.GetImageFromArray(mask.astype(np.uint8, copy=False))
    output_image.CopyInformation(reference_image)
    sitk.WriteImage(output_image, str(output_path), True)


def read_mask(mask_path: Path, metadata_path: Path | None) -> tuple[np.ndarray, dict[str, Any]]:
    if metadata_path is not None:
        return read_raw_mask(mask_path, metadata_path)
    return read_image_mask(mask_path)


def read_raw_mask(mask_path: Path, metadata_path: Path) -> tuple[np.ndarray, dict[str, Any]]:
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    width = int(metadata["width"])
    height = int(metadata["height"])
    depth = int(metadata["depth"])
    dtype = str(metadata.get("dtype", "uint8"))
    axis_order = str(metadata.get("axis_order", "zyx"))
    if dtype != "uint8":
        raise RuntimeError(f"Unsupported corrected mask dtype: {dtype}")
    if axis_order != "zyx":
        raise RuntimeError(f"Unsupported corrected mask axis_order: {axis_order}")

    data = np.fromfile(mask_path, dtype=np.uint8)
    expected = width * height * depth
    if data.size != expected:
        raise RuntimeError(f"Corrected mask voxel count mismatch: got {data.size}, expected {expected}")

    mask = data.reshape((depth, height, width))
    metadata["reader"] = "raw_uint8_metadata"
    return mask, metadata


def read_image_mask(mask_path: Path) -> tuple[np.ndarray, dict[str, Any]]:
    import SimpleITK as sitk

    mask_sitk = sitk.ReadImage(str(mask_path))
    mask = sitk.GetArrayFromImage(mask_sitk).astype(np.uint8, copy=False)
    metadata = {
        "reader": "SimpleITK",
        "spacing": list(mask_sitk.GetSpacing()),
        "origin": list(mask_sitk.GetOrigin()),
        "direction": list(mask_sitk.GetDirection()),
    }
    return mask, metadata


def validate_shapes(image_org: np.ndarray, mask: np.ndarray) -> None:
    if image_org.shape != mask.shape:
        raise RuntimeError(f"CT/mask dimensions differ: image={image_org.shape}, mask={mask.shape}")


def mask_value_summary(mask: np.ndarray) -> dict[str, Any]:
    values = np.unique(mask)
    values_list = [int(value) for value in values[:64]]
    is_binary = all(value in (0, 1) for value in values_list) and len(values) <= 2
    return {
        "uniqueValues": values_list,
        "uniqueValueCount": int(values.size),
        "isBinary": bool(is_binary),
        "compatibleWithSegmentCacs": True,
        "note": (
            "Binary mask is accepted for total Agatston score because SEGMENT-CACS binarizes mask > 0 "
            "inside computeAgatston(). Segment-level scores require SEGMENT-CACS artery labels."
            if is_binary else
            "Multi-label mask can produce SEGMENT-CACS segment-level scores when labels match upstream artery labels."
        ),
    }


def scoring_input_diagnostics(image_org: np.ndarray, mask: np.ndarray) -> dict[str, Any]:
    mask_positive = mask > 0
    eligible = mask_positive & (image_org >= 130)
    positive_count = int(mask_positive.sum())
    eligible_count = int(eligible.sum())
    per_slice = []
    for slice_index in range(mask.shape[0]):
        slice_positive = int(mask_positive[slice_index].sum())
        slice_eligible = int(eligible[slice_index].sum())
        if slice_positive > 0 or slice_eligible > 0:
            per_slice.append(
                {
                    "sliceIndex": int(slice_index),
                    "maskNonzeroVoxelCount": slice_positive,
                    "eligibleVoxelCountHU130": slice_eligible,
                }
            )

    if positive_count > 0:
        hu_inside = image_org[mask_positive]
        max_hu = int(hu_inside.max())
        min_hu = int(hu_inside.min())
    else:
        max_hu = None
        min_hu = None

    return {
        "maskNonzeroVoxelCount": positive_count,
        "eligibleVoxelCountHU130": eligible_count,
        "maxHUInsideMask": max_hu,
        "minHUInsideMask": min_hu,
        "perSliceEligibleCounts": per_slice,
    }


def official_grade(raw_official: dict[str, Any]) -> str:
    grading = raw_official.get("Grading")
    if isinstance(grading, (list, tuple)) and len(grading) > 1:
        return str(grading[1])
    return "unknown"


def build_result(
    raw_official: dict[str, Any],
    input_path: Path,
    mask_path: Path,
    metadata_path: Path | None,
    segmentcacs_src: Path,
    spacing,
    image_shape,
    mask_shape,
    mask_summary: dict[str, Any],
    mask_metadata: dict[str, Any],
) -> dict[str, Any]:
    score = float(raw_official.get("AgatstonScore", 0.0))
    result = dict(raw_official)
    result.update(
        {
            "agatstonScore": score,
            "riskGrade": official_grade(raw_official),
            "source": "corrected_mask",
            "method": "segment_cacs_computeAgatstonArtery",
            "usedOfficialSegmentCacsScoring": True,
            "modelInferenceSkipped": True,
            "inputPath": str(input_path),
            "maskPath": str(mask_path),
            "metadataPath": str(metadata_path) if metadata_path else None,
            "segmentcacsSrc": str(segmentcacs_src),
            "spacing": [float(value) for value in spacing],
            "imageShape": [int(value) for value in image_shape],
            "maskShape": [int(value) for value in mask_shape],
            "maskSummary": mask_summary,
            "maskMetadata": mask_metadata,
            "officialRawResult": raw_official,
        }
    )
    return result


def main() -> int:
    args = parse_args()
    input_path = Path(args.input_volume).expanduser().resolve()
    mask_path = Path(args.corrected_mask).expanduser().resolve()
    metadata_path = Path(args.metadata).expanduser().resolve() if args.metadata else None
    output_path = Path(args.output).expanduser().resolve()
    if not args.segmentcacs_src:
        raise RuntimeError("SEGMENT-CACS src directory is required.")
    segmentcacs_src = Path(args.segmentcacs_src).expanduser().resolve()

    compute_agatston_artery = import_segmentcacs_scoring(segmentcacs_src)
    image_org, spacing, reference_image = read_ct_volume(input_path)
    mask, mask_metadata = read_mask(mask_path, metadata_path)
    validate_shapes(image_org, mask)
    export_mask_path = Path(args.export_mask).expanduser().resolve() if args.export_mask else None
    if export_mask_path is not None:
        write_mask_image(mask, reference_image, export_mask_path)

    summary = mask_value_summary(mask)
    input_diagnostics = scoring_input_diagnostics(image_org, mask)
    raw_official = compute_agatston_artery(image_org, mask, spacing=spacing)
    result = build_result(
        raw_official=raw_official,
        input_path=input_path,
        mask_path=mask_path,
        metadata_path=metadata_path,
        segmentcacs_src=segmentcacs_src,
        spacing=spacing,
        image_shape=image_org.shape,
        mask_shape=mask.shape,
        mask_summary=summary,
        mask_metadata=mask_metadata,
    )
    result.update(input_diagnostics)
    result["exportedCorrectedMaskPath"] = str(export_mask_path) if export_mask_path else None

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({
        "output": str(output_path),
        "agatstonScore": result["agatstonScore"],
        "riskGrade": result["riskGrade"],
        "maskNonzeroVoxelCount": result["maskNonzeroVoxelCount"],
        "eligibleVoxelCountHU130": result["eligibleVoxelCountHU130"],
        "maxHUInsideMask": result["maxHUInsideMask"],
        "usedOfficialSegmentCacsScoring": True,
        "modelInferenceSkipped": True,
        "exportedCorrectedMaskPath": result["exportedCorrectedMaskPath"],
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
