"""Read-only acceptance audit for a full-coverage Sentinel-1 fine FEP H5."""

import argparse
import json
from pathlib import Path

import h5py
import numpy as np


STRATEGIES = {
    "fine_v2": {
        "interpolation": "fine_state_vec_cubic_hermite_v2",
    },
    "fine_lagrange": {
        "interpolation": "orbit_state_vectors_apply_orbit_1s_lagrange_v1",
    },
}
SNAP_SHAPE = (9090, 24956)
SNAP_RESIDUAL_EDGE_WARNING_RAD = 0.001


def decode_scalar(h5, name):
    value = np.asarray(h5[name][()]).reshape(-1)[0]
    return value.decode("ascii") if isinstance(value, bytes) else value.item()


def require(h5, name):
    if name not in h5:
        raise RuntimeError("required dataset is missing: " + name)
    return h5[name]


def wrapped_summary(values):
    values = np.asarray(values, dtype=np.float64)
    if values.size == 0:
        return {"sample_count": 0}
    mean = float(np.angle(np.mean(np.exp(1j * values))))
    centered = np.angle(np.exp(1j * (values - mean)))
    return {
        "sample_count": int(values.size),
        "circular_mean_rad": mean,
        "mean_abs_wrapped_difference_rad": float(np.mean(np.abs(values))),
        "p95_abs_about_circular_mean_rad": float(np.quantile(np.abs(centered), 0.95)),
        "max_abs_wrapped_difference_rad": float(np.max(np.abs(values))),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("h5", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--strategy", choices=sorted(STRATEGIES), default="fine_v2")
    parser.add_argument("--snap-dir", type=Path,
                        default=Path(r"D:\test\benchmark\runtime\reference-work\30_sentinel1_flat_earth_phase_common_no_aoi\interferogram_flat_phase_iw3_vv_b1_6_b4_9.data"))
    args = parser.parse_args()
    expected = STRATEGIES[args.strategy]

    if not args.h5.is_file():
        raise RuntimeError("candidate H5 does not exist: " + str(args.h5))
    with h5py.File(args.h5, "r") as h5:
        strategy = decode_scalar(h5, "flat_earth_orbit_interpolation_strategy")
        master_source = decode_scalar(h5, "flat_earth_master_orbit_source")
        slave_source = decode_scalar(h5, "flat_earth_slave_orbit_source")
        ranges = np.asarray(require(h5, "s1_tops_retained_source_row_ranges")[()], dtype=np.int64)
        retained = np.asarray(require(h5, "s1_tops_retained_master_burst_indices")[()], dtype=np.int64).reshape(-1)
        source_rows = np.asarray(require(h5, "s1_tops_output_source_row_map")[:, 0], dtype=np.int64)
        valid = require(h5, "phase_valid_mask")
        fep = require(h5, "flat_earth_reference_phase")
        failures = np.asarray(require(h5, "flat_earth_failure_burst_statistics")[()], dtype=np.float64)
        rde = np.asarray(require(h5, "flat_earth_rde_burst_statistics")[()], dtype=np.float64)

        if ranges.ndim != 2 or ranges.shape[1] != 2 or retained.size != ranges.shape[0]:
            raise RuntimeError("invalid retained-burst contract")
        if source_rows.size != fep.shape[0] or valid.shape != fep.shape:
            raise RuntimeError("FEP, mask, and source-row grids differ")
        if (failures.ndim != 2 or failures.shape[1] != 3 or rde.ndim != 2 or
                rde.shape[1] < 11):
            raise RuntimeError("per-burst diagnostics have an invalid shape")
        snap_fep = None
        if args.snap_dir.is_dir() and fep.shape[1] == SNAP_SHAPE[1] and source_rows.max() < SNAP_SHAPE[0]:
            snap_path = args.snap_dir / "fep_IW3_VV_15Aug2016_27Aug2016.img"
            if snap_path.is_file():
                snap_fep = np.memmap(snap_path, dtype=">f4", mode="r", shape=SNAP_SHAPE)

        burst_rows = []
        for index, row_range in enumerate(ranges):
            diagnostic_row = int(retained[index]) - 1
            if diagnostic_row < 0 or diagnostic_row >= failures.shape[0] or diagnostic_row >= rde.shape[0]:
                raise RuntimeError("retained burst has no corresponding Core diagnostic row: " + str(retained[index]))
            # The persisted common-coverage contract stores [start, end), not
            # an inclusive final source row. This matches the C++ loader.
            selected = np.flatnonzero((source_rows >= row_range[0]) & (source_rows < row_range[1]))
            if selected.size == 0:
                raise RuntimeError("retained burst has no output rows: " + str(index + 1))
            mask = np.asarray(valid[selected, :], dtype=np.uint8)
            burst_rows.append(selected)
            burst = {
                "burst": int(retained[index]),
                "source_row_range_half_open": row_range.tolist(),
                "output_rows": int(selected.size),
                "valid_complex_sample_count": int(np.count_nonzero(mask)),
                "valid_complex_sample_fraction": float(np.mean(mask == 1)),
                "master_rde_failure_count": int(failures[diagnostic_row, 0]),
                "slave_zero_doppler_failure_count": int(failures[diagnostic_row, 1]),
                "physical_closure_failure_count": int(failures[diagnostic_row, 2]),
                "max_differential_range_error_bound_m": float(rde[diagnostic_row, 10]),
                "max_last_geometry_phase_change_rad": float(rde[diagnostic_row, 9]),
            }
            burst["accepted"] = (burst["master_rde_failure_count"] == 0 and
                                 burst["slave_zero_doppler_failure_count"] == 0 and
                                 burst["physical_closure_failure_count"] == 0 and
                                 burst["valid_complex_sample_count"] > 0)
            burst_rows[-1] = (selected, burst)

        boundaries = []
        for index in range(len(burst_rows) - 1):
            before_rows, before = burst_rows[index]
            after_rows, after = burst_rows[index + 1]
            before_mask = np.asarray(valid[before_rows[-1], :], dtype=np.uint8) == 1
            after_mask = np.asarray(valid[after_rows[0], :], dtype=np.uint8) == 1
            overlap = before_mask & after_mask
            boundary = {
                "before_burst": before["burst"],
                "after_burst": after["burst"],
                "source_row_gap": int(after["source_row_range_half_open"][0] -
                                      before["source_row_range_half_open"][1]),
                "edge_valid_intersection_count": int(np.count_nonzero(overlap)),
                "edge_valid_intersection_fraction": float(np.mean(overlap)),
                "accepted": bool(np.count_nonzero(overlap) > 0),
            }
            if snap_fep is not None and np.any(overlap):
                before_residual = np.angle(np.exp(1j * (
                    np.asarray(fep[before_rows[-1], :], dtype=np.float64) -
                    np.asarray(snap_fep[source_rows[before_rows[-1]], :], dtype=np.float64))))
                after_residual = np.angle(np.exp(1j * (
                    np.asarray(fep[after_rows[0], :], dtype=np.float64) -
                    np.asarray(snap_fep[source_rows[after_rows[0]], :], dtype=np.float64))))
                boundary["snap_residual_edge_jump"] = wrapped_summary(
                    np.angle(np.exp(1j * (after_residual[overlap] - before_residual[overlap]))))
                boundary["snap_residual_edge_warning_threshold_rad"] = SNAP_RESIDUAL_EDGE_WARNING_RAD
                boundary["snap_residual_edge_warning"] = (
                    boundary["snap_residual_edge_jump"]["mean_abs_wrapped_difference_rad"] >
                    SNAP_RESIDUAL_EDGE_WARNING_RAD)
            boundaries.append(boundary)

        snap_audit = None
        if args.snap_dir.is_dir():
            import subprocess
            audit_path = args.out.with_name(args.out.stem + "_snap_spatial.json")
            subprocess.run([
                "python", str(Path(__file__).with_name("fep_spatial_audit.py")), "--fine-h5", str(args.h5),
                "--snap-dir", str(args.snap_dir), "--out", str(audit_path),
                "--label", args.strategy + "_fullburst",
            ], check=True)
            snap_audit = str(audit_path)

        report = {
            "method": "read-only full-burst %s acceptance audit; no product data changed" % args.strategy,
            "h5": str(args.h5),
            "orbit": {
                "strategy": strategy,
                "master_source": master_source,
                "slave_source": slave_source,
                "accepted": (strategy == expected["interpolation"] and
                             master_source == "fine_state_vec" and slave_source == "fine_state_vec"),
            },
            "retained_burst_count": int(retained.size),
            "bursts": [item[1] for item in burst_rows],
            "boundaries": boundaries,
            "snap_spatial_audit": snap_audit,
        }
        report["accepted"] = (report["orbit"]["accepted"] and retained.size >= 6 and
                              all(item["accepted"] for item in report["bursts"]) and
                              all(item["accepted"] for item in boundaries))
        report["warnings"] = [
            "SNAP residual edge jump exceeds %.3g rad: burst %d to %d" %
            (item["snap_residual_edge_warning_threshold_rad"], item["before_burst"], item["after_burst"])
            for item in boundaries if item.get("snap_residual_edge_warning")
        ]
        report["status"] = ("accepted_with_warnings" if report["accepted"] and report["warnings"]
                            else "accepted" if report["accepted"] else "rejected")
    args.out.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="ascii")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
