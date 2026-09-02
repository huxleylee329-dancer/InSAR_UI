"""Read-only spatial audit of a Sentinel-1 FEP candidate against SNAP."""

import argparse
import json
import math
from pathlib import Path

import h5py
import numpy as np


DEFAULT_FINE_H5 = Path(r"D:\test\4Benchmark\S1_Batch_Import_Orbit_regis_interf_diag_first2burst\20160815_iw3vv_regis_20160827_iw3vv_regis.h5")
DEFAULT_SNAP_DIR = Path(r"D:\test\benchmark\runtime\reference-work\30_sentinel1_flat_earth_phase_common_no_aoi\interferogram_flat_phase_iw3_vv_b1_6_b4_9.data")

SNAP_SHAPE = (9090, 24956)
BLOCK_ROWS = 32
LINE_BINS = 8
SAMPLE_BINS = 8
# Keep the audit bounded for full six-burst products.  Sampling is deterministic
# and uniform in raster storage order, so candidates remain directly comparable
# when audited with the same stride.
SAMPLE_STRIDE = 2048


def summary(values):
    values = np.asarray(values, dtype=np.float64)
    if values.size == 0:
        return {"sample_count": 0}
    sin_sum = float(np.sin(values).sum(dtype=np.float64))
    cos_sum = float(np.cos(values).sum(dtype=np.float64))
    mean = math.atan2(sin_sum, cos_sum)
    resultant = math.hypot(sin_sum, cos_sum) / values.size
    centered = np.angle(np.exp(1j * (values - mean)))
    return {
        "sample_count": int(values.size),
        "circular_mean_rad": mean,
        "circular_sigma_rad": math.sqrt(max(0.0, -2.0 * math.log(max(resultant, 1e-300)))),
        "mean_abs_wrapped_difference_rad": float(np.abs(values).mean()),
        "p95_abs_about_circular_mean_rad": float(np.quantile(np.abs(centered), 0.95)),
        "max_abs_wrapped_difference_rad": float(np.abs(values).max()),
    }


def scalar(h5, name):
    return np.asarray(h5[name][()]).reshape(-1)[0].item()


def string(h5, name):
    value = np.asarray(h5[name][()]).reshape(-1)[0]
    return value.decode("ascii") if isinstance(value, bytes) else str(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fine-h5", type=Path, default=DEFAULT_FINE_H5)
    parser.add_argument("--snap-dir", type=Path, default=DEFAULT_SNAP_DIR)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--label", default="unspecified")
    args = parser.parse_args()
    fine_h5 = args.fine_h5
    snap_dir = args.snap_dir
    out = args.out

    if not fine_h5.is_file():
        raise RuntimeError(f"candidate H5 does not exist: {fine_h5}")
    if not snap_dir.is_dir():
        raise RuntimeError(f"SNAP reference directory does not exist: {snap_dir}")
    snap_fep = np.memmap(
        snap_dir / "fep_IW3_VV_15Aug2016_27Aug2016.img",
        dtype=">f4", mode="r", shape=SNAP_SHAPE,
    )
    with h5py.File(fine_h5, "r") as h5:
        fep = h5["flat_earth_reference_phase"]
        valid = h5["phase_valid_mask"]
        source_rows = h5["s1_tops_output_source_row_map"][:, 0].astype(np.int64)
        ranges = h5["s1_tops_retained_source_row_ranges"][()].astype(np.int64)
        if fep.shape[1] != SNAP_SHAPE[1] or source_rows.size != fep.shape[0]:
            raise RuntimeError("fine output and SNAP reference grids are incompatible")

        burst_index = np.full(fep.shape[0], -1, dtype=np.int16)
        for index, (first, last) in enumerate(ranges):
            # Retained source-row ranges follow the C++ [start, end) contract.
            selected = (source_rows >= first) & (source_rows < last)
            burst_index[selected] = index
        if np.any(burst_index < 0):
            raise RuntimeError("an output row is outside every retained burst range")

        all_values = []
        per_burst = []
        valid_sample_count = 0
        for burst in range(len(ranges)):
            row_ids = np.flatnonzero(burst_index == burst)
            line_edges = np.linspace(0, row_ids.size, LINE_BINS + 1, dtype=np.int64)
            sample_edges = np.linspace(0, fep.shape[1], SAMPLE_BINS + 1, dtype=np.int64)
            cell_sums = np.zeros((LINE_BINS, SAMPLE_BINS), dtype=np.float64)
            cell_counts = np.zeros((LINE_BINS, SAMPLE_BINS), dtype=np.int64)
            sampled_values = []
            sums = {"n": 0.0, "y": 0.0, "x_line": 0.0, "x_sample": 0.0,
                    "yy": 0.0, "line2": 0.0, "sample2": 0.0,
                    "line_sample": 0.0}
            for y0 in range(0, row_ids.size, BLOCK_ROWS):
                y1 = min(y0 + BLOCK_ROWS, row_ids.size)
                output_rows = row_ids[y0:y1]
                mapped = source_rows[output_rows]
                user = np.asarray(fep[output_rows, :], dtype=np.float64)
                reference = np.asarray(snap_fep[mapped, :], dtype=np.float64)
                mask = (np.asarray(valid[output_rows, :]) == 1) & np.isfinite(user) & np.isfinite(reference)
                valid_sample_count += int(np.count_nonzero(mask))
                delta = np.angle(np.exp(1j * (user - reference)))
                selected = delta[mask]
                if selected.size:
                    sampled = selected[::SAMPLE_STRIDE]
                    sampled_values.append(sampled)
                    all_values.append(sampled)
                for local, output_row in enumerate(output_rows):
                    row_mask = mask[local]
                    if not np.any(row_mask):
                        continue
                    cols = np.flatnonzero(row_mask)
                    vals = delta[local, cols]
                    line_x = (float(y0 + local) / max(1, row_ids.size - 1)) - 0.5
                    sample_x = cols.astype(np.float64) / max(1, fep.shape[1] - 1) - 0.5
                    sums["n"] += vals.size
                    sums["y"] += float(vals.sum(dtype=np.float64))
                    sums["x_line"] += float((line_x * vals).sum(dtype=np.float64))
                    sums["x_sample"] += float((sample_x * vals).sum(dtype=np.float64))
                    sums["yy"] += float((vals * vals).sum(dtype=np.float64))
                    sums["line2"] += float(vals.size * line_x * line_x)
                    sums["sample2"] += float((sample_x * sample_x).sum(dtype=np.float64))
                    sums["line_sample"] += float((line_x * sample_x).sum(dtype=np.float64))
                    line_bin = min(LINE_BINS - 1, (y0 + local) * LINE_BINS // row_ids.size)
                    for sample_bin in range(SAMPLE_BINS):
                        cell = vals[(cols >= sample_edges[sample_bin]) & (cols < sample_edges[sample_bin + 1])]
                        if cell.size:
                            cell_sums[line_bin, sample_bin] += float(cell.sum(dtype=np.float64))
                            cell_counts[line_bin, sample_bin] += cell.size
            full = np.concatenate(sampled_values) if sampled_values else np.empty(0)
            n = sums["n"]
            mean = sums["y"] / n if n else 0.0
            centered = sums["y"] - n * mean
            # Least-squares slopes after centering the independent coordinates.
            line_var = sums["line2"] - sums["x_line"] ** 2 / n if n else 0.0
            sample_var = sums["sample2"] - sums["x_sample"] ** 2 / n if n else 0.0
            # Report spatial cell means; these reveal smooth ramps or curvature
            # without fitting or altering the production phase.
            cell_means = [
                [float(cell_sums[row, column] / cell_counts[row, column])
                 if cell_counts[row, column] else None for column in range(SAMPLE_BINS)]
                for row in range(LINE_BINS)
            ]
            per_burst.append({
                "burst": burst + 1,
                "source_row_range_half_open": ranges[burst].tolist(),
                "output_rows": int(row_ids.size),
                "summary": summary(full),
                "spatial_cell_mean_rad": cell_means,
                "cell_mean_min_rad": float(np.nanmin(np.asarray(cell_means, dtype=np.float64))),
                "cell_mean_max_rad": float(np.nanmax(np.asarray(cell_means, dtype=np.float64))),
                "cell_mean_range_rad": float(np.nanmax(np.asarray(cell_means, dtype=np.float64)) - np.nanmin(np.asarray(cell_means, dtype=np.float64))),
                "line_variance_normalized": float(line_var / n) if n else 0.0,
                "sample_variance_normalized": float(sample_var / n) if n else 0.0,
            })

        sampled = np.concatenate(all_values) if all_values else np.empty(0)
        report = {
            "method": "read-only deterministic-sampled wrapped FEP residual spatial audit; no fitting or correction",
            "candidate_label": args.label,
            "fine_h5": str(fine_h5),
            "snap_fep_dir": str(snap_dir),
            "fine_shape": list(fep.shape),
            "snap_shape": list(SNAP_SHAPE),
            "snap_comparable_valid_sample_count": valid_sample_count,
            "snap_comparable_valid_fraction": valid_sample_count / int(fep.shape[0] * fep.shape[1]),
            "residual_sample_stride": SAMPLE_STRIDE,
            "master_orbit_source": string(h5, "flat_earth_master_orbit_source"),
            "slave_orbit_source": string(h5, "flat_earth_slave_orbit_source"),
            "flat_earth_model_version": int(scalar(h5, "flat_earth_model_version")),
            "source_row_ranges_half_open": ranges.tolist(),
            "overall_sampled_summary": summary(sampled),
            "bursts": per_burst,
        }
    out.write_text(json.dumps(report, indent=2, allow_nan=False), encoding="ascii")
    print(out)
    print(json.dumps(report["overall_sampled_summary"], indent=2))
    for burst in per_burst:
        print("burst", burst["burst"], json.dumps(burst["summary"]),
              "cell_range", burst["cell_mean_range_rad"])


if __name__ == "__main__":
    main()
