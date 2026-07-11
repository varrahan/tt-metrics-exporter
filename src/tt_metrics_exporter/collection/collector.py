"""Telemetry collection from safe node-local sources."""

from __future__ import annotations

import os
from pathlib import Path
import stat

from ..models import (
    CollectionIssue,
    CollectionResult,
    CollectorConfig,
    CharacterDevice,
    DeviceTelemetry,
    HwmonSensor,
    InterconnectLink,
    NamedCounter,
    PciInfo,
    PowerInfo,
    SourceDiagnostics,
    TelemetrySource,
)
from .device_resources import DeviceResourceReader
from .parsers import parse_int64, parse_pci_resources, parse_uint64
from .state import StateIngestor
from .sysfs_io import SysfsReader


class SysfsCollector(DeviceResourceReader, SysfsReader):
    """Collect a bounded snapshot from sysfs and configured state roots."""

    def __init__(self, config: CollectorConfig | None = None) -> None:
        self.config = config or CollectorConfig()

    def collect(self) -> CollectionResult:
        result = CollectionResult()
        sysfs = result.sources[TelemetrySource.SYSFS_ROOT]
        sysfs.configured = True
        with StateIngestor.open(self.config, result) as state:
            try:
                status = self.config.sysfs_root.stat()
                if not stat.S_ISDIR(status.st_mode):
                    raise NotADirectoryError
                with os.scandir(self.config.sysfs_root) as iterator:
                    entries = list(iterator)
            except (FileNotFoundError, NotADirectoryError):
                sysfs.issues[CollectionIssue.MISSING_ROOT] += 1
                return result
            except PermissionError:
                sysfs.issues[CollectionIssue.PERMISSION_DENIED] += 1
                return result
            except OSError:
                sysfs.issues[CollectionIssue.READ_FAILED] += 1
                return result

            sysfs.accessible = True
            result.critical_sources_ok = True
            for entry in entries:
                try:
                    if not entry.is_dir(follow_symlinks=True):
                        continue
                except OSError:
                    continue
                device = DeviceTelemetry(id=entry.name, sysfs_path=Path(entry.path))
                self._populate_device(device, sysfs)
                state.populate(device)
                result.devices.append(device)
            result.devices.sort(key=lambda device: device.id)
            return result

    def _populate_device(self, device: DeviceTelemetry, diagnostics: SourceDiagnostics) -> None:
        device.architecture = self._read_first_text(
            device.sysfs_path,
            ("architecture", "arch", "chip_arch", "chip_architecture", "device_arch", "chip"),
            diagnostics,
        )
        device.board_type = self._read_first_text(
            device.sysfs_path,
            ("board_type", "board", "card_type", "card_series", "product_name"),
            diagnostics,
        )
        device.health = self._read_first_text(device.sysfs_path, ("health", "status", "device_status"), diagnostics)
        device.character_device = self._read_character_device(device.sysfs_path, diagnostics)
        device.pci = self._read_pci_info(device.sysfs_path, diagnostics)
        self._populate_firmware(device, diagnostics)
        if device.firmware.card_type is not None:
            device.board_type = self._normalize_card_type(device.firmware.card_type)
        if device.architecture is None:
            device.architecture = {
                "0x401e": "wormhole",
                "0xb140": "blackhole",
            }.get((device.pci.device_id or "").lower())
        if self.config.collect_hwmon:
            device.hwmon_sensors = self._read_hwmon_sensors(device.sysfs_path, diagnostics)
        device.power = self._read_power_info(device.sysfs_path, diagnostics)
        resource = self._read_text(device.sysfs_path / "device/resource", diagnostics)
        if resource is not None:
            device.pci_resources = parse_pci_resources(resource)
        if self.config.collect_pcie_counters:
            device.pcie_counters = self._read_pcie_counters(device.sysfs_path, diagnostics)
        self._populate_memory(device, diagnostics)
        self._populate_tensix(device, diagnostics)
        self._populate_health(device, diagnostics)
        device.interconnect_links = self._read_interconnect_links(device.sysfs_path, diagnostics)

    def _read_uevent_field(self, root: Path, field: str, diagnostics: SourceDiagnostics) -> str | None:
        content = self._read_text(root / "uevent", diagnostics)
        prefix = f"{field}="
        if content is not None:
            for line in content.splitlines():
                if line.startswith(prefix):
                    return line[len(prefix) :].strip(" \t\n\r\v\f")
        return None

    @staticmethod
    def _read_symlink_basename(path: Path) -> str | None:
        try:
            name = Path(os.readlink(path)).name
        except OSError:
            return None
        return name or None

    def _read_character_device(self, root: Path, diagnostics: SourceDiagnostics) -> CharacterDevice | None:
        value = self._read_text(root / "dev", diagnostics)
        if value is None or ":" not in value:
            return None
        major_text, minor_text = value.split(":", 1)
        major = parse_uint64(major_text)
        minor = parse_uint64(minor_text)
        if major is None or minor is None:
            return None
        return CharacterDevice(
            major=major,
            minor=minor,
            dev_name=self._read_uevent_field(root, "DEVNAME", diagnostics),
        )

    def _read_pci_info(self, root: Path, diagnostics: SourceDiagnostics) -> PciInfo:
        pci_root = root / "device"
        bdf = self._read_uevent_field(pci_root, "PCI_SLOT_NAME", diagnostics)
        if bdf is None:
            bdf = self._read_symlink_basename(pci_root)
        driver = self._read_uevent_field(pci_root, "DRIVER", diagnostics)
        if driver is None:
            driver = self._read_symlink_basename(pci_root / "driver")
        if driver is None:
            driver = self._read_text(pci_root / "driver", diagnostics)

        iommu_group = self._read_symlink_basename(pci_root / "iommu_group")
        parsed_iommu = parse_int64(iommu_group or "")
        if parsed_iommu is None:
            parsed_iommu = parse_int64(self._read_text(pci_root / "iommu_group", diagnostics) or "")
        return PciInfo(
            bdf=bdf,
            driver=driver,
            vendor_id=self._read_text(pci_root / "vendor", diagnostics),
            device_id=self._read_text(pci_root / "device", diagnostics),
            class_id=self._read_text(pci_root / "class", diagnostics),
            revision=self._read_text(pci_root / "revision", diagnostics),
            subsystem_vendor_id=self._read_text(pci_root / "subsystem_vendor", diagnostics),
            subsystem_device_id=self._read_text(pci_root / "subsystem_device", diagnostics),
            numa_node=parse_int64(self._read_text(pci_root / "numa_node", diagnostics) or ""),
            iommu_group=parsed_iommu,
            current_link_speed=self._read_text(pci_root / "current_link_speed", diagnostics),
            current_link_width=self._read_int(pci_root / "current_link_width", diagnostics),
            max_link_speed=self._read_text(pci_root / "max_link_speed", diagnostics),
            max_link_width=self._read_int(pci_root / "max_link_width", diagnostics),
            reset_method=self._read_text(pci_root / "reset_method", diagnostics),
        )

    @staticmethod
    def _normalize_card_type(value: str) -> str:
        normalized = value.strip().lower()
        return {
            "n150d": "n150",
            "n150s": "n150",
            "n300d": "n300",
            "n300s": "n300",
            "p100a": "p100",
            "p100b": "p100",
            "p150a": "p150",
            "p150b": "p150",
        }.get(normalized, normalized)

    def _populate_firmware(self, device: DeviceTelemetry, diagnostics: SourceDiagnostics) -> None:
        root = device.sysfs_path
        firmware = device.firmware
        firmware.ai_clock_mhz = self._read_int(root / "tt_aiclk", diagnostics)
        firmware.axi_clock_mhz = self._read_int(root / "tt_axiclk", diagnostics)
        firmware.arc_clock_mhz = self._read_int(root / "tt_arcclk", diagnostics)
        firmware.heartbeat = self._read_int(root / "tt_heartbeat", diagnostics)
        firmware.thermal_trip_count = self._read_int(root / "tt_therm_trip_count", diagnostics)
        for attribute, filename in (
            ("serial", "tt_serial"),
            ("card_type", "tt_card_type"),
            ("asic_id", "tt_asic_id"),
            ("fw_bundle_version", "tt_fw_bundle_ver"),
            ("m3_app_fw_version", "tt_m3app_fw_ver"),
            ("m3_bl_fw_version", "tt_m3bl_fw_ver"),
            ("arc_fw_version", "tt_arc_fw_ver"),
            ("eth_fw_version", "tt_eth_fw_ver"),
            ("ttflash_version", "tt_ttflash_ver"),
        ):
            setattr(firmware, attribute, self._read_text(root / filename, diagnostics))

    @staticmethod
    def _hwmon_unit(name: str) -> str:
        for prefix, unit in (
            ("temp", "millidegrees_celsius"),
            ("in", "millivolts"),
            ("curr", "milliamps"),
            ("power", "microwatts"),
            ("fan", "rpm"),
            ("freq", "hertz"),
        ):
            if name.startswith(prefix):
                return unit
        return "raw"

    def _read_hwmon_sensors(self, root: Path, diagnostics: SourceDiagnostics) -> list[HwmonSensor]:
        sensors: list[HwmonSensor] = []
        for base in (root / "hwmon", root / "device/hwmon"):
            try:
                directories = [entry for entry in base.iterdir() if entry.is_dir()]
            except OSError:
                continue
            for directory in directories:
                try:
                    entries = list(directory.iterdir())
                except OSError:
                    continue
                for entry in entries:
                    try:
                        is_input = entry.is_file() and entry.name.endswith("_input")
                    except OSError:
                        continue
                    if not is_input or len(entry.name) <= 6:
                        continue
                    value = self._read_int(entry, diagnostics)
                    if value is None:
                        continue
                    prefix = entry.name[:-6]
                    sensors.append(
                        HwmonSensor(
                            name=entry.name,
                            label=self._read_text(directory / f"{prefix}_label", diagnostics),
                            unit=self._hwmon_unit(prefix),
                            value=value,
                        )
                    )
        sensors.sort(key=lambda sensor: sensor.name)
        return sensors

    def _read_power_info(self, root: Path, diagnostics: SourceDiagnostics) -> PowerInfo:
        power_root = root / "power"
        return PowerInfo(
            runtime_status=self._read_text(power_root / "runtime_status", diagnostics),
            control=self._read_text(power_root / "control", diagnostics),
            runtime_enabled=self._read_text(power_root / "runtime_enabled", diagnostics),
            runtime_active_time_ms=parse_int64(self._read_text(power_root / "runtime_active_time", diagnostics) or ""),
            runtime_suspended_time_ms=parse_int64(self._read_text(power_root / "runtime_suspended_time", diagnostics) or ""),
            runtime_usage=parse_int64(self._read_text(power_root / "runtime_usage", diagnostics) or ""),
            runtime_active_kids=parse_int64(self._read_text(power_root / "runtime_active_kids", diagnostics) or ""),
            autosuspend_delay_ms=parse_int64(self._read_text(power_root / "autosuspend_delay_ms", diagnostics) or ""),
        )

    def _read_pcie_counters(self, root: Path, diagnostics: SourceDiagnostics) -> list[NamedCounter]:
        counters: list[NamedCounter] = []
        try:
            entries = list((root / "pcie_perf_counters").iterdir())
        except OSError:
            return counters
        for entry in entries:
            try:
                if not entry.is_file():
                    continue
            except OSError:
                continue
            value = self._read_uint(entry, diagnostics)
            if value is not None:
                counters.append(NamedCounter(name=entry.name, value=value))
        counters.sort(key=lambda counter: counter.name)
        return counters

    def _populate_health(self, device: DeviceTelemetry, diagnostics: SourceDiagnostics) -> None:
        root = device.sysfs_path
        health = device.health_detail
        health.fault_code = self._read_first_text(root, ("fault_code", "device_fault_code", "last_fault_code"), diagnostics)
        health.fault_reason = self._read_first_text(
            root,
            ("fault_reason", "device_fault_reason", "last_fault_reason", "reset_reason"),
            diagnostics,
        )
        health.reset_required = self._read_first_boolish(root, ("reset_required", "needs_reset", "requires_reset"), diagnostics)
        health.oom_fault_count = self._read_first_int(root, ("oom_fault_count", "oom_count", "out_of_memory_count"), diagnostics)
        health.hang_fault_count = self._read_first_int(root, ("hang_fault_count", "hang_count", "device_hang_count"), diagnostics)

    def _read_interconnect_links(self, root: Path, diagnostics: SourceDiagnostics) -> list[InterconnectLink]:
        links: list[InterconnectLink] = []
        for base, default_type in (
            (root / "scaleout_links", "scaleout"),
            (root / "ethernet_links", "ethernet"),
            (root / "fabric_links", "fabric"),
        ):
            try:
                entries = [entry for entry in base.iterdir() if entry.is_dir()]
            except OSError:
                continue
            for entry in entries:
                links.append(
                    InterconnectLink(
                        name=entry.name,
                        type=self._read_first_text(entry, ("type", "link_type"), diagnostics) or default_type,
                        state=self._read_first_text(entry, ("state", "status"), diagnostics),
                        peer=self._read_first_text(
                            entry,
                            ("peer", "remote", "remote_device", "remote_bdf"),
                            diagnostics,
                        ),
                        speed_gbps=self._read_first_int(
                            entry,
                            ("speed_gbps", "link_speed_gbps", "rate_gbps"),
                            diagnostics,
                        ),
                        ring_id=self._read_first_text(entry, ("ring_id", "ring"), diagnostics),
                    )
                )
        links.sort(key=lambda link: link.name)
        return links
