"""Freeze inputs and runtime artifacts before a fine-orbit comparison run.

This tool is read-only with respect to H5 products. It writes only the requested
JSON manifest and fails if any declared comparison input is missing.
"""

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path

import h5py
import numpy as np


ALLOWED_STRATEGIES = {
    "fine_v2_cubic_hermite": "fine_state_vec_cubic_hermite_v2",
    "fine_lagrange": "orbit_state_vectors_apply_orbit_1s_lagrange_v1",
}


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def file_record(path):
    path = path.resolve()
    if not path.is_file():
        raise RuntimeError("required file does not exist: " + str(path))
    return {
        "path": str(path),
        "size_bytes": path.stat().st_size,
        "sha256": sha256_file(path),
    }


def dataset_record(h5, name):
    if name not in h5:
        raise RuntimeError("required dataset is missing: " + name)
    dataset = h5[name]
    value = np.ascontiguousarray(dataset[()])
    return {
        "dataset": name,
        "shape": list(value.shape),
        "dtype": str(value.dtype),
        "sha256": hashlib.sha256(value.tobytes(order="C")).hexdigest(),
    }


def input_record(path):
    record = file_record(path)
    with h5py.File(path, "r") as h5:
        record["fine_state_vec"] = dataset_record(h5, "fine_state_vec")
        record["state_vec"] = dataset_record(h5, "state_vec")
        for name in ("acquisition_start_time_gps", "acquisition_stop_time_gps",
                     "fine_state_vec_time_scale", "h5_time_reference_version"):
            if name not in h5:
                raise RuntimeError("required timing/provenance dataset is missing: " + name)
            value = np.asarray(h5[name][()]).reshape(-1)[0]
            if isinstance(value, bytes):
                value = value.decode("ascii")
            elif hasattr(value, "item"):
                value = value.item()
            record[name] = value
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strategy", choices=sorted(ALLOWED_STRATEGIES), required=True)
    parser.add_argument("--master-h5", type=Path, required=True)
    parser.add_argument("--slave-h5", type=Path, required=True)
    parser.add_argument("--master-eof", type=Path, required=True)
    parser.add_argument("--slave-eof", type=Path, required=True)
    parser.add_argument("--binary", type=Path, action="append", required=True,
                        help="Runtime executable or DLL to hash; repeat for every loaded artifact.")
    parser.add_argument("--script", type=Path, action="append", required=True,
                        help="Comparison/audit script to version; repeat as needed.")
    parser.add_argument("--intended-output", type=Path, required=True)
    parser.add_argument("--evidence-status", choices=("pre_run", "retroactive"), default="pre_run",
                        help="Use retroactive only for a product completed before this tool was introduced.")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    master_h5 = args.master_h5.resolve()
    slave_h5 = args.slave_h5.resolve()
    if master_h5 == slave_h5:
        raise RuntimeError("master and slave H5 inputs must be distinct files")

    manifest = {
        "schema": "sentinel1_fine_orbit_comparison_freeze_v1",
        "created_utc": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
        "requested_strategy": args.strategy,
        "evidence_status": args.evidence_status,
        "expected_h5_interpolation_strategy": ALLOWED_STRATEGIES[args.strategy],
        "environment": {
            "INSAR_SENTINEL1_FINE_ORBIT_INTERPOLATION": args.strategy,
        },
        "inputs": {
            "master": input_record(master_h5),
            "slave": input_record(slave_h5),
        },
        "eof": {
            "master": file_record(args.master_eof),
            "slave": file_record(args.slave_eof),
        },
        "runtime_binaries": [file_record(path) for path in args.binary],
        "comparison_scripts": [file_record(path) for path in args.script],
        "intended_output": str(args.intended_output.resolve()),
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="ascii")
    print(args.out)


if __name__ == "__main__":
    main()
