#!/usr/bin/env python
"""Two-stage, local-only VMTK vessel analysis and curved-MPR CLI.

The categorical source segmentation is never modified.  Component numbers are
one-based ranks sorted by descending voxel count.  All source-space points in
the JSON contract use SimpleITK's physical LPS coordinate convention.
"""

from __future__ import annotations

import argparse
import importlib.metadata
import json
import logging
import math
import os
import shutil
import sys
import tempfile
import time
import traceback
import uuid
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence

import numpy as np
import SimpleITK as sitk
import vtk
from vtk.util import numpy_support
from vmtk import vmtkscripts, vtkvmtk


LOGGER = logging.getLogger("cac.vessel_straightening")
RADIUS_ARRAY = "MaximumInscribedSphereRadius"
ABSCISSAS_ARRAY = "Abscissas"
NORMALS_ARRAY = "ParallelTransportNormals"
TANGENT_ARRAY = "FrenetTangent"
COORDINATE_SYSTEM = "LPS"


class VesselProcessingError(RuntimeError):
    """Expected, user-actionable vessel-processing failure."""


@dataclass(frozen=True)
class ComponentStatistics:
    component: int
    voxel_count: int
    physical_volume_mm3: float
    percentage_of_selected_label: float
    physical_bounding_box_dimensions_mm: tuple[float, float, float]
    likely_noise: bool


@dataclass(frozen=True)
class GeometryDescription:
    dimensions: tuple[int, int, int]
    spacing_mm: tuple[float, float, float]
    origin_lps_mm: tuple[float, float, float]
    direction_lps: tuple[float, ...]
    pixel_id: str


def emit_progress(stage: str, percent: int, message: str) -> None:
    payload = {
        "event": "progress",
        "stage": stage,
        "percent": max(0, min(100, int(percent))),
        "message": message,
    }
    print(json.dumps(payload, ensure_ascii=False), flush=True)
    LOGGER.info("[%s] %s%% %s", stage, payload["percent"], message)


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Analyze and straighten a manually selected NIfTI vessel component."
    )
    parser.add_argument("--mode", choices=("analyze", "straighten"), required=True)
    parser.add_argument("--ct", type=Path, required=True)
    parser.add_argument("--segmentation", type=Path, required=True)
    parser.add_argument("--label", type=int, required=True)
    parser.add_argument("--component", type=int, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--path-id")
    parser.add_argument("--cross-section-size-mm", type=float, default=32.0)
    parser.add_argument("--cross-section-spacing-mm", type=float, default=0.5)
    parser.add_argument("--longitudinal-spacing-mm", type=float, default=0.5)
    return parser.parse_args(argv)


def configure_logging(output_dir: Path) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    log_path = output_dir / "process.log"
    LOGGER.handlers.clear()
    LOGGER.setLevel(logging.INFO)
    formatter = logging.Formatter(
        "%(asctime)s %(levelname)s %(message)s", "%Y-%m-%dT%H:%M:%S"
    )
    file_handler = logging.FileHandler(log_path, encoding="utf-8")
    file_handler.setFormatter(formatter)
    LOGGER.addHandler(file_handler)
    return log_path


def validate_arguments(args: argparse.Namespace) -> None:
    if not args.ct.is_file():
        raise VesselProcessingError(f"CT file does not exist: {args.ct}")
    if not args.segmentation.is_file():
        raise VesselProcessingError(
            f"Categorical segmentation does not exist: {args.segmentation}"
        )
    if args.label <= 0:
        raise VesselProcessingError("The selected vessel label must be positive.")
    if args.component <= 0:
        raise VesselProcessingError("The selected component rank must be positive.")
    if args.cross_section_size_mm <= 0:
        raise VesselProcessingError("Cross-section size must be positive.")
    if args.cross_section_spacing_mm <= 0:
        raise VesselProcessingError("Cross-section spacing must be positive.")
    if args.longitudinal_spacing_mm <= 0:
        raise VesselProcessingError("Longitudinal spacing must be positive.")
    if args.mode == "straighten" and not args.path_id:
        raise VesselProcessingError("--path-id is required in straighten mode.")


def read_scalar_image(path: Path, role: str) -> sitk.Image:
    try:
        image = read_sitk_image_unicode_safe(path)
    except Exception as exc:
        raise VesselProcessingError(f"Could not read {role}: {exc}") from exc
    if image.GetDimension() != 3:
        raise VesselProcessingError(f"{role} must be a 3D image.")
    if image.GetNumberOfComponentsPerPixel() != 1:
        raise VesselProcessingError(f"{role} must contain scalar voxels.")
    if any(size <= 0 for size in image.GetSize()):
        raise VesselProcessingError(f"{role} has an empty dimension.")
    return image


def path_contains_non_ascii(path: Path) -> bool:
    try:
        str(path).encode("ascii")
        return False
    except UnicodeEncodeError:
        return True


def image_file_suffix(path: Path) -> str:
    lower_name = path.name.lower()
    if lower_name.endswith(".nii.gz"):
        return ".nii.gz"
    return path.suffix or ".image"


def read_sitk_image_unicode_safe(path: Path) -> sitk.Image:
    if not path_contains_non_ascii(path):
        return sitk.ReadImage(str(path))
    temporary_path = (
        Path(tempfile.gettempdir())
        / f"cac-vessel-read-{uuid.uuid4().hex}{image_file_suffix(path)}"
    )
    try:
        shutil.copyfile(path, temporary_path)
        return sitk.ReadImage(str(temporary_path))
    finally:
        temporary_path.unlink(missing_ok=True)


def describe_geometry(image: sitk.Image) -> GeometryDescription:
    return GeometryDescription(
        dimensions=tuple(int(value) for value in image.GetSize()),
        spacing_mm=tuple(float(value) for value in image.GetSpacing()),
        origin_lps_mm=tuple(float(value) for value in image.GetOrigin()),
        direction_lps=tuple(float(value) for value in image.GetDirection()),
        pixel_id=image.GetPixelIDTypeAsString(),
    )


def geometry_matches(first: sitk.Image, second: sitk.Image) -> bool:
    return (
        first.GetSize() == second.GetSize()
        and np.allclose(first.GetSpacing(), second.GetSpacing(), atol=1.0e-6)
        and np.allclose(first.GetOrigin(), second.GetOrigin(), atol=1.0e-5)
        and np.allclose(first.GetDirection(), second.GetDirection(), atol=1.0e-6)
    )


def component_statistics(
    segmentation: sitk.Image, label_value: int
) -> tuple[sitk.Image, list[ComponentStatistics]]:
    label_mask = sitk.Cast(segmentation == int(label_value), sitk.sitkUInt8)
    foreground_voxels = int(
        sitk.GetArrayViewFromImage(label_mask).astype(np.uint64, copy=False).sum()
    )
    if foreground_voxels == 0:
        raise VesselProcessingError(
            f"Selected label {label_value} is absent from the segmentation."
        )

    connected = sitk.ConnectedComponent(label_mask, False)
    shape = sitk.LabelShapeStatisticsImageFilter()
    shape.Execute(connected)
    records: list[tuple[int, int, float, tuple[float, float, float]]] = []
    for native_label in shape.GetLabels():
        voxel_count = int(shape.GetNumberOfPixels(native_label))
        physical_size = float(shape.GetPhysicalSize(native_label))
        index_and_size = shape.GetBoundingBox(native_label)
        start = index_and_size[:3]
        size = index_and_size[3:]
        corners: list[tuple[float, float, float]] = []
        for dx in (0, max(0, size[0] - 1)):
            for dy in (0, max(0, size[1] - 1)):
                for dz in (0, max(0, size[2] - 1)):
                    corners.append(
                        connected.TransformIndexToPhysicalPoint(
                            (start[0] + dx, start[1] + dy, start[2] + dz)
                        )
                    )
        corner_array = np.asarray(corners, dtype=np.float64)
        dimensions = tuple(
            float(value)
            for value in (corner_array.max(axis=0) - corner_array.min(axis=0))
        )
        records.append((int(native_label), voxel_count, physical_size, dimensions))

    records.sort(key=lambda item: (-item[1], item[0]))
    statistics: list[ComponentStatistics] = []
    voxel_volume = float(np.prod(segmentation.GetSpacing()))
    noise_threshold_voxels = max(8, int(round(1.0 / max(voxel_volume, 1.0e-9))))
    rank_image = sitk.RelabelComponent(
        connected, minimumObjectSize=0, sortByObjectSize=True
    )
    for rank, (native_label, voxel_count, physical_size, dimensions) in enumerate(
        records, start=1
    ):
        statistics.append(
            ComponentStatistics(
                component=rank,
                voxel_count=voxel_count,
                physical_volume_mm3=physical_size,
                percentage_of_selected_label=100.0
                * float(voxel_count)
                / float(foreground_voxels),
                physical_bounding_box_dimensions_mm=dimensions,
                likely_noise=voxel_count < noise_threshold_voxels,
            )
        )
    return rank_image, statistics


def select_component_in_ct_geometry(
    ct: sitk.Image,
    segmentation: sitk.Image,
    label_value: int,
    component_rank: int,
) -> tuple[sitk.Image, list[ComponentStatistics], bool]:
    ranks, statistics = component_statistics(segmentation, label_value)
    if component_rank > len(statistics):
        raise VesselProcessingError(
            f"Component {component_rank} is invalid; "
            f"label {label_value} has {len(statistics)} components."
        )
    selected = sitk.Cast(ranks == int(component_rank), sitk.sitkUInt8)
    was_resampled = not geometry_matches(ct, selected)
    if was_resampled:
        selected = sitk.Resample(
            selected,
            ct,
            sitk.Transform(),
            sitk.sitkNearestNeighbor,
            0,
            sitk.sitkUInt8,
        )
    selected_voxels = int(sitk.GetArrayViewFromImage(selected).sum())
    if selected_voxels == 0:
        raise VesselProcessingError(
            "The selected component became empty after mapping into CT geometry."
        )
    return selected, statistics, was_resampled


def sitk_to_vtk_image(image: sitk.Image, output_scalar_type: int | None = None) -> vtk.vtkImageData:
    array = np.asarray(sitk.GetArrayFromImage(image))
    vtk_image = vtk.vtkImageData()
    vtk_image.SetDimensions(*(int(value) for value in image.GetSize()))
    vtk_image.SetSpacing(*(float(value) for value in image.GetSpacing()))
    vtk_image.SetOrigin(*(float(value) for value in image.GetOrigin()))
    direction = vtk.vtkMatrix3x3()
    direction_values = image.GetDirection()
    for row in range(3):
        for column in range(3):
            direction.SetElement(row, column, direction_values[3 * row + column])
    vtk_image.SetDirectionMatrix(direction)
    if output_scalar_type is None:
        vtk_array = numpy_support.numpy_to_vtk(array.ravel(order="C"), deep=True)
        vtk_image.GetPointData().SetScalars(vtk_array)
    else:
        vtk_image.AllocateScalars(output_scalar_type, 1)
        destination = numpy_support.vtk_to_numpy(
            vtk_image.GetPointData().GetScalars()
        )
        destination[:] = array.ravel(order="C")
        vtk_image.GetPointData().GetScalars().Modified()
    vtk_image.Modified()
    return vtk_image


def component_surface(component_mask: sitk.Image) -> vtk.vtkPolyData:
    vtk_mask = sitk_to_vtk_image(component_mask, vtk.VTK_UNSIGNED_CHAR)
    contour = vtk.vtkMarchingCubes()
    contour.SetInputData(vtk_mask)
    contour.SetValue(0, 0.5)
    contour.ComputeNormalsOff()
    contour.Update()
    if contour.GetOutput().GetNumberOfPoints() < 20:
        raise VesselProcessingError("Selected component is too small to form a surface.")

    clean = vtk.vtkCleanPolyData()
    clean.SetInputConnection(contour.GetOutputPort())
    clean.Update()
    triangle = vtk.vtkTriangleFilter()
    triangle.SetInputConnection(clean.GetOutputPort())
    triangle.Update()
    smoother = vtk.vtkWindowedSincPolyDataFilter()
    smoother.SetInputConnection(triangle.GetOutputPort())
    smoother.SetNumberOfIterations(15)
    smoother.SetPassBand(0.1)
    smoother.BoundarySmoothingOff()
    smoother.FeatureEdgeSmoothingOff()
    smoother.NonManifoldSmoothingOn()
    smoother.NormalizeCoordinatesOn()
    smoother.Update()
    normals = vtk.vtkPolyDataNormals()
    normals.SetInputConnection(smoother.GetOutputPort())
    normals.AutoOrientNormalsOn()
    normals.ConsistencyOn()
    normals.SplittingOff()
    normals.Update()
    output = vtk.vtkPolyData()
    output.DeepCopy(normals.GetOutput())
    if output.GetNumberOfPolys() == 0:
        raise VesselProcessingError("Surface extraction produced no triangles.")
    return output


def surface_principal_extremes(
    surface: vtk.vtkPolyData,
) -> tuple[np.ndarray, np.ndarray]:
    points = numpy_support.vtk_to_numpy(surface.GetPoints().GetData()).astype(
        np.float64, copy=False
    )
    centered = points - points.mean(axis=0)
    _, _, vectors = np.linalg.svd(centered, full_matrices=False)
    projection = centered @ vectors[0]
    return points[int(np.argmin(projection))], points[int(np.argmax(projection))]


def open_surface_at_point(
    surface: vtk.vtkPolyData, point: Sequence[float]
) -> vtk.vtkPolyData:
    opened = vtk.vtkPolyData()
    opened.DeepCopy(surface)
    locator = vtk.vtkPointLocator()
    locator.SetDataSet(opened)
    locator.BuildLocator()
    point_id = locator.FindClosestPoint(point)
    if point_id < 0:
        raise VesselProcessingError("Could not locate the network extraction seed.")
    opened.BuildLinks()
    cell_ids = vtk.vtkIdList()
    opened.GetPointCells(point_id, cell_ids)
    if cell_ids.GetNumberOfIds() == 0:
        raise VesselProcessingError("Could not open the surface at the network seed.")
    opened.DeleteCell(cell_ids.GetId(0))
    opened.RemoveDeletedCells()
    return opened


def network_endpoint_candidates(
    surface: vtk.vtkPolyData,
) -> tuple[np.ndarray, list[np.ndarray], list[str]]:
    first_extreme, second_extreme = surface_principal_extremes(surface)
    warnings: list[str] = []
    opened = open_surface_at_point(surface, first_extreme)
    network_script = vmtkscripts.vmtkNetworkExtraction()
    network_script.Surface = opened
    network_script.Execute()
    network = network_script.Network
    endpoints: list[np.ndarray] = []
    if network and network.GetNumberOfLines() > 0:
        degree: dict[int, int] = {}
        for cell_index in range(network.GetNumberOfCells()):
            cell = network.GetCell(cell_index)
            if cell.GetCellType() not in (vtk.VTK_LINE, vtk.VTK_POLY_LINE):
                continue
            ids = cell.GetPointIds()
            if ids.GetNumberOfIds() < 2:
                continue
            for point_id in (ids.GetId(0), ids.GetId(ids.GetNumberOfIds() - 1)):
                degree[point_id] = degree.get(point_id, 0) + 1
        for point_id, point_degree in degree.items():
            if point_degree == 1:
                endpoints.append(
                    np.asarray(network.GetPoint(point_id), dtype=np.float64)
                )

    source = np.asarray(first_extreme, dtype=np.float64)
    targets: list[np.ndarray] = []
    for endpoint in endpoints:
        if np.linalg.norm(endpoint - source) < 1.0:
            continue
        if not any(np.linalg.norm(endpoint - current) < 1.0 for current in targets):
            targets.append(endpoint)
    if not targets:
        warnings.append(
            "VMTK network extraction did not expose terminal branches; "
            "the opposite principal surface extreme was used as the endpoint candidate."
        )
        targets = [np.asarray(second_extreme, dtype=np.float64)]
    return source, targets, warnings


def extract_vmtk_centerline_tree(
    surface: vtk.vtkPolyData,
) -> tuple[vtk.vtkPolyData, list[dict[str, Any]], list[str]]:
    source, targets, warnings = network_endpoint_candidates(surface)
    centerline_script = vmtkscripts.vmtkCenterlines()
    centerline_script.Surface = surface
    centerline_script.SeedSelectorName = "pointlist"
    centerline_script.SourcePoints = source.tolist()
    centerline_script.TargetPoints = np.asarray(targets).reshape(-1).tolist()
    centerline_script.AppendEndPoints = 1
    centerline_script.Resampling = 1
    centerline_script.ResamplingStepLength = 0.5
    centerline_script.Execute()
    centerlines = centerline_script.Centerlines
    if (
        centerlines is None
        or centerlines.GetNumberOfLines() == 0
        or centerlines.GetNumberOfPoints() < 2
    ):
        raise VesselProcessingError("VMTK produced no centerline tree.")

    geometry = vmtkscripts.vmtkCenterlineGeometry()
    geometry.Centerlines = centerlines
    geometry.Execute()
    attributes = vmtkscripts.vmtkCenterlineAttributes()
    attributes.Centerlines = geometry.Centerlines
    attributes.Execute()
    centerlines = vtk.vtkPolyData()
    centerlines.DeepCopy(attributes.Centerlines)
    for array_name in (RADIUS_ARRAY, TANGENT_ARRAY, ABSCISSAS_ARRAY, NORMALS_ARRAY):
        if centerlines.GetPointData().GetArray(array_name) is None:
            raise VesselProcessingError(
                f"VMTK centerline output is missing {array_name}."
            )

    endpoint_info = [
        {"role": "source", "point_lps_mm": source.tolist()},
        *[
            {"role": "target", "point_lps_mm": target.tolist()}
            for target in targets
        ],
    ]
    return centerlines, endpoint_info, warnings


def ordered_cell_point_ids(
    polydata: vtk.vtkPolyData, cell_index: int
) -> list[int]:
    cell = polydata.GetCell(cell_index)
    if cell.GetCellType() not in (vtk.VTK_LINE, vtk.VTK_POLY_LINE):
        return []
    ids = cell.GetPointIds()
    return [int(ids.GetId(index)) for index in range(ids.GetNumberOfIds())]


def polyline_length(points: np.ndarray) -> float:
    if len(points) < 2:
        return 0.0
    return float(np.linalg.norm(np.diff(points, axis=0), axis=1).sum())


def centerline_candidates(centerlines: vtk.vtkPolyData) -> list[dict[str, Any]]:
    radii = centerlines.GetPointData().GetArray(RADIUS_ARRAY)
    candidates: list[dict[str, Any]] = []
    for cell_index in range(centerlines.GetNumberOfCells()):
        point_ids = ordered_cell_point_ids(centerlines, cell_index)
        if len(point_ids) < 2:
            continue
        points = np.asarray(
            [centerlines.GetPoint(point_id) for point_id in point_ids],
            dtype=np.float64,
        )
        length_mm = polyline_length(points)
        if length_mm <= 0.5:
            continue
        path_radii = (
            np.asarray([radii.GetTuple1(point_id) for point_id in point_ids])
            if radii is not None
            else np.asarray([], dtype=np.float64)
        )
        candidates.append(
            {
                "path_id": f"path-{len(candidates) + 1}",
                "cell_id": cell_index,
                "physical_length_mm": length_mm,
                "start_point_lps_mm": points[0].tolist(),
                "end_point_lps_mm": points[-1].tolist(),
                "point_count": len(point_ids),
                "mean_radius_mm": (
                    float(path_radii.mean()) if path_radii.size else None
                ),
                "minimum_radius_mm": (
                    float(path_radii.min()) if path_radii.size else None
                ),
                "maximum_radius_mm": (
                    float(path_radii.max()) if path_radii.size else None
                ),
                "branch_information": {
                    "tree_cell_id": cell_index,
                    "tree_path_count": centerlines.GetNumberOfLines(),
                },
                "points_lps_mm": points.tolist(),
            }
        )
    if not candidates:
        raise VesselProcessingError("No valid centerline path candidates were generated.")
    return candidates


def write_json(path: Path, payload: dict[str, Any]) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    temporary.replace(path)


def write_polydata(path: Path, data: vtk.vtkPolyData) -> None:
    writer = vtk.vtkXMLPolyDataWriter()
    writer.SetFileName(str(path))
    writer.SetInputData(data)
    if writer.Write() != 1:
        raise VesselProcessingError(f"Could not write VTK polydata: {path}")


def vmtk_version() -> str:
    try:
        return importlib.metadata.version("vmtk")
    except importlib.metadata.PackageNotFoundError:
        return "unknown"


def analyze(
    ct_path: Path,
    segmentation_path: Path,
    label_value: int,
    component_rank: int,
    output_dir: Path,
) -> dict[str, Any]:
    started = time.perf_counter()
    emit_progress("read", 5, "Reading CT and categorical segmentation")
    ct = read_scalar_image(ct_path, "CT")
    segmentation = read_scalar_image(segmentation_path, "categorical segmentation")
    emit_progress("component", 15, "Selecting the requested connected component")
    selected_component, statistics, was_resampled = select_component_in_ct_geometry(
        ct, segmentation, label_value, component_rank
    )
    emit_progress("surface", 30, "Creating a clean vascular surface")
    surface = component_surface(selected_component)
    emit_progress("centerline", 45, "Extracting endpoint candidates with VMTK")
    centerlines, endpoints, warnings = extract_vmtk_centerline_tree(surface)
    emit_progress("paths", 80, "Building selectable centerline paths")
    candidates = centerline_candidates(centerlines)

    write_polydata(output_dir / "centerline_tree.vtp", centerlines)
    tree_payload = {
        "schema_version": 1,
        "coordinate_system": COORDINATE_SYSTEM,
        "selected_numeric_label": label_value,
        "selected_component": component_rank,
        "points_lps_mm": [
            list(centerlines.GetPoint(index))
            for index in range(centerlines.GetNumberOfPoints())
        ],
        "cells": [
            ordered_cell_point_ids(centerlines, index)
            for index in range(centerlines.GetNumberOfCells())
            if ordered_cell_point_ids(centerlines, index)
        ],
        "endpoint_information": endpoints,
        "point_arrays": [
            centerlines.GetPointData().GetArrayName(index)
            for index in range(centerlines.GetPointData().GetNumberOfArrays())
        ],
    }
    write_json(output_dir / "centerline_tree.json", tree_payload)
    candidates_payload = {
        "schema_version": 1,
        "coordinate_system": COORDINATE_SYSTEM,
        "selected_numeric_label": label_value,
        "selected_component": component_rank,
        "component_statistics": [asdict(item) for item in statistics],
        "candidate_paths": candidates,
        "warnings": warnings,
    }
    write_json(output_dir / "centerline_candidates.json", candidates_payload)
    result = {
        "mode": "analyze",
        "selected_numeric_label": label_value,
        "selected_component": component_rank,
        "candidate_path_count": len(candidates),
        "centerline_tree_path": str(output_dir / "centerline_tree.json"),
        "centerline_candidates_path": str(
            output_dir / "centerline_candidates.json"
        ),
        "centerline_polydata_path": str(output_dir / "centerline_tree.vtp"),
        "segmentation_resampled_to_ct": was_resampled,
        "ct_geometry": asdict(describe_geometry(ct)),
        "segmentation_geometry": asdict(describe_geometry(segmentation)),
        "vmtk_version": vmtk_version(),
        "duration_seconds": time.perf_counter() - started,
        "warnings": warnings,
    }
    emit_progress("complete", 100, "Centerline analysis complete")
    print(json.dumps({"event": "result", **result}, ensure_ascii=False), flush=True)
    return result


def load_candidates(path: Path) -> dict[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        raise VesselProcessingError(f"Could not read centerline candidates: {exc}") from exc
    if payload.get("schema_version") != 1:
        raise VesselProcessingError("Unsupported centerline candidate schema.")
    candidates = payload.get("candidate_paths")
    if not isinstance(candidates, list) or not candidates:
        raise VesselProcessingError("Centerline candidate file contains no paths.")
    return payload


def candidate_by_id(payload: dict[str, Any], path_id: str) -> dict[str, Any]:
    for candidate in payload["candidate_paths"]:
        if candidate.get("path_id") == path_id:
            return candidate
    raise VesselProcessingError(f"Unknown centerline path ID: {path_id}")


def uniformly_resampled_centerline(
    candidate: dict[str, Any], spacing_mm: float
) -> tuple[vtk.vtkPolyData, float]:
    original = np.asarray(candidate.get("points_lps_mm"), dtype=np.float64)
    if original.ndim != 2 or original.shape[1] != 3 or len(original) < 2:
        raise VesselProcessingError("Selected centerline path has invalid points.")
    segment_lengths = np.linalg.norm(np.diff(original, axis=0), axis=1)
    cumulative = np.concatenate(([0.0], np.cumsum(segment_lengths)))
    total_length = float(cumulative[-1])
    if total_length < spacing_mm:
        raise VesselProcessingError("Selected centerline path is too short.")
    sample_abscissas = np.arange(
        0.0, math.floor(total_length / spacing_mm) * spacing_mm + 0.25 * spacing_mm, spacing_mm
    )
    if len(sample_abscissas) < 2:
        raise VesselProcessingError("Centerline resampling produced too few points.")
    sampled = np.column_stack(
        [
            np.interp(sample_abscissas, cumulative, original[:, axis])
            for axis in range(3)
        ]
    )
    points = vtk.vtkPoints()
    for point in sampled:
        points.InsertNextPoint(*point)
    line = vtk.vtkPolyLine()
    line.GetPointIds().SetNumberOfIds(len(sampled))
    for index in range(len(sampled)):
        line.GetPointIds().SetId(index, index)
    cells = vtk.vtkCellArray()
    cells.InsertNextCell(line)
    polyline = vtk.vtkPolyData()
    polyline.SetPoints(points)
    polyline.SetLines(cells)
    geometry = vmtkscripts.vmtkCenterlineGeometry()
    geometry.Centerlines = polyline
    geometry.Execute()
    attributes = vmtkscripts.vmtkCenterlineAttributes()
    attributes.Centerlines = geometry.Centerlines
    attributes.Execute()
    output = vtk.vtkPolyData()
    output.DeepCopy(attributes.Centerlines)
    for name in (TANGENT_ARRAY, NORMALS_ARRAY, ABSCISSAS_ARRAY):
        if output.GetPointData().GetArray(name) is None:
            raise VesselProcessingError(
                f"Resampled VMTK centerline is missing {name}."
            )
    return output, total_length


def verify_vmtk_curved_mpr_runtime(
    image: vtk.vtkImageData,
    centerline: vtk.vtkPolyData,
    pixel_count: int,
    spacing_mm: float,
    background: float,
) -> tuple[int, int, int]:
    """Execute the official filter with its VTK 9.2 output-allocation fix."""
    curved = vtkvmtk.vtkvmtkCurvedMPRImageFilter()
    curved.SetInputData(image)
    curved.SetCenterline(centerline)
    curved.SetParallelTransportNormalsArrayName(NORMALS_ARRAY)
    curved.SetFrenetTangentArrayName(TANGENT_ARRAY)
    curved.SetInplaneOutputSpacing(spacing_mm, spacing_mm)
    curved.SetInplaneOutputSize(pixel_count, pixel_count)
    curved.SetReslicingBackgroundLevel(background)
    curved.UpdateInformation()
    output_info = curved.GetOutputInformation(0)
    extent = output_info.Get(vtk.vtkStreamingDemandDrivenPipeline.WHOLE_EXTENT())
    output = curved.GetOutput()
    output.SetExtent(extent)
    output.AllocateScalars(
        image.GetScalarType(), image.GetNumberOfScalarComponents()
    )
    vtk.vtkDataObject.SetPointDataActiveScalarInfo(
        output_info, image.GetScalarType(), image.GetNumberOfScalarComponents()
    )
    curved.Update()
    if curved.GetOutput().GetPointData().GetScalars() is None:
        raise VesselProcessingError("VMTK Curved MPR produced no scalar output.")
    return tuple(int(value) for value in curved.GetOutput().GetDimensions())


def reslice_with_vmtk_frame(
    image: vtk.vtkImageData,
    centerline: vtk.vtkPolyData,
    pixel_count: int,
    spacing_mm: float,
    background: float,
    nearest_neighbor: bool,
) -> np.ndarray:
    tangents = centerline.GetPointData().GetArray(TANGENT_ARRAY)
    normals = centerline.GetPointData().GetArray(NORMALS_ARRAY)
    if tangents is None or normals is None:
        raise VesselProcessingError("VMTK centerline frame arrays are unavailable.")
    output = np.empty(
        (centerline.GetNumberOfPoints(), pixel_count, pixel_count),
        dtype=np.float32,
    )
    origin = -0.5 * float(pixel_count - 1) * spacing_mm
    reslice = vtk.vtkImageReslice()
    reslice.SetInputData(image)
    reslice.SetOutputDimensionality(2)
    reslice.TransformInputSamplingOff()
    reslice.SetBackgroundLevel(background)
    reslice.SetOutputSpacing(spacing_mm, spacing_mm, 1.0)
    reslice.SetOutputExtent(0, pixel_count - 1, 0, pixel_count - 1, 0, 0)
    reslice.SetOutputOrigin(origin, origin, 0.0)
    if nearest_neighbor:
        reslice.SetInterpolationModeToNearestNeighbor()
    else:
        reslice.SetInterpolationModeToLinear()
    for index in range(centerline.GetNumberOfPoints()):
        center = centerline.GetPoint(index)
        tangent = np.asarray(tangents.GetTuple(index), dtype=np.float64)
        normal = np.asarray(normals.GetTuple(index), dtype=np.float64)
        tangent /= max(np.linalg.norm(tangent), 1.0e-12)
        normal -= tangent * np.dot(tangent, normal)
        normal /= max(np.linalg.norm(normal), 1.0e-12)
        second = np.cross(tangent, normal)
        second /= max(np.linalg.norm(second), 1.0e-12)
        reslice.SetResliceAxesDirectionCosines(
            *normal.tolist(), *second.tolist(), *tangent.tolist()
        )
        reslice.SetResliceAxesOrigin(*center)
        reslice.Update()
        values = numpy_support.vtk_to_numpy(
            reslice.GetOutput().GetPointData().GetScalars()
        )
        output[index] = values.reshape((pixel_count, pixel_count), order="C")
    return output


def write_cpr_nrrd(
    path: Path, array_zyx: np.ndarray, spacing_xy_mm: float, spacing_z_mm: float
) -> None:
    image = sitk.GetImageFromArray(array_zyx)
    image.SetSpacing((spacing_xy_mm, spacing_xy_mm, spacing_z_mm))
    image.SetOrigin(
        (
            -0.5 * (array_zyx.shape[2] - 1) * spacing_xy_mm,
            -0.5 * (array_zyx.shape[1] - 1) * spacing_xy_mm,
            0.0,
        )
    )
    image.SetDirection((1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0))
    if not path_contains_non_ascii(path):
        sitk.WriteImage(image, str(path), True)
        return
    temporary_path = (
        Path(tempfile.gettempdir())
        / f"cac-vessel-write-{uuid.uuid4().hex}.nrrd"
    )
    try:
        sitk.WriteImage(image, str(temporary_path), True)
        temporary_path.replace(path)
    finally:
        temporary_path.unlink(missing_ok=True)


def straighten(
    ct_path: Path,
    segmentation_path: Path,
    label_value: int,
    component_rank: int,
    path_id: str,
    output_dir: Path,
    cross_section_size_mm: float,
    cross_section_spacing_mm: float,
    longitudinal_spacing_mm: float,
) -> dict[str, Any]:
    started = time.perf_counter()
    candidates_path = output_dir / "centerline_candidates.json"
    if not candidates_path.is_file():
        analyze(
            ct_path,
            segmentation_path,
            label_value,
            component_rank,
            output_dir,
        )
    payload = load_candidates(candidates_path)
    if int(payload.get("selected_numeric_label", -1)) != label_value:
        raise VesselProcessingError(
            "Centerline candidates were produced for a different label."
        )
    if int(payload.get("selected_component", -1)) != component_rank:
        raise VesselProcessingError(
            "Centerline candidates were produced for a different component."
        )
    candidate = candidate_by_id(payload, path_id)
    emit_progress("resample-path", 10, "Resampling the selected centerline path")
    centerline, path_length_mm = uniformly_resampled_centerline(
        candidate, longitudinal_spacing_mm
    )
    write_polydata(output_dir / "selected_centerline.vtp", centerline)
    selected_payload = {
        "schema_version": 1,
        "coordinate_system": COORDINATE_SYSTEM,
        "selected_numeric_label": label_value,
        "selected_component": component_rank,
        "selected_path": path_id,
        "physical_length_mm": path_length_mm,
        "longitudinal_spacing_mm": longitudinal_spacing_mm,
        "points_lps_mm": [
            list(centerline.GetPoint(index))
            for index in range(centerline.GetNumberOfPoints())
        ],
        "parallel_transport_normals": [
            list(centerline.GetPointData().GetArray(NORMALS_ARRAY).GetTuple(index))
            for index in range(centerline.GetNumberOfPoints())
        ],
        "frenet_tangents": [
            list(centerline.GetPointData().GetArray(TANGENT_ARRAY).GetTuple(index))
            for index in range(centerline.GetNumberOfPoints())
        ],
    }
    write_json(output_dir / "selected_centerline.json", selected_payload)

    emit_progress("read", 20, "Reading source images")
    ct = read_scalar_image(ct_path, "CT")
    segmentation = read_scalar_image(segmentation_path, "categorical segmentation")
    component, _, was_resampled = select_component_in_ct_geometry(
        ct, segmentation, label_value, component_rank
    )
    ct_float = sitk.Cast(ct, sitk.sitkFloat32)
    vtk_ct = sitk_to_vtk_image(ct_float, vtk.VTK_FLOAT)
    vtk_mask = sitk_to_vtk_image(component, vtk.VTK_UNSIGNED_CHAR)
    pixel_count = max(
        2, int(round(cross_section_size_mm / cross_section_spacing_mm))
    )

    emit_progress("vmtk-curved-mpr", 35, "Validating the VMTK Curved MPR runtime")
    official_dimensions = verify_vmtk_curved_mpr_runtime(
        vtk_ct,
        centerline,
        pixel_count,
        cross_section_spacing_mm,
        -1024.0,
    )
    emit_progress("ct-reslice", 50, "Generating linear-interpolated straightened CT")
    straightened_ct = reslice_with_vmtk_frame(
        vtk_ct,
        centerline,
        pixel_count,
        cross_section_spacing_mm,
        -1024.0,
        nearest_neighbor=False,
    )
    emit_progress(
        "mask-reslice", 75, "Generating nearest-neighbour straightened vessel mask"
    )
    straightened_mask = reslice_with_vmtk_frame(
        vtk_mask,
        centerline,
        pixel_count,
        cross_section_spacing_mm,
        0.0,
        nearest_neighbor=True,
    )
    straightened_mask = (straightened_mask >= 0.5).astype(np.uint8)
    unique_mask_values = set(int(value) for value in np.unique(straightened_mask))
    if not unique_mask_values.issubset({0, 1}):
        raise VesselProcessingError(
            "Nearest-neighbour mask output contains non-binary values."
        )
    if straightened_ct.shape != straightened_mask.shape:
        raise VesselProcessingError("Straightened CT and mask dimensions differ.")
    if not np.isfinite(straightened_ct).all():
        raise VesselProcessingError("Straightened CT contains non-finite values.")

    ct_output = output_dir / "straightened_ct.nrrd"
    mask_output = output_dir / "straightened_vessel_mask.nrrd"
    write_cpr_nrrd(
        ct_output,
        straightened_ct.astype(np.float32, copy=False),
        cross_section_spacing_mm,
        longitudinal_spacing_mm,
    )
    write_cpr_nrrd(
        mask_output,
        straightened_mask,
        cross_section_spacing_mm,
        longitudinal_spacing_mm,
    )
    ct_roundtrip = read_sitk_image_unicode_safe(ct_output)
    mask_roundtrip = read_sitk_image_unicode_safe(mask_output)
    if ct_roundtrip.GetSize() != mask_roundtrip.GetSize():
        raise VesselProcessingError("Written CT and mask geometry is incompatible.")
    if tuple(ct_roundtrip.GetSize()) != (
        straightened_ct.shape[2],
        straightened_ct.shape[1],
        straightened_ct.shape[0],
    ):
        raise VesselProcessingError("Written Curved MPR dimensions are invalid.")

    duration = time.perf_counter() - started
    warnings = list(payload.get("warnings", []))
    warnings.append(
        "VMTK 1.5.0/VTK 9.2 on Windows requires explicit Curved MPR "
        "output allocation before Update()."
    )
    metadata = {
        "schema_version": 1,
        "selected_numeric_label": label_value,
        "selected_component": component_rank,
        "selected_path": path_id,
        "vmtk_version": vmtk_version(),
        "coordinate_system": COORDINATE_SYSTEM,
        "source_geometry_without_phi": {
            "ct": asdict(describe_geometry(ct)),
            "segmentation": asdict(describe_geometry(segmentation)),
        },
        "path_length_mm": path_length_mm,
        "output_dimensions": list(ct_roundtrip.GetSize()),
        "output_spacing_mm": list(ct_roundtrip.GetSpacing()),
        "cross_section_size_mm": cross_section_size_mm,
        "cross_section_spacing_mm": cross_section_spacing_mm,
        "longitudinal_spacing_mm": longitudinal_spacing_mm,
        "outside_ct_value_hu": -1024.0,
        "ct_interpolation": "linear",
        "mask_interpolation": "nearest-neighbour",
        "curved_mpr_engine": (
            "vtkvmtkCurvedMPRImageFilter runtime validation plus "
            "vtkImageReslice using VMTK FrenetTangent and "
            "ParallelTransportNormals"
        ),
        "official_vmtk_filter_dimensions": list(official_dimensions),
        "segmentation_resampled_to_ct": was_resampled,
        "processing_duration_seconds": duration,
        "warnings": warnings,
    }
    write_json(output_dir / "straightening_metadata.json", metadata)
    result = {
        "mode": "straighten",
        "selected_numeric_label": label_value,
        "selected_component": component_rank,
        "selected_path": path_id,
        "path_length_mm": path_length_mm,
        "processing_duration_seconds": duration,
        "straightened_ct_path": str(ct_output),
        "straightened_vessel_mask_path": str(mask_output),
        "selected_centerline_path": str(output_dir / "selected_centerline.json"),
        "selected_centerline_polydata_path": str(
            output_dir / "selected_centerline.vtp"
        ),
        "metadata_path": str(output_dir / "straightening_metadata.json"),
        "warnings": warnings,
    }
    emit_progress("complete", 100, "Vessel straightening complete")
    print(json.dumps({"event": "result", **result}, ensure_ascii=False), flush=True)
    return result


def run(args: argparse.Namespace) -> dict[str, Any]:
    validate_arguments(args)
    output_dir = args.output_dir.resolve()
    configure_logging(output_dir)
    LOGGER.info(
        "mode=%s label=%s component=%s output=%s",
        args.mode,
        args.label,
        args.component,
        output_dir,
    )
    if args.mode == "analyze":
        return analyze(
            args.ct.resolve(),
            args.segmentation.resolve(),
            args.label,
            args.component,
            output_dir,
        )
    return straighten(
        args.ct.resolve(),
        args.segmentation.resolve(),
        args.label,
        args.component,
        args.path_id,
        output_dir,
        args.cross_section_size_mm,
        args.cross_section_spacing_mm,
        args.longitudinal_spacing_mm,
    )


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    try:
        run(args)
        return 0
    except KeyboardInterrupt:
        print(
            json.dumps(
                {"event": "error", "code": "cancelled", "message": "Cancelled"},
                ensure_ascii=False,
            ),
            file=sys.stderr,
            flush=True,
        )
        return 130
    except VesselProcessingError as exc:
        LOGGER.error("%s", exc)
        print(
            json.dumps(
                {
                    "event": "error",
                    "code": "processing_error",
                    "message": str(exc),
                },
                ensure_ascii=False,
            ),
            file=sys.stderr,
            flush=True,
        )
        return 2
    except Exception as exc:  # pragma: no cover - defensive crash contract
        LOGGER.exception("Unhandled vessel-processing failure")
        print(
            json.dumps(
                {
                    "event": "error",
                    "code": "unhandled_error",
                    "message": str(exc),
                    "traceback": traceback.format_exc(),
                },
                ensure_ascii=False,
            ),
            file=sys.stderr,
            flush=True,
        )
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
