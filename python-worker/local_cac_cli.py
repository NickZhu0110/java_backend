#!/usr/bin/env python3
"""Authoritative local Windows SEGMENT-CACS command-line entry point."""

from __future__ import annotations

import argparse
import contextlib
import json
import math
import sys
import time
import traceback
from pathlib import Path
from typing import Any

import numpy as np
import SimpleITK as sitk


GEOMETRY_TOLERANCE = 1e-5


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run one local DICOM series through the official SEGMENT-CACS model."
    )
    parser.add_argument("--input-series", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--segmentcacs-root", required=True, type=Path)
    parser.add_argument("--model", required=True, type=Path)
    parser.add_argument("--device", choices=("cpu",), default="cpu")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    input_series = args.input_series.expanduser().resolve()
    output_dir = args.output_dir.expanduser().resolve()
    segmentcacs_root = args.segmentcacs_root.expanduser().resolve()
    model_path = args.model.expanduser().resolve()
    started = time.perf_counter()

    try:
        validate_paths(input_series, output_dir, segmentcacs_root, model_path)
        output_dir.mkdir(parents=True, exist_ok=True)

        print("stage=PREPARING_INPUT", flush=True)
        ct_image, slice_count = read_one_dicom_series(input_series)
        ct_dir = output_dir / "input"
        ct_dir.mkdir(parents=True, exist_ok=True)
        ct_path = ct_dir / "ct.nrrd"
        sitk.WriteImage(ct_image, str(ct_path), True)
        require_readable_image(ct_path, "Prepared CT")

        print(f"dicom_slices={slice_count}", flush=True)
        print("stage=RUNNING_INFERENCE", flush=True)
        run_official_model(
            segmentcacs_root=segmentcacs_root,
            model_path=model_path,
            prepared_input_dir=ct_dir,
            output_dir=output_dir,
        )

        print("stage=VALIDATING_OUTPUT", flush=True)
        result_path = output_dir / "result.json"
        result = read_json_object(result_path)
        files = result.get("files")
        if not isinstance(files, dict):
            raise RuntimeError("result.json does not contain a files object.")

        mask_path = resolve_managed_artifact(
            output_dir, files.get("originalMask"), "AI mask"
        )
        ct_validation_image = require_readable_image(ct_path, "CT volume")
        mask_image = require_readable_image(mask_path, "AI mask")
        validation = validate_geometry(ct_validation_image, mask_image, slice_count)

        files["ctVolume"] = ct_path.relative_to(output_dir).as_posix()
        result["files"] = files
        result["validation"] = validation
        result["runtime"] = {
            "python": sys.version.split()[0],
            "device": "cpu",
            "wallClockSeconds": round(time.perf_counter() - started, 3),
        }
        write_json_atomic(result_path, result)

        print(f"ct_size={format_values(validation['ctSize'])}", flush=True)
        print(f"ct_spacing={format_values(validation['ctSpacing'])}", flush=True)
        print(f"mask_size={format_values(validation['maskSize'])}", flush=True)
        print(f"mask_spacing={format_values(validation['maskSpacing'])}", flush=True)
        print(f"foreground_voxels={validation['foregroundVoxelCount']}", flush=True)
        print(f"wall_clock_seconds={result['runtime']['wallClockSeconds']}", flush=True)
        print("stage=COMPLETED", flush=True)
        return 0
    except Exception as exception:
        output_dir.mkdir(parents=True, exist_ok=True)
        safe_message = sanitize_error(
            exception,
            input_series=input_series,
            segmentcacs_root=segmentcacs_root,
            model_path=model_path,
        )
        write_json_atomic(
            output_dir / "cli_error.json",
            {"status": "FAILED", "error": safe_message},
        )
        print(f"Local SEGMENT-CACS failed: {safe_message}", file=sys.stderr)
        return 1


def validate_paths(
    input_series: Path,
    output_dir: Path,
    segmentcacs_root: Path,
    model_path: Path,
) -> None:
    if not input_series.is_dir():
        raise FileNotFoundError("The selected DICOM series directory does not exist.")
    if output_dir == input_series or output_dir.is_relative_to(input_series):
        raise ValueError("The managed output directory cannot be inside the DICOM directory.")
    if not segmentcacs_root.is_dir():
        raise FileNotFoundError("The configured SEGMENT-CACS root does not exist.")
    prediction_script = segmentcacs_root / "src" / "segment_cacs_predict.py"
    wrapper_root = segmentcacs_root / "model-service-python"
    if not prediction_script.is_file():
        raise FileNotFoundError("The official SEGMENT-CACS prediction script is missing.")
    if not (wrapper_root / "parsers" / "segmentcacs_parser.py").is_file():
        raise FileNotFoundError("The official SEGMENT-CACS result parser is missing.")
    if not model_path.is_file():
        raise FileNotFoundError("The configured SEGMENT-CACS checkpoint is missing.")
    if output_dir.exists() and any(output_dir.iterdir()):
        raise FileExistsError("The managed output directory is not empty.")


def read_one_dicom_series(input_series: Path) -> tuple[sitk.Image, int]:
    series_ids = sitk.ImageSeriesReader.GetGDCMSeriesIDs(str(input_series))
    if not series_ids:
        raise RuntimeError("No readable DICOM series was found in the selected directory.")
    if len(series_ids) != 1:
        raise RuntimeError(
            f"Expected exactly one compatible DICOM series, found {len(series_ids)}."
        )

    file_names = sitk.ImageSeriesReader.GetGDCMSeriesFileNames(
        str(input_series), series_ids[0]
    )
    if not file_names:
        raise RuntimeError("The selected DICOM series contains no readable slices.")

    input_root = input_series.resolve()
    for file_name in file_names:
        candidate = Path(file_name).resolve()
        if candidate.parent != input_root or not candidate.is_file():
            raise RuntimeError(
                "The DICOM reader returned a file outside the selected directory."
            )

    reader = sitk.ImageSeriesReader()
    reader.SetFileNames(file_names)
    image = reader.Execute()
    validate_image_geometry(image, "DICOM CT")
    if image.GetDimension() != 3 or image.GetSize()[2] != len(file_names):
        raise RuntimeError("The DICOM slice count does not match the reconstructed volume.")
    return image, len(file_names)


def run_official_model(
    segmentcacs_root: Path,
    model_path: Path,
    prepared_input_dir: Path,
    output_dir: Path,
) -> None:
    wrapper_root = segmentcacs_root / "model-service-python"
    source_root = segmentcacs_root / "src"
    raw_output_dir = output_dir / "raw_outputs"
    result_path = output_dir / "result.json"
    status_path = output_dir / "status.json"
    run_log_path = output_dir / "run.log"
    raw_output_dir.mkdir(parents=True, exist_ok=True)

    original_path = list(sys.path)
    sys.path.insert(0, str(wrapper_root))
    sys.path.insert(0, str(source_root))
    try:
        import segment_cacs_predict
        from parsers.segmentcacs_parser import create_standard_result
        from utils.file_utils import find_agatston_json, write_json

        write_json(
            status_path,
            {"model": "SEGMENT-CACS", "status": "RUNNING", "progress": 0},
        )
        official_args = argparse.Namespace(
            model_dir=str(model_path),
            data_dir=str(prepared_input_dir),
            prediction_dir=str(raw_output_dir),
            filetype="nrrd",
            device="cpu",
            use_zero_module=False,
        )
        with run_log_path.open("w", encoding="utf-8") as run_log:
            run_log.write("SEGMENT-CACS local CPU inference\n")
            run_log.write("Input: managed prepared CT volume\n")
            run_log.write("Device: cpu\n\n")
            run_log.flush()
            with contextlib.redirect_stdout(run_log), contextlib.redirect_stderr(run_log):
                segment_cacs_predict.main(official_args)

        raw_result_path = find_agatston_json(raw_output_dir)
        standardized = create_standard_result(raw_result_path, output_dir)
        write_json(result_path, standardized)
        write_json(
            status_path,
            {"model": "SEGMENT-CACS", "status": "FINISHED", "progress": 100},
        )
    except Exception as exception:
        from utils.file_utils import write_json

        write_json(
            status_path,
            {
                "model": "SEGMENT-CACS",
                "status": "FAILED",
                "progress": 0,
                "error": (str(exception).strip() or exception.__class__.__name__)[:500],
            },
        )
        with run_log_path.open("a", encoding="utf-8") as run_log:
            run_log.write("\nOfficial inference failed:\n")
            traceback.print_exc(file=run_log)
        raise
    finally:
        sys.path[:] = original_path


def validate_image_geometry(image: sitk.Image, label: str) -> None:
    if image.GetDimension() != 3:
        raise RuntimeError(f"{label} is not a three-dimensional image.")
    if any(value <= 0 for value in image.GetSize()):
        raise RuntimeError(f"{label} has invalid dimensions.")
    spacing = image.GetSpacing()
    origin = image.GetOrigin()
    direction = image.GetDirection()
    if len(spacing) != 3 or any(not math.isfinite(v) or v <= 0 for v in spacing):
        raise RuntimeError(f"{label} has invalid spacing.")
    if len(origin) != 3 or any(not math.isfinite(v) for v in origin):
        raise RuntimeError(f"{label} has invalid origin.")
    if len(direction) != 9 or any(not math.isfinite(v) for v in direction):
        raise RuntimeError(f"{label} has invalid direction.")


def require_readable_image(path: Path, label: str) -> sitk.Image:
    if not path.is_file() or path.stat().st_size <= 0:
        raise RuntimeError(f"{label} artifact is missing or empty.")
    image = sitk.ReadImage(str(path))
    validate_image_geometry(image, label)
    array = sitk.GetArrayViewFromImage(image)
    if array.size == 0:
        raise RuntimeError(f"{label} has an empty voxel buffer.")
    return image


def validate_geometry(
    ct_image: sitk.Image,
    mask_image: sitk.Image,
    slice_count: int,
) -> dict[str, Any]:
    ct_size = tuple(int(v) for v in ct_image.GetSize())
    mask_size = tuple(int(v) for v in mask_image.GetSize())
    ct_spacing = tuple(float(v) for v in ct_image.GetSpacing())
    mask_spacing = tuple(float(v) for v in mask_image.GetSpacing())
    dimensions_match = ct_size == mask_size
    geometry_compatible = (
        dimensions_match
        and values_close(ct_spacing, mask_spacing)
        and values_close(ct_image.GetOrigin(), mask_image.GetOrigin())
        and values_close(ct_image.GetDirection(), mask_image.GetDirection())
    )
    if not dimensions_match:
        raise RuntimeError("CT and AI mask dimensions do not match.")
    if not geometry_compatible:
        raise RuntimeError("CT and AI mask physical geometry are incompatible.")

    mask_array = sitk.GetArrayViewFromImage(mask_image)
    foreground_count = int(np.count_nonzero(mask_array))
    return {
        "ctReadable": True,
        "maskReadable": True,
        "dimensionsMatch": dimensions_match,
        "geometryCompatible": geometry_compatible,
        "dicomSliceCount": int(slice_count),
        "ctSize": list(ct_size),
        "ctSpacing": list(ct_spacing),
        "ctOrigin": [float(v) for v in ct_image.GetOrigin()],
        "ctDirection": [float(v) for v in ct_image.GetDirection()],
        "maskSize": list(mask_size),
        "maskSpacing": list(mask_spacing),
        "maskOrigin": [float(v) for v in mask_image.GetOrigin()],
        "maskDirection": [float(v) for v in mask_image.GetDirection()],
        "foregroundVoxelCount": foreground_count,
    }


def values_close(left: Any, right: Any) -> bool:
    left_values = tuple(float(v) for v in left)
    right_values = tuple(float(v) for v in right)
    return len(left_values) == len(right_values) and all(
        abs(a - b) <= GEOMETRY_TOLERANCE for a, b in zip(left_values, right_values)
    )


def resolve_managed_artifact(output_dir: Path, value: Any, label: str) -> Path:
    if not value:
        raise RuntimeError(f"result.json does not reference the required {label}.")
    candidate = Path(str(value))
    if not candidate.is_absolute():
        candidate = output_dir / candidate
    candidate = candidate.resolve()
    if not candidate.is_relative_to(output_dir.resolve()):
        raise RuntimeError(f"The {label} path escaped the managed output directory.")
    return candidate


def read_json_object(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise FileNotFoundError("The official wrapper did not write result.json.")
    with path.open("r", encoding="utf-8") as file_obj:
        value = json.load(file_obj)
    if not isinstance(value, dict):
        raise RuntimeError("result.json is not a JSON object.")
    return value


def write_json_atomic(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w", encoding="utf-8") as file_obj:
        json.dump(payload, file_obj, indent=2)
        file_obj.write("\n")
    temporary.replace(path)


def sanitize_error(
    exception: Exception,
    *,
    input_series: Path,
    segmentcacs_root: Path,
    model_path: Path,
) -> str:
    message = str(exception).strip() or exception.__class__.__name__
    replacements = (
        (str(input_series), "<selected-dicom-series>"),
        (str(segmentcacs_root), "<segmentcacs-root>"),
        (str(model_path), "<model-checkpoint>"),
    )
    for source, replacement in replacements:
        message = message.replace(source, replacement)
    return message[:1000]


def format_values(values: Any) -> str:
    return "x".join(str(value) for value in values)


if __name__ == "__main__":
    raise SystemExit(main())
