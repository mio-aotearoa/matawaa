"""Input integrity and integration tests for the real pinned upstream engine."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
import numpy as np

module_path = Path(os.environ.get("MATAWAA_ANALYSIS_MODULE", Path(__file__).resolve().parents[1] / "tools/sleep-analysis/analyze.py"))
spec = importlib.util.spec_from_file_location("matawaa_sleep_analysis", module_path)
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


class SleepAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "motion.csv"

    def recording(self, time, accel=None, **overrides):
        if accel is None:
            accel = np.column_stack([np.zeros(len(time)), np.zeros(len(time)), np.ones(len(time))])
        np.savetxt(self.path, np.column_stack([time, accel]), delimiter=",", header="timestamp,x,y,z", comments="", fmt="%.6f")
        metadata = {"schemaVersion": 2, "kind": "raw-acceleration", "units": "g", "status": "complete", "gaps": 0, "invalidSamples": 0, "samples": len(time), "startUs": round(time[0]*1e6), "endUs": round(time[-1]*1e6)}
        metadata.update(overrides)
        self.path.with_suffix(".json").write_text(json.dumps(metadata))

    def test_valid_input_and_units_preserved(self):
        time = 1767297600 + np.arange(100)/20
        self.recording(time)
        out_time, accel, fs, _ = analysis.load_recording(self.path, min_seconds=0)
        np.testing.assert_allclose(out_time, time)
        np.testing.assert_allclose(accel[:,2], 1)
        self.assertAlmostEqual(fs, 20, places=3)

    def test_rejects_gaps_duplicates_and_sparse_old_cadence(self):
        for time in [np.array([1.,1.05,2.]), np.array([1.,1.05,1.05]), np.array([1.,301.,601.])]:
            with self.subTest(time=time):
                self.recording(time)
                with self.assertRaises(ValueError): analysis.load_recording(self.path, min_seconds=0)

    def test_rejects_invalid_samples_in_metadata_and_nonfinite_csv(self):
        time=np.arange(100)/20+1
        for overrides in [{"invalidSamples":1},{"gaps":1},{"status":"recording"},{"units":"m/s2"},{"samples":101}]:
            with self.subTest(metadata=overrides):
                self.recording(time, **overrides)
                with self.assertRaises(ValueError): analysis.load_recording(self.path, min_seconds=0)
        accel=np.ones((100,3));accel[5,1]=np.nan;self.recording(time,accel)
        with self.assertRaises(ValueError): analysis.load_recording(self.path,min_seconds=0)

    def test_requires_upstream_minimum_recording_duration(self):
        self.recording(1+np.arange(100)/20)
        with self.assertRaises(ValueError): analysis.load_recording(self.path)

    def test_wear_intervals_do_not_assume_wrist_contact(self):
        time=1+np.arange(100)/20
        path=Path(self.directory.name)/"wear.csv";path.write_text("start,end\n1.5,3\n")
        np.testing.assert_equal(analysis.wear_indices(time,intervals=path),[[10,40]])
        path.write_text("start,end\n1,3\n2,4\n")
        with self.assertRaises(ValueError): analysis.wear_indices(time,intervals=path)

    def test_reports_are_private_and_atomic_on_failure(self):
        path=Path(self.directory.name)/"report.json"
        analysis.write_report(path,{"valid":True})
        before=path.read_bytes()
        self.assertEqual(path.stat().st_mode & 0o077,0)
        with self.assertRaises(ValueError):analysis.write_report(path,{"invalid":float("nan")})
        self.assertEqual(path.read_bytes(),before)
        self.assertEqual(list(path.parent.glob(".sleep-analysis-*")),[])

    def test_real_upstream_sleep_pipeline_runs_without_custom_staging(self):
        # Six hours of continuous input with a sustained rest block. This verifies
        # integration and schema only, not accuracy against polysomnography.
        count=6*3600*20+1
        time=1767297600+np.arange(count)/20
        rng=np.random.default_rng(42)
        accel=rng.uniform(-0.3,0.3,(count,3));accel[:,2]+=1
        # Low-motion fixture follows the upstream tests/sleep/conftest.py
        # input structure, rather than changing the model's non-wear thresholds.
        accel[3600*20:5*3600*20] = rng.uniform(-0.04,0.05,(4*3600*20,3))
        accel[3600*20:5*3600*20,2]+=1
        self.recording(time,accel)
        report=analysis.analyze(self.path,"UTC",assume_worn=True)
        self.assertEqual(report["algorithm"],"skdh.sleep.Sleep")
        self.assertEqual(report["upstreamRevision"],analysis.UPSTREAM_REVISION)
        self.assertIn("total sleep time",report["results"])
        self.assertTrue(any(value is not None for value in report["results"]["total sleep time"]))
        self.assertNotIn("sleepScore",report)
        json.dumps(report,allow_nan=False)


if __name__ == "__main__":unittest.main()
