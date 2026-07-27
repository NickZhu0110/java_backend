from __future__ import annotations

import json
import math
import sys
from pathlib import Path

import numpy as np
import pytest
import SimpleITK as sitk
import vtk
from vtk.util import numpy_support
from vmtk import vmtkscripts


PYTHON_WORKER = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PYTHON_WORKER))

from vessel_straightening import vmtk_straighten_vessel as pipeline


def make_image_pair(
    directory: Path,
    centerline_points: np.ndarray,
    *,
    size: tuple[int, int, int] = (64, 64, 64),
    spacing: tuple[float, float, float] = (1.0, 1.0, 1.0),
    direction: tuple[float, ...] = (1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0),
    label: int = 17,
    radius_mm: float = 3.0,
) -> tuple[Path, Path]:
    z_grid, y_grid, x_grid = np.indices(
        (size[2], size[1], size[0]), dtype=np.float32
    )
    physical = np.stack(
        (
            x_grid * spacing[0],
            y_grid * spacing[1],
            z_grid * spacing[2],
        ),
        axis=-1,
    )
    rotation = np.asarray(direction, dtype=np.float64).reshape(3, 3)
    physical = physical @ rotation.T
    minimum_distance2 = np.full(physical.shape[:-1], np.inf, dtype=np.float32)
    for point in centerline_points:
        minimum_distance2 = np.minimum(
            minimum_distance2,
            np.sum((physical - point) ** 2, axis=-1),
        )
    mask = minimum_distance2 <= radius_mm**2
    ct_array = np.full(mask.shape, -1024.0, dtype=np.float32)
    ct_array[mask] = 350.0
    segmentation_array = np.zeros(mask.shape, dtype=np.uint16)
    segmentation_array[mask] = label
    ct = sitk.GetImageFromArray(ct_array)
    segmentation = sitk.GetImageFromArray(segmentation_array)
    for image in (ct, segmentation):
        image.SetSpacing(spacing)
        image.SetDirection(direction)
    ct_path = directory / "合成 CT %.nii.gz"
    segmentation_path = directory / "合成 segmentation %.nii.gz"
    staged_ct = directory / "staged-ct.nii.gz"
    staged_segmentation = directory / "staged-segmentation.nii.gz"
    sitk.WriteImage(ct, str(staged_ct))
    sitk.WriteImage(segmentation, str(staged_segmentation))
    staged_ct.replace(ct_path)
    staged_segmentation.replace(segmentation_path)
    return ct_path, segmentation_path


def straight_points() -> np.ndarray:
    return np.asarray([(32.0, 32.0, float(z)) for z in range(8, 57)])


def curved_points() -> np.ndarray:
    return np.asarray(
        [
            (
                32.0 + 8.0 * math.sin((z - 8.0) / 11.0),
                32.0 + 5.0 * math.sin((z - 8.0) / 15.0 + 0.4),
                float(z),
            )
            for z in range(8, 57)
        ]
    )


def test_vmtk_import_and_official_curved_mpr_runtime() -> None:
    points = vtk.vtkPoints()
    for point in curved_points():
        points.InsertNextPoint(*point)
    line = vtk.vtkPolyLine()
    line.GetPointIds().SetNumberOfIds(points.GetNumberOfPoints())
    for index in range(points.GetNumberOfPoints()):
        line.GetPointIds().SetId(index, index)
    cells = vtk.vtkCellArray()
    cells.InsertNextCell(line)
    centerline = vtk.vtkPolyData()
    centerline.SetPoints(points)
    centerline.SetLines(cells)
    geometry = vmtkscripts.vmtkCenterlineGeometry()
    geometry.Centerlines = centerline
    geometry.Execute()
    attributes = vmtkscripts.vmtkCenterlineAttributes()
    attributes.Centerlines = geometry.Centerlines
    attributes.Execute()
    centerline = attributes.Centerlines

    image = vtk.vtkImageData()
    image.SetDimensions(64, 64, 64)
    image.SetSpacing(1.0, 1.0, 1.0)
    image.AllocateScalars(vtk.VTK_FLOAT, 1)
    values = numpy_support.vtk_to_numpy(image.GetPointData().GetScalars())
    values[:] = np.linspace(-1024.0, 500.0, len(values), dtype=np.float32)
    dimensions = pipeline.verify_vmtk_curved_mpr_runtime(
        image, centerline, 32, 0.5, -1024.0
    )
    assert dimensions == (32, 32, centerline.GetNumberOfPoints())


def test_component_statistics_are_ranked_and_preserve_source() -> None:
    array = np.zeros((12, 12, 12), dtype=np.uint16)
    array[1:5, 1:5, 1:5] = 23
    array[8:10, 8:10, 8:10] = 23
    image = sitk.GetImageFromArray(array)
    image.SetSpacing((0.5, 0.75, 1.25))
    original = sitk.GetArrayFromImage(image).copy()
    ranks, statistics = pipeline.component_statistics(image, 23)
    assert [item.voxel_count for item in statistics] == [64, 8]
    assert statistics[0].component == 1
    assert statistics[0].physical_volume_mm3 == pytest.approx(30.0)
    assert statistics[0].percentage_of_selected_label == pytest.approx(
        100.0 * 64.0 / 72.0
    )
    assert set(np.unique(sitk.GetArrayFromImage(ranks))) == {0, 1, 2}
    np.testing.assert_array_equal(sitk.GetArrayFromImage(image), original)


def test_anisotropic_non_identity_geometry_maps_with_nearest_neighbor() -> None:
    ct = sitk.Image((20, 18, 16), sitk.sitkFloat32)
    ct.SetSpacing((0.7, 1.1, 1.8))
    ct.SetOrigin((4.0, -3.0, 2.0))
    ct.SetDirection((0.0, -1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0))
    segmentation = sitk.Image((10, 9, 8), sitk.sitkUInt16)
    segmentation.SetSpacing((1.4, 2.2, 3.6))
    segmentation.SetOrigin(ct.GetOrigin())
    segmentation.SetDirection(ct.GetDirection())
    segmentation[2:8, 2:7, 1:7] = 41
    component, statistics, resampled = pipeline.select_component_in_ct_geometry(
        ct, segmentation, 41, 1
    )
    assert resampled
    assert component.GetSize() == ct.GetSize()
    assert component.GetSpacing() == ct.GetSpacing()
    assert component.GetDirection() == ct.GetDirection()
    assert statistics[0].voxel_count == 6 * 5 * 6
    assert set(np.unique(sitk.GetArrayFromImage(component))).issubset({0, 1})


def test_candidate_parser_and_failure_contract(tmp_path: Path) -> None:
    payload = {
        "schema_version": 1,
        "candidate_paths": [
            {"path_id": "path-1", "points_lps_mm": [[0, 0, 0], [0, 0, 2]]},
            {"path_id": "path-2", "points_lps_mm": [[0, 0, 0], [0, 1, 2]]},
        ],
    }
    path = tmp_path / "centerline_candidates.json"
    path.write_text(json.dumps(payload), encoding="utf-8")
    loaded = pipeline.load_candidates(path)
    assert pipeline.candidate_by_id(loaded, "path-2")["path_id"] == "path-2"
    with pytest.raises(pipeline.VesselProcessingError, match="Unknown"):
        pipeline.candidate_by_id(loaded, "missing")
    with pytest.raises(pipeline.VesselProcessingError, match="schema"):
        bad = tmp_path / "bad.json"
        bad.write_text('{"schema_version": 99}', encoding="utf-8")
        pipeline.load_candidates(bad)


def test_process_failure_and_cancellation_exit_contract(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    missing = tmp_path / "missing.nii.gz"
    output = tmp_path / "output"
    failure_exit = pipeline.main(
        [
            "--mode",
            "analyze",
            "--ct",
            str(missing),
            "--segmentation",
            str(missing),
            "--label",
            "17",
            "--component",
            "1",
            "--output-dir",
            str(output),
        ]
    )
    assert failure_exit == 2

    def interrupt(_args):
        raise KeyboardInterrupt

    monkeypatch.setattr(pipeline, "run", interrupt)
    cancelled_exit = pipeline.main(
        [
            "--mode",
            "analyze",
            "--ct",
            str(missing),
            "--segmentation",
            str(missing),
            "--label",
            "17",
            "--component",
            "1",
            "--output-dir",
            str(output),
        ]
    )
    assert cancelled_exit == 130


@pytest.mark.parametrize(
    "points_factory", [straight_points, curved_points], ids=["straight", "curved"]
)
def test_two_stage_pipeline_and_output_geometry(
    tmp_path: Path, points_factory
) -> None:
    ct_path, segmentation_path = make_image_pair(tmp_path, points_factory())
    output_dir = tmp_path / "输出 % curved mpr"
    output_dir.mkdir()
    pipeline.configure_logging(output_dir)
    analysis = pipeline.analyze(ct_path, segmentation_path, 17, 1, output_dir)
    assert analysis["candidate_path_count"] >= 1
    candidates = pipeline.load_candidates(output_dir / "centerline_candidates.json")
    selected_path = candidates["candidate_paths"][0]["path_id"]
    result = pipeline.straighten(
        ct_path,
        segmentation_path,
        17,
        1,
        selected_path,
        output_dir,
        16.0,
        1.0,
        1.0,
    )
    ct_output = pipeline.read_sitk_image_unicode_safe(
        Path(result["straightened_ct_path"])
    )
    mask_output = pipeline.read_sitk_image_unicode_safe(
        Path(result["straightened_vessel_mask_path"])
    )
    assert ct_output.GetSize() == mask_output.GetSize()
    assert ct_output.GetSize()[0:2] == (16, 16)
    assert ct_output.GetSpacing() == pytest.approx((1.0, 1.0, 1.0))
    mask_values = set(np.unique(sitk.GetArrayFromImage(mask_output)))
    assert mask_values.issubset({0, 1})
    assert 1 in mask_values


def test_branching_surface_produces_candidate_paths(tmp_path: Path) -> None:
    trunk = np.asarray([(32.0, 32.0, float(z)) for z in range(8, 38)])
    left = np.asarray(
        [
            (32.0 - 0.6 * step, 32.0, 37.0 + 0.8 * step)
            for step in range(0, 24)
        ]
    )
    right = np.asarray(
        [
            (32.0 + 0.6 * step, 32.0, 37.0 + 0.8 * step)
            for step in range(0, 24)
        ]
    )
    points = np.concatenate((trunk, left, right), axis=0)
    ct_path, segmentation_path = make_image_pair(
        tmp_path, points, radius_mm=2.5
    )
    output_dir = tmp_path / "branch-output"
    output_dir.mkdir()
    pipeline.configure_logging(output_dir)
    result = pipeline.analyze(ct_path, segmentation_path, 17, 1, output_dir)
    assert result["candidate_path_count"] >= 1
    candidates = pipeline.load_candidates(output_dir / "centerline_candidates.json")
    assert all(
        candidate["physical_length_mm"] > 0
        for candidate in candidates["candidate_paths"]
    )
