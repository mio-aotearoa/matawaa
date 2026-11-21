#!/usr/bin/env python3
"""Run the pinned upstream SKDH sleep/wake pipeline on a completed motion recording.

No sleep classification or fertility model is implemented in this wrapper.
Method references and acquisition constraints: docs/health-methods.md.
"""
from __future__ import annotations
import argparse
import datetime
import json
import math
import os
from pathlib import Path
import tempfile
from zoneinfo import ZoneInfo
import numpy as np

UPSTREAM_REVISION = "5c65ba36c2cb1993d56a4788e1a8019d9c45b8f2"
PAPERS = ["https://doi.org/10.1093/sleep/15.5.461", "https://doi.org/10.1371/journal.pone.0160644", "https://doi.org/10.1038/s41598-018-31266-z"]


def load_recording(path: Path, min_seconds: float = 6 * 3600):
    """Reject absent/invalid/gappy inputs instead of imputing them as inactivity."""
    metadata = json.loads(path.with_suffix(".json").read_text())
    if metadata.get("schemaVersion") != 2 or metadata.get("kind") != "raw-acceleration" or metadata.get("units") != "g":
        raise ValueError("Expected a version 2 motion recording in g")
    if metadata.get("status") not in {"complete", "stopped"}:
        raise ValueError("Stop the recording successfully before analysis")
    if metadata.get("invalidSamples", 0) or metadata.get("gaps", 0):
        raise ValueError("Recording contains invalid or missing samples; analysis refused")
    data = np.genfromtxt(path, delimiter=",", names=True, dtype=float, ndmin=1)
    if data.dtype.names != ("timestamp", "x", "y", "z") or data.size < 2:
        raise ValueError("Expected timestamp,x,y,z columns and at least two samples")
    time = data["timestamp"]
    accel = np.column_stack([data[axis] for axis in ("x", "y", "z")])
    if not np.isfinite(time).all() or not np.isfinite(accel).all() or time[0] <= 0:
        raise ValueError("Non-finite or invalid sensor data")
    delta = np.diff(time)
    if np.any(delta <= 0):
        raise ValueError("Timestamps must be strictly increasing")
    fs = 1.0 / float(np.median(delta))
    if fs < 19.9 or fs > 200:
        raise ValueError("Sleep analysis needs continuous motion at 20 Hz or higher")
    if float(np.max(delta)) > 3.0 / fs:
        raise ValueError("Gaps exceed three sample periods; no gap filling is performed")
    if time[-1] - time[0] < min_seconds - 1.0 / fs:
        raise ValueError("At least six hours are required by the upstream sleep day settings")
    if metadata.get("samples") != len(time):
        raise ValueError("Recording and metadata sample counts disagree")
    if abs(metadata.get("startUs", 0) / 1e6 - time[0]) > 1e-5 or abs(metadata.get("endUs", 0) / 1e6 - time[-1]) > 1e-5:
        raise ValueError("Recording timestamps disagree with metadata")
    return time, accel, fs, metadata


def wear_indices(time, *, assume_worn=False, intervals=None):
    if assume_worn:
        return np.array([[0, len(time) - 1]], dtype=int)
    if Path(intervals).read_text().splitlines()[0].strip() != "start,end":
        raise ValueError("Wear intervals need a start,end header")
    rows = np.loadtxt(intervals, delimiter=",", skiprows=1, ndmin=2)
    if rows.shape[1] != 2 or not np.isfinite(rows).all() or not len(rows):
        raise ValueError("Wear intervals must contain start,end UTC seconds")
    if np.any(rows[:, 0] >= rows[:, 1]) or np.any(rows[1:, 0] < rows[:-1, 1]):
        raise ValueError("Wear intervals must be ordered and non-overlapping")
    if np.any(rows[:, 0] < time[0]) or np.any(rows[:, 1] > time[-1]):
        raise ValueError("Wear intervals must lie within the recording")
    indices = np.column_stack([np.searchsorted(time, rows[:, 0]), np.minimum(np.searchsorted(time, rows[:, 1]), len(time) - 1)])
    if np.any(indices[:, 0] >= indices[:, 1]):
        raise ValueError("Wear intervals must contain at least two samples")
    return indices


def json_value(value):
    if isinstance(value, dict):
        return {str(k): json_value(v) for k, v in value.items()}
    if isinstance(value, np.ndarray):
        return json_value(value.tolist())
    if isinstance(value, (list, tuple)):
        return [json_value(v) for v in value]
    if isinstance(value, np.generic):
        return json_value(value.item())
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if isinstance(value, (datetime.date, datetime.datetime)):
        return value.isoformat()
    return value


def analyze(path: Path, timezone: str, *, assume_worn=False, intervals=None):
    import skdh
    from skdh.preprocessing import GetDayWindowIndices
    from skdh.sleep import Sleep
    ZoneInfo(timezone)  # Require an actual IANA zone, including its DST rules.
    time, accel, fs, metadata = load_recording(path)
    wear = wear_indices(time, assume_worn=assume_worn, intervals=intervals)
    windows = GetDayWindowIndices(bases=[12], periods=[24]).predict(time=time, fs=fs, tz_name=timezone)
    results = Sleep().predict(time=time, accel=accel, fs=fs, wear=wear, tz_name=timezone, **windows)
    if not np.isfinite(np.asarray(results.get("total sleep time", []), dtype=float)).any():
        raise ValueError("The upstream engine found no analysable sleep window")
    return json_value({
        "schemaVersion": 1, "algorithm": "skdh.sleep.Sleep", "version": skdh.__version__,
        "upstreamRevision": UPSTREAM_REVISION, "papers": PAPERS, "timezone": timezone,
        "input": {"file": path.name, "samples": len(time), "samplingHz": fs,
                  "calibration": "sensor backend; not independently verified",
                  "wear": "user asserts full wear" if assume_worn else "user-provided intervals"},
        "results": results,
    })


def write_report(path: Path, report):
    """Private, atomic output: failed analysis must not truncate an existing report."""
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=path.parent, prefix=".sleep-analysis-")
    try:
        with os.fdopen(fd, "w") as out:
            json.dump(report, out, allow_nan=False, indent=2)
            out.write("\n")
            out.flush()
            os.fsync(out.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recording", type=Path)
    parser.add_argument("--timezone", required=True, help="Recording location, e.g. Australia/Canberra")
    parser.add_argument("--output", type=Path, required=True)
    wear = parser.add_mutually_exclusive_group(required=True)
    wear.add_argument("--assume-worn", action="store_true", help="Assert that the watch was worn for the entire recording")
    wear.add_argument("--wear-intervals", type=Path, help="CSV with start,end UTC seconds for observed wear periods")
    args = parser.parse_args()
    if args.output.resolve() in {args.recording.resolve(), args.recording.with_suffix(".json").resolve()}:
        parser.error("Output must not overwrite the recording or its metadata")
    try:
        report = analyze(args.recording, args.timezone, assume_worn=args.assume_worn, intervals=args.wear_intervals)
        write_report(args.output, report)
    except (ValueError, OSError, json.JSONDecodeError, IndexError) as error:
        parser.exit(1, f"Cannot analyze recording: {error}\n")


if __name__ == "__main__":
    main()
