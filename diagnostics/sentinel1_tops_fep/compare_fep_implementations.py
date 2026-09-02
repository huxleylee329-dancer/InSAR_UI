"""Read-only FEP comparison of full-burst fine_v2 and fine/Lagrange products."""

import argparse
import hashlib
import json
import math
from pathlib import Path

import h5py
import numpy as np


V2_STRATEGY = "fine_state_vec_cubic_hermite_v2"
LAGRANGE_STRATEGY = "orbit_state_vectors_apply_orbit_1s_lagrange_v1"
BLOCK_ROWS = 32
LINE_BINS = 8
SAMPLE_BINS = 8
SAMPLE_STRIDE = 2048


def decode_scalar(h5, name):
    if name not in h5:
        raise RuntimeError("required dataset is missing: " + name)
    value = np.asarray(h5[name][()]).reshape(-1)[0]
    return value.decode("ascii") if isinstance(value, bytes) else str(value.item())


def normalized_path(value):
    return str(Path(value).resolve()).replace("\\", "/").lower()


def wrapped(value):
    return np.angle(np.exp(1j * value))


class CircularAccumulator:
    def __init__(self):
        self.count = 0
        self.sin_sum = 0.0
        self.cos_sum = 0.0
        self.samples = []

    def add(self, values):
        values = np.asarray(values, dtype=np.float64)
        if values.size == 0:
            return
        self.count += int(values.size)
        self.sin_sum += float(np.sin(values).sum(dtype=np.float64))
        self.cos_sum += float(np.cos(values).sum(dtype=np.float64))
        self.samples.append(values[::SAMPLE_STRIDE])

    def report(self):
        if not self.count:
            return {"sample_count": 0}
        mean = math.atan2(self.sin_sum, self.cos_sum)
        samples = np.concatenate(self.samples) if self.samples else np.empty(0, dtype=np.float64)
        centered = np.abs(wrapped(samples - mean))
        return {
            "sample_count": self.count,
            "circular_mean_rad": mean,
            "demeaned_p95_abs_rad": float(np.quantile(centered, 0.95)) if centered.size else None,
            "p95_sample_count": int(samples.size),
            "p95_sampling_stride": SAMPLE_STRIDE,
        }


def circular_cell_mean(values):
    if values.size == 0:
        return None
    return float(math.atan2(float(np.sin(values).sum(dtype=np.float64)),
                            float(np.cos(values).sum(dtype=np.float64))))


def freeze_subset(manifest):
    def product(value):
        return {
            "sha256": value["sha256"],
            "fine_state_vec": value["fine_state_vec"],
            "state_vec": value["state_vec"],
            "acquisition_start_time_gps": value["acquisition_start_time_gps"],
            "acquisition_stop_time_gps": value["acquisition_stop_time_gps"],
            "fine_state_vec_time_scale": value["fine_state_vec_time_scale"],
            "h5_time_reference_version": value["h5_time_reference_version"],
        }
    return {
        "master": product(manifest["inputs"]["master"]),
        "slave": product(manifest["inputs"]["slave"]),
        "master_eof_sha256": manifest["eof"]["master"]["sha256"],
        "slave_eof_sha256": manifest["eof"]["slave"]["sha256"],
    }


def read_freeze(path, requested_strategy):
    if not path.is_file():
        raise RuntimeError("freeze manifest does not exist: " + str(path))
    manifest = json.loads(path.read_text(encoding="ascii"))
    if manifest.get("schema") != "sentinel1_fine_orbit_comparison_freeze_v1":
        raise RuntimeError("unsupported freeze manifest schema: " + str(path))
    if manifest.get("requested_strategy") != requested_strategy:
        raise RuntimeError("freeze manifest strategy mismatch: " + str(path))
    for key in ("inputs", "eof", "runtime_binaries", "comparison_scripts", "intended_output"):
        if key not in manifest:
            raise RuntimeError("incomplete freeze manifest field " + key + ": " + str(path))
    return manifest


def read_candidate(path, expected_strategy):
    if not path.is_file():
        raise RuntimeError("candidate H5 does not exist: " + str(path))
    with h5py.File(path, "r") as h5:
        strategy = decode_scalar(h5, "flat_earth_orbit_interpolation_strategy")
        master_source = decode_scalar(h5, "flat_earth_master_orbit_source")
        slave_source = decode_scalar(h5, "flat_earth_slave_orbit_source")
        source_1 = decode_scalar(h5, "source_1")
        source_2 = decode_scalar(h5, "source_2")
        if (strategy != expected_strategy or
                master_source != "fine_state_vec" or slave_source != "fine_state_vec"):
            raise RuntimeError("candidate orbit provenance does not match requested comparison strategy: " + str(path))
        required = ["flat_earth_reference_phase", "phase_valid_mask",
                    "s1_tops_output_source_row_map", "s1_tops_retained_source_row_ranges",
                    "s1_tops_retained_master_burst_indices"]
        for name in required:
            if name not in h5:
                raise RuntimeError("required dataset is missing: " + name)
        shape = tuple(h5["flat_earth_reference_phase"].shape)
        if h5["phase_valid_mask"].shape != shape:
            raise RuntimeError("FEP and phase_valid_mask shape mismatch: " + str(path))
        source_rows = np.asarray(h5["s1_tops_output_source_row_map"][:, 0], dtype=np.int64)
        ranges = np.asarray(h5["s1_tops_retained_source_row_ranges"][()], dtype=np.int64)
        retained = np.asarray(h5["s1_tops_retained_master_burst_indices"][()], dtype=np.int64).reshape(-1)
        if (source_rows.size != shape[0] or ranges.ndim != 2 or ranges.shape[1] != 2 or
                retained.size != ranges.shape[0]):
            raise RuntimeError("invalid source-row or retained-burst contract: " + str(path))
        return {
            "path": str(path.resolve()),
            "shape": list(shape),
            "strategy": strategy,
            "source_1": source_1,
            "source_2": source_2,
            "source_rows": source_rows,
            "ranges": ranges,
            "retained": retained,
        }


def require_matching_contract(v2, lagrange):
    for key in ("shape", "source_rows", "ranges", "retained"):
        left = v2[key]
        right = lagrange[key]
        if isinstance(left, np.ndarray):
            matches = np.array_equal(left, right)
        else:
            matches = left == right
        if not matches:
            raise RuntimeError("candidate source-grid contract differs: " + key)


def require_candidate_sources(candidate, manifest):
    expected_master = normalized_path(manifest["inputs"]["master"]["path"])
    expected_slave = normalized_path(manifest["inputs"]["slave"]["path"])
    if normalized_path(candidate["source_1"]) != expected_master or \
            normalized_path(candidate["source_2"]) != expected_slave:
        raise RuntimeError("candidate source_1/source_2 do not match the frozen master/slave H5 inputs")


def burst_row_ids(source_rows, row_range):
    rows = np.flatnonzero((source_rows >= row_range[0]) & (source_rows < row_range[1]))
    if not rows.size:
        raise RuntimeError("retained burst has no output rows")
    return rows


def burst_report(v2_h5, lagrange_h5, row_ids, columns):
    accumulator = CircularAccumulator()
    cell_means = [[None for _ in range(SAMPLE_BINS)] for _ in range(LINE_BINS)]
    cell_counts = np.zeros((LINE_BINS, SAMPLE_BINS), dtype=np.int64)
    cell_sines = np.zeros((LINE_BINS, SAMPLE_BINS), dtype=np.float64)
    cell_cosines = np.zeros((LINE_BINS, SAMPLE_BINS), dtype=np.float64)
    common_count = 0
    line_edges = np.linspace(0, row_ids.size, LINE_BINS + 1, dtype=np.int64)
    sample_edges = np.linspace(0, columns, SAMPLE_BINS + 1, dtype=np.int64)
    for first in range(0, row_ids.size, BLOCK_ROWS):
        last = min(first + BLOCK_ROWS, row_ids.size)
        output_rows = row_ids[first:last]
        v2_phase = np.asarray(v2_h5["flat_earth_reference_phase"][output_rows, :], dtype=np.float64)
        lagrange_phase = np.asarray(lagrange_h5["flat_earth_reference_phase"][output_rows, :], dtype=np.float64)
        mask = ((np.asarray(v2_h5["phase_valid_mask"][output_rows, :]) == 1) &
                (np.asarray(lagrange_h5["phase_valid_mask"][output_rows, :]) == 1) &
                np.isfinite(v2_phase) & np.isfinite(lagrange_phase))
        delta = wrapped(v2_phase - lagrange_phase)
        accumulator.add(delta[mask])
        common_count += int(np.count_nonzero(mask))
        for line_bin in range(LINE_BINS):
            local_first = max(first, line_edges[line_bin]) - first
            local_last = min(last, line_edges[line_bin + 1]) - first
            if local_last <= local_first:
                continue
            for sample_bin in range(SAMPLE_BINS):
                values = delta[local_first:local_last,
                               sample_edges[sample_bin]:sample_edges[sample_bin + 1]]
                selected = values[mask[local_first:local_last,
                                       sample_edges[sample_bin]:sample_edges[sample_bin + 1]]]
                if selected.size:
                    cell_counts[line_bin, sample_bin] += selected.size
                    cell_sines[line_bin, sample_bin] += np.sin(selected).sum(dtype=np.float64)
                    cell_cosines[line_bin, sample_bin] += np.cos(selected).sum(dtype=np.float64)
    for line_bin in range(LINE_BINS):
        for sample_bin in range(SAMPLE_BINS):
            if cell_counts[line_bin, sample_bin]:
                cell_means[line_bin][sample_bin] = float(math.atan2(
                    cell_sines[line_bin, sample_bin], cell_cosines[line_bin, sample_bin]))
    finite_means = np.asarray([value for row in cell_means for value in row if value is not None], dtype=np.float64)
    report = accumulator.report()
    report.update({
        "common_valid_sample_count": common_count,
        "common_valid_fraction": common_count / float(row_ids.size * columns),
        "spatial_cell_mean_rad": cell_means,
        "spatial_cell_mean_range_rad": (float(finite_means.max() - finite_means.min())
                                        if finite_means.size else None),
    })
    return report


def boundary_report(v2_h5, lagrange_h5, before_row, after_row, columns):
    v2_before = np.asarray(v2_h5["flat_earth_reference_phase"][before_row, :], dtype=np.float64)
    v2_after = np.asarray(v2_h5["flat_earth_reference_phase"][after_row, :], dtype=np.float64)
    lagrange_before = np.asarray(lagrange_h5["flat_earth_reference_phase"][before_row, :], dtype=np.float64)
    lagrange_after = np.asarray(lagrange_h5["flat_earth_reference_phase"][after_row, :], dtype=np.float64)
    mask = ((np.asarray(v2_h5["phase_valid_mask"][before_row, :]) == 1) &
            (np.asarray(v2_h5["phase_valid_mask"][after_row, :]) == 1) &
            (np.asarray(lagrange_h5["phase_valid_mask"][before_row, :]) == 1) &
            (np.asarray(lagrange_h5["phase_valid_mask"][after_row, :]) == 1) &
            np.isfinite(v2_before) & np.isfinite(v2_after) &
            np.isfinite(lagrange_before) & np.isfinite(lagrange_after))
    jump = wrapped(wrapped(v2_after - lagrange_after) - wrapped(v2_before - lagrange_before))
    accumulator = CircularAccumulator()
    accumulator.add(jump[mask])
    sample_edges = np.linspace(0, columns, SAMPLE_BINS + 1, dtype=np.int64)
    cell_means = []
    for sample_bin in range(SAMPLE_BINS):
        values = jump[sample_edges[sample_bin]:sample_edges[sample_bin + 1]]
        selected = values[mask[sample_edges[sample_bin]:sample_edges[sample_bin + 1]]]
        cell_means.append(circular_cell_mean(selected))
    finite_means = np.asarray([value for value in cell_means if value is not None], dtype=np.float64)
    report = accumulator.report()
    report.update({
        "common_edge_valid_sample_count": int(np.count_nonzero(mask)),
        "common_edge_valid_fraction": float(np.mean(mask)),
        "spatial_sample_cell_mean_rad": cell_means,
        "spatial_sample_cell_mean_range_rad": (float(finite_means.max() - finite_means.min())
                                                 if finite_means.size else None),
    })
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fine-v2-h5", type=Path, required=True)
    parser.add_argument("--fine-lagrange-h5", type=Path, required=True)
    parser.add_argument("--fine-v2-freeze", type=Path, required=True)
    parser.add_argument("--fine-lagrange-freeze", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    v2_freeze = read_freeze(args.fine_v2_freeze, "fine_v2_cubic_hermite")
    lagrange_freeze = read_freeze(args.fine_lagrange_freeze, "fine_lagrange")
    if freeze_subset(v2_freeze) != freeze_subset(lagrange_freeze):
        raise RuntimeError("freeze manifests do not prove identical input H5, fine_state_vec/state_vec, and EOF bytes")
    if v2_freeze["intended_output"] == lagrange_freeze["intended_output"]:
        raise RuntimeError("freeze manifests declare the same intended output path")

    v2 = read_candidate(args.fine_v2_h5, V2_STRATEGY)
    lagrange = read_candidate(args.fine_lagrange_h5, LAGRANGE_STRATEGY)
    require_matching_contract(v2, lagrange)
    require_candidate_sources(v2, v2_freeze)
    require_candidate_sources(lagrange, lagrange_freeze)

    bursts = []
    boundaries = []
    with h5py.File(args.fine_v2_h5, "r") as v2_h5, h5py.File(args.fine_lagrange_h5, "r") as lagrange_h5:
        for index, row_range in enumerate(v2["ranges"]):
            rows = burst_row_ids(v2["source_rows"], row_range)
            item = burst_report(v2_h5, lagrange_h5, rows, v2["shape"][1])
            item.update({
                "burst": int(v2["retained"][index]),
                "source_row_range_half_open": row_range.tolist(),
                "output_rows": int(rows.size),
            })
            bursts.append((rows, item))
        for index in range(len(bursts) - 1):
            before_rows, before = bursts[index]
            after_rows, after = bursts[index + 1]
            item = boundary_report(v2_h5, lagrange_h5, before_rows[-1], after_rows[0], v2["shape"][1])
            item.update({
                "before_burst": before["burst"],
                "after_burst": after["burst"],
                "source_row_gap": int(after["source_row_range_half_open"][0] -
                                      before["source_row_range_half_open"][1]),
            })
            boundaries.append(item)

    report = {
        "method": "read-only direct wrapped FEP_v2 minus FEP_Lagrange comparison on identical phase_valid_mask; no product data changed",
        "fine_v2_h5": str(args.fine_v2_h5.resolve()),
        "fine_lagrange_h5": str(args.fine_lagrange_h5.resolve()),
        "fine_v2_freeze": str(args.fine_v2_freeze.resolve()),
        "fine_lagrange_freeze": str(args.fine_lagrange_freeze.resolve()),
        "freeze_shared_input_contract": "matched",
        "fine_v2_freeze_evidence_status": v2_freeze.get("evidence_status", "unknown"),
        "fine_lagrange_freeze_evidence_status": lagrange_freeze.get("evidence_status", "unknown"),
        "runtime_artifacts_identical": (v2_freeze["runtime_binaries"] == lagrange_freeze["runtime_binaries"]),
        "comparison_scripts_identical": (v2_freeze["comparison_scripts"] == lagrange_freeze["comparison_scripts"]),
        "source_grid_contract": {
            "shape": v2["shape"],
            "retained_master_burst_indices": v2["retained"].tolist(),
            "source_row_ranges": v2["ranges"].tolist(),
        },
        "bursts": [item for _, item in bursts],
        "boundaries": boundaries,
        "comparison_script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="ascii")
    print(args.out)


if __name__ == "__main__":
    main()
