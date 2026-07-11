import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


INTEGRATION_ROOT = Path(__file__).resolve().parents[2] / "integrations" / "ttnn"
sys.path.insert(0, str(INTEGRATION_ROOT))

from metalium_profiler_publisher import MetaliumProfilerPublisher


class MetaliumProfilerPublisherTest(unittest.TestCase):
    def test_publishes_latest_core_footprint_atomically(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            publisher = MetaliumProfilerPublisher(
                temporary_directory,
                "namespace/pod",
                device_keys=(0,),
                pod_namespace="default",
                pod_name="worker-0",
                container_name="model",
            )
            summary = publisher.publish_profiler_data(
                {
                    0: [
                        SimpleNamespace(core_count=8, num_available_cores=80),
                        SimpleNamespace(core_count=24, num_available_cores=80),
                    ]
                }
            )

            self.assertEqual(summary["0"]["tensix_cores_used"], 24)
            self.assertEqual(summary["0"]["tensix_cores_total"], 80)
            state_files = list((Path(temporary_directory) / "0").glob("*.state"))
            self.assertEqual(len(state_files), 1)
            state = state_files[0].read_text(encoding="utf-8")
            self.assertIn("schema_version=2\n", state)
            self.assertIn("workload_id=namespace/pod\n", state)
            self.assertIn("active=1\n", state)
            self.assertIn("programs_observed=2\n", state)
            self.assertIn("tensix_cores_used=24\n", state)
            self.assertIn("tensix_cores_total=80\n", state)
            self.assertIn("pod_name=worker-0\n", state)
            self.assertEqual(list((Path(temporary_directory) / "0").glob("*.tmp")), [])

            publisher.close()
            inactive_state = state_files[0].read_text(encoding="utf-8")
            self.assertIn("active=0\n", inactive_state)
            self.assertIn("tensix_cores_used=0\n", inactive_state)
            self.assertIn("tensix_cores_total=80\n", inactive_state)

    def test_empty_profiler_read_marks_configured_device_inactive(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            publisher = MetaliumProfilerPublisher(
                temporary_directory, "idle", device_keys=(0,)
            )
            summary = publisher.publish_profiler_data({})
            self.assertEqual(summary["0"]["active"], 0)
            self.assertEqual(summary["0"]["tensix_cores_used"], 0)
            publisher.close()

    def test_maps_runtime_chip_id_to_exporter_device_key(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            publisher = MetaliumProfilerPublisher(
                temporary_directory,
                "mapped",
                device_keys=("0000:01:00.0",),
                device_key_map={0: "0000:01:00.0"},
            )
            publisher.publish_profiler_data(
                {0: [SimpleNamespace(core_count=8, num_available_cores=80)]}
            )
            self.assertTrue(
                list(
                    (Path(temporary_directory) / "0000:01:00.0").glob("*.state")
                )
            )
            publisher.close()

    def test_rejects_impossible_core_count(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            publisher = MetaliumProfilerPublisher(
                temporary_directory, "invalid", device_keys=(0,), strict=True
            )
            with self.assertRaises(ValueError):
                publisher.publish_profiler_data(
                    {0: [SimpleNamespace(core_count=81, num_available_cores=80)]}
                )
            publisher.close()

    def test_best_effort_failure_state_and_warning_are_bounded(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            warnings = []
            publisher = MetaliumProfilerPublisher(
                temporary_directory,
                "best-effort",
                device_keys=(0,),
                warning_callback=warnings.append,
            )
            invalid = {0: [SimpleNamespace(core_count=81, num_available_cores=80)]}
            self.assertEqual(publisher.publish_profiler_data(invalid), {})
            self.assertEqual(publisher.publish_profiler_data(invalid), {})
            self.assertEqual(publisher.failure_count, 2)
            self.assertEqual(publisher.last_failure, "ValueError")
            self.assertEqual(len(warnings), 1)
            self.assertNotIn("81", warnings[0])
            publisher.close()

    def test_failed_rename_cleans_temporary_file(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            warnings = []
            publisher = MetaliumProfilerPublisher(
                temporary_directory,
                "rename-failure",
                device_keys=(0,),
                warning_callback=warnings.append,
            )
            with mock.patch("os.replace", side_effect=OSError("disk full")):
                self.assertEqual(publisher.publish_profiler_data({}), {})
            self.assertEqual(publisher.failure_count, 1)
            self.assertEqual(list(Path(temporary_directory).rglob("*.tmp")), [])
            publisher.close()

    def test_strict_publication_failure_raises(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            publisher = MetaliumProfilerPublisher(
                temporary_directory, "strict", device_keys=(0,), strict=True
            )
            with mock.patch("os.fsync", side_effect=OSError("read only")):
                with self.assertRaises(OSError):
                    publisher.publish_profiler_data({})
            self.assertEqual(list(Path(temporary_directory).rglob("*.tmp")), [])
            publisher.close()

    def test_best_effort_invalid_environment_disables_sampling(self):
        original = {
            name: os.environ.pop(name, None)
            for name in (
                "TT_METAL_DEVICE_PROFILER",
                "TT_METAL_PROFILER_MID_RUN_DUMP",
                "TT_METAL_PROFILER_CPP_POST_PROCESS",
            )
        }
        warnings = []
        try:
            with tempfile.TemporaryDirectory() as temporary_directory:
                publisher = MetaliumProfilerPublisher(
                    temporary_directory,
                    workload_id="disabled",
                    warning_callback=warnings.append,
                )
                self.assertEqual(publisher.sample(object()), {})
                self.assertEqual(publisher.sample(object()), {})
                self.assertEqual(publisher.failure_count, 1)
                self.assertEqual(len(warnings), 1)
                publisher.close()
        finally:
            for name, value in original.items():
                if value is not None:
                    os.environ[name] = value

    def test_profiler_environment_is_explicit(self):
        original = {name: os.environ.get(name) for name in (
            "TT_METAL_DEVICE_PROFILER",
            "TT_METAL_PROFILER_MID_RUN_DUMP",
            "TT_METAL_PROFILER_CPP_POST_PROCESS",
        )}
        try:
            for name in original:
                os.environ.pop(name, None)
            with self.assertRaises(RuntimeError):
                MetaliumProfilerPublisher.validate_profiler_environment()
        finally:
            for name, value in original.items():
                if value is None:
                    os.environ.pop(name, None)
                else:
                    os.environ[name] = value

    def test_sample_interval_bounds_profiler_reads(self):
        original = {
            name: os.environ.get(name)
            for name in (
                "TT_METAL_DEVICE_PROFILER",
                "TT_METAL_PROFILER_MID_RUN_DUMP",
                "TT_METAL_PROFILER_CPP_POST_PROCESS",
            )
        }
        calls = []
        fake_ttnn = SimpleNamespace(
            ReadDeviceProfiler=lambda _device: calls.append("read"),
            get_latest_programs_perf_data=lambda: {},
        )
        try:
            for name in original:
                os.environ[name] = "1"
            with tempfile.TemporaryDirectory() as temporary_directory:
                publisher = MetaliumProfilerPublisher(
                    temporary_directory,
                    "interval",
                    device_keys=(0,),
                    minimum_sample_interval_seconds=60,
                    strict=True,
                )
                with mock.patch.dict(sys.modules, {"ttnn": fake_ttnn}):
                    self.assertIn("0", publisher.sample(object()))
                    self.assertEqual(publisher.sample(object()), {})
                self.assertEqual(calls, ["read"])
                self.assertIsNotNone(publisher.last_success_timestamp)
                publisher.close()
        finally:
            for name, value in original.items():
                if value is None:
                    os.environ.pop(name, None)
                else:
                    os.environ[name] = value

    def test_scoped_root_cannot_escape_through_device_key(self):
        with tempfile.TemporaryDirectory() as temporary_directory:
            scoped = Path(temporary_directory) / "pod-a"
            sibling = Path(temporary_directory) / "pod-b"
            sibling.mkdir()
            marker = sibling / "marker"
            marker.write_text("owned by pod-b")
            publisher = MetaliumProfilerPublisher(
                scoped,
                "pod-a",
                device_keys=("../pod-b",),
                strict=True,
            )
            with self.assertRaises(ValueError):
                publisher.publish_profiler_data({})
            self.assertEqual(marker.read_text(), "owned by pod-b")
            self.assertEqual(list(sibling.iterdir()), [marker])
            with self.assertRaises(ValueError):
                publisher.close()


if __name__ == "__main__":
    unittest.main()
