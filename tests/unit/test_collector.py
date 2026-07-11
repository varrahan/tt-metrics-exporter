#!/usr/bin/env python3
"""Unit contracts for secure I/O and telemetry collection."""

import os
from pathlib import Path
import tempfile
import time
import unittest

from tt_metrics_exporter import SysfsCollector
from tt_metrics_exporter.models import (
    CollectionIssue,
    CollectorConfig,
    SourceDiagnostics,
    TelemetrySource,
)
from tt_metrics_exporter.collection.secure_io import SecureDirectory, valid_component


def profiler_state(schema: int, workload: str, timestamp: int, used: int = 1) -> str:
    return f"schema_version={schema}\nworkload_id={workload}\nactive=1\nprograms_observed=1\ntensix_cores_used={used}\ntensix_cores_total=80\nsample_timestamp_seconds={timestamp}\n"


class SecureIoTest(unittest.TestCase):
    def test_components_are_bounded_printable_and_relative(self) -> None:
        self.assertTrue(valid_component("0000:01:00.0"))
        for value in ("", ".", "..", "../escape", "line\nfeed", "é"):
            self.assertFalse(valid_component(value), value)

    def test_special_files_links_sizes_and_cardinality_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "valid").write_text(" value \n")
            (root / "empty").write_text("")
            (root / "large").write_text("x" * 33)
            (root / "directory").mkdir()
            (root / "outside").write_text("outside")
            (root / "symlink").symlink_to(root / "outside")
            os.link(root / "outside", root / "hardlink")
            os.mkfifo(root / "fifo")

            diagnostics = SourceDiagnostics()
            directory = SecureDirectory.open_root(root, diagnostics)
            self.assertIsNotNone(directory)
            assert directory is not None
            with directory:
                self.assertEqual(directory.read_text("valid", 32, diagnostics), "value")
                self.assertIsNone(directory.read_text("empty", 32, diagnostics))
                self.assertIsNone(directory.read_text("large", 32, diagnostics))
                self.assertIsNone(directory.read_text("directory", 32, diagnostics))
                self.assertIsNone(directory.read_text("symlink", 32, diagnostics))
                self.assertIsNone(directory.read_text("hardlink", 32, diagnostics))
                self.assertIsNone(directory.read_text("fifo", 32, diagnostics))
                self.assertIsNone(directory.open_directory("../escape", diagnostics))
                self.assertEqual(len(directory.entries(2, diagnostics)), 2)

            self.assertGreaterEqual(diagnostics.issues[CollectionIssue.INVALID_VALUE], 5)
            self.assertEqual(diagnostics.issues[CollectionIssue.OVERSIZED_RECORD], 1)
            self.assertEqual(diagnostics.issues[CollectionIssue.CARDINALITY_LIMIT], 1)

    def test_symlinked_root_is_not_followed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            real = base / "real"
            real.mkdir()
            link = base / "link"
            link.symlink_to(real, target_is_directory=True)
            diagnostics = SourceDiagnostics(configured=True)
            self.assertIsNone(SecureDirectory.open_root(link, diagnostics))
            self.assertFalse(diagnostics.accessible)


class CollectorTest(unittest.TestCase):
    def test_missing_sysfs_root_is_critical(self) -> None:
        result = SysfsCollector(CollectorConfig(sysfs_root=Path("/definitely/not/present"))).collect()
        self.assertFalse(result.critical_sources_ok)
        self.assertEqual(result.devices, [])
        self.assertEqual(
            result.sources[TelemetrySource.SYSFS_ROOT].issues[CollectionIssue.MISSING_ROOT],
            1,
        )

    def test_core_identity_memory_resources_and_optional_roots(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            sysfs = base / "sysfs"
            device = sysfs / "0"
            (device / "device").mkdir(parents=True)
            (sysfs / "1").mkdir()
            allocation = base / "allocation"
            allocation.mkdir()
            janitor = base / "janitor"
            janitor.mkdir()
            profiler = base / "profiler"
            profiler.mkdir()

            files = {
                "board_type": "ignored-by-firmware\n",
                "health": "ok\n",
                "memory_usage": "used_bytes 1 KiB\ntotal_bytes 4 KiB\n",
                "memory_free_bytes": "2 KiB\n",
                "memory_available_bytes": "3 KiB\n",
                "memory_bandwidth_bytes_per_second": "123456789\n",
                "memory_type": "GDDR6\n",
                "gddr_controller_layout": "localizedControllers\n",
                "gddr_controller_count": "12\n",
                "gddr_controllers_per_asic": "6\n",
                "dram_channel_count": "6\n",
                "dev": "241:0\n",
                "uevent": "MAJOR=241\nMINOR=0\nDEVNAME=tenstorrent/0\n",
                "device/uevent": "DRIVER=tenstorrent\nPCI_SLOT_NAME=0000:01:00.0\n",
                "device/vendor": "0x1e52\n",
                "device/device": "0x401e\n",
                "device/class": "0x120000\n",
                "device/revision": "0x01\n",
                "device/subsystem_vendor": "0x1af4\n",
                "device/subsystem_device": "0x1100\n",
                "device/numa_node": "-1\n",
                "device/iommu_group": "17\n",
                "device/current_link_speed": "16.0 GT/s PCIe\n",
                "device/current_link_width": "8\n",
                "device/max_link_speed": "32.0 GT/s PCIe\n",
                "device/max_link_width": "16\n",
                "device/reset_method": "bus\n",
                "device/resource": "0x800000000 0x81fffffff 0x140204\n0x0 0x0 0x0\n",
                "tt_aiclk": "1000\n",
                "tt_axiclk": "800\n",
                "tt_arcclk": "333\n",
                "tt_heartbeat": "42\n",
                "tt_therm_trip_count": "1\n",
                "tt_serial": "0xabc\n",
                "tt_card_type": "n300s\n",
                "tt_asic_id": "0x1234\n",
                "tt_fw_bundle_ver": "1.2.3\n",
                "tt_m3app_fw_ver": "2.3.4\n",
                "tt_m3bl_fw_ver": "3.4.5\n",
                "tt_arc_fw_ver": "4.5.6\n",
                "tt_eth_fw_ver": "5.6.7\n",
                "tt_ttflash_ver": "6.7.8\n",
                "power/runtime_status": "unsupported\n",
                "power/control": "auto\n",
                "power/runtime_enabled": "disabled\n",
                "power/runtime_active_time": "7\n",
                "power/runtime_suspended_time": "11\n",
                "power/runtime_usage": "3\n",
                "power/runtime_active_kids": "2\n",
                "power/autosuspend_delay_ms": "250\n",
                "device/hwmon/hwmon0/temp1_label": "asic\n",
                "device/hwmon/hwmon0/temp1_input": "55000\n",
                "device/hwmon/hwmon0/power1_label": "board\n",
                "device/hwmon/hwmon0/power1_input": "160000000\n",
                "pcie_perf_counters/read_words": "12\n",
                "tensix_cores_used": "8\n",
                "tensix_cores_available": "72\n",
                "tensix_cores_total": "80\n",
                "tensix_mesh": "8x10\n",
                "tensix_topology": "2dMesh\n",
                "tensix_active_regions": "0,0-1,3\n",
                "fault_code": "none\n",
                "fault_reason": "clear\n",
                "reset_required": "false\n",
                "oom_fault_count": "0\n",
                "hang_fault_count": "1\n",
                "scaleout_links/eth0/state": "up\n",
                "scaleout_links/eth0/peer": "0000:02:00.0\n",
                "scaleout_links/eth0/speed_gbps": "200\n",
                "scaleout_links/eth0/ring_id": "ring-a\n",
            }
            for relative, content in files.items():
                path = device / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)

            state_files = {
                allocation / "0/claim_namespace": "default\n",
                allocation / "0/claim_name": "tt-claim\n",
                allocation / "0/claim_uid": "claim-uid\n",
                allocation / "0/pod_namespace": "default\n",
                allocation / "0/pod_name": "tt-pod\n",
                allocation / "0/container_name": "worker\n",
                janitor / "0/state": "healthy\n",
                janitor / "0/quarantine_reason": "none\n",
                janitor / "0/last_scrub_status": "ok\n",
                janitor / "0/last_reset_status": "ok\n",
                janitor / "0/scrub_count": "3\n",
                janitor / "0/reset_count": "2\n",
                janitor / "0/last_scrub_timestamp_seconds": "123\n",
                janitor / "0/last_reset_timestamp_seconds": "456\n",
            }
            for path, content in state_files.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(content)
            now = int(time.time())
            v2 = profiler / "v2/workloads/pod-a/0/snapshot.state"
            v2.parent.mkdir(parents=True)
            v2.write_text(profiler_state(2, "same-workload", now, 5) + "pod_namespace=default\n" + "pod_name=worker-0\n" + "container_name=model\n")
            legacy = profiler / "0/legacy.state"
            legacy.parent.mkdir(parents=True)
            legacy.write_text(profiler_state(1, "same-workload", now, 79))

            result = SysfsCollector(
                CollectorConfig(
                    sysfs_root=sysfs,
                    allocation_state_root=allocation,
                    janitor_state_root=janitor,
                    metalium_profiler_state_root=profiler,
                    collect_hwmon=True,
                    collect_pcie_counters=True,
                )
            ).collect()

            self.assertTrue(result.critical_sources_ok)
            self.assertEqual([item.id for item in result.devices], ["0", "1"])
            collected = result.devices[0]
            self.assertEqual(collected.architecture, "wormhole")
            self.assertEqual(collected.board_type, "n300")
            self.assertEqual(collected.health, "ok")
            self.assertEqual(collected.memory.used_bytes, 1024)
            self.assertEqual(collected.memory.total_bytes, 4096)
            self.assertEqual(collected.memory.free_bytes, 2048)
            self.assertEqual(collected.memory.available_bytes, 3072)
            self.assertEqual(collected.memory.type, "GDDR6")
            self.assertEqual(collected.memory.controller_count, 12)
            self.assertEqual(len(collected.pci_resources), 1)
            self.assertEqual(collected.character_device.dev_name, "tenstorrent/0")
            self.assertEqual(collected.pci.bdf, "0000:01:00.0")
            self.assertEqual(collected.firmware.fw_bundle_version, "1.2.3")
            self.assertEqual(len(collected.hwmon_sensors), 2)
            self.assertEqual(collected.power.runtime_active_time_ms, 7)
            self.assertEqual(collected.pcie_counters[0].value, 12)
            self.assertEqual(collected.tensix.mesh_rows, 8)
            self.assertEqual(collected.tensix.mesh_cols, 10)
            self.assertEqual(collected.tensix.source, "sysfs")
            self.assertEqual(collected.health_detail.reset_required, 0)
            self.assertEqual(collected.interconnect_links[0].speed_gbps, 200)
            self.assertEqual(collected.allocation.claim_name, "tt-claim")
            self.assertEqual(collected.janitor.scrub_count, 3)
            self.assertEqual(len(collected.metalium_workloads), 1)
            self.assertEqual(collected.metalium_workloads[0].workload_id, "pod-a")
            self.assertEqual(collected.metalium_workloads[0].tensix_cores_used, 5)
            self.assertTrue(result.sources[TelemetrySource.ALLOCATION_STATE].accessible)
            self.assertTrue(result.sources[TelemetrySource.METALIUM_PROFILER_STATE].accessible)

            for path in (allocation / "0").iterdir():
                path.unlink()
            (allocation / "0").rmdir()
            cleaned = SysfsCollector(
                CollectorConfig(
                    sysfs_root=sysfs,
                    allocation_state_root=allocation,
                )
            ).collect()
            self.assertIsNone(cleaned.devices[0].allocation.pod_name)
            self.assertEqual(cleaned.devices[0].hwmon_sensors, [])

    def test_profiler_security_freshness_deduplication_and_aggregation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            sysfs = base / "sysfs"
            (sysfs / "0").mkdir(parents=True)
            profiler = base / "profiler/0"
            profiler.mkdir(parents=True)
            now = int(time.time())
            (profiler / "fresh.state").write_text(profiler_state(1, "default/worker", now, 24))
            (profiler / "duplicate.state").write_text(profiler_state(1, "default/worker", now - 1, 12))
            (profiler / "stale.state").write_text(profiler_state(1, "default/stale", 1, 8))
            (profiler / "unsupported.state").write_text(profiler_state(99, "ignored", now))
            (profiler / "control.state").write_text(profiler_state(1, "bad\x01label", now))
            outside = base / "outside.state"
            outside.write_text(profiler_state(1, "escaped", now))
            (profiler / "symlink.state").symlink_to(outside)
            os.link(outside, profiler / "hardlink.state")
            (profiler / "oversized.state").write_text("x" * (17 * 1024))
            escaped_v2 = base / "outside-v2/0/snapshot.state"
            escaped_v2.parent.mkdir(parents=True)
            escaped_v2.write_text(profiler_state(2, "escaped-v2", now))
            pod_link = base / "profiler/v2/workloads/pod-link"
            pod_link.parent.mkdir(parents=True)
            pod_link.symlink_to(escaped_v2.parents[1], target_is_directory=True)

            result = SysfsCollector(
                CollectorConfig(
                    sysfs_root=sysfs,
                    metalium_profiler_state_root=base / "profiler",
                )
            ).collect()
            device = result.devices[0]
            self.assertEqual(
                [workload.workload_id for workload in device.metalium_workloads],
                ["default/worker", "default/stale"],
            )
            self.assertFalse(device.metalium_workloads[0].stale)
            self.assertTrue(device.metalium_workloads[1].stale)
            self.assertEqual(device.tensix.used, 24)
            self.assertEqual(device.tensix.total, 80)
            self.assertEqual(device.tensix.available, 56)
            self.assertEqual(device.tensix.source, "metalium_profiler")
            diagnostics = result.sources[TelemetrySource.METALIUM_PROFILER_STATE]
            self.assertGreaterEqual(diagnostics.issues[CollectionIssue.INVALID_VALUE], 3)
            self.assertEqual(diagnostics.issues[CollectionIssue.UNSUPPORTED_SCHEMA], 1)
            self.assertEqual(diagnostics.issues[CollectionIssue.OVERSIZED_RECORD], 1)

    def test_fresh_workload_wins_bounded_export(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            sysfs = base / "sysfs"
            (sysfs / "0").mkdir(parents=True)
            profiler = base / "profiler/0"
            profiler.mkdir(parents=True)
            for index in range(1024):
                (profiler / f"stale-{index}.state").write_text(profiler_state(1, f"stale-{index}", 1))
            (profiler / "fresh.state").write_text(profiler_state(1, "fresh", int(time.time())))
            result = SysfsCollector(
                CollectorConfig(
                    sysfs_root=sysfs,
                    metalium_profiler_state_root=base / "profiler",
                )
            ).collect()
            workloads = result.devices[0].metalium_workloads
            self.assertEqual(len(workloads), 1024)
            self.assertEqual(workloads[0].workload_id, "fresh")
            self.assertGreaterEqual(
                result.sources[TelemetrySource.METALIUM_PROFILER_STATE].issues[CollectionIssue.CARDINALITY_LIMIT],
                1,
            )


if __name__ == "__main__":
    unittest.main()
