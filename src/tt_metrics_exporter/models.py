"""Typed telemetry model shared by Python collectors and renderers."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path


class TelemetrySource(str, Enum):
    SYSFS_ROOT = "sysfs_root"
    ALLOCATION_STATE = "allocation_state"
    JANITOR_STATE = "janitor_state"
    METALIUM_PROFILER_STATE = "metalium_profiler_state"


class CollectionIssue(str, Enum):
    MISSING_ROOT = "missing_root"
    PERMISSION_DENIED = "permission_denied"
    READ_FAILED = "read_failed"
    INVALID_VALUE = "invalid_value"
    OVERSIZED_RECORD = "oversized_record"
    UNSUPPORTED_SCHEMA = "unsupported_schema"
    CARDINALITY_LIMIT = "cardinality_limit"
    STALE_RECORD = "stale_record"


@dataclass(slots=True)
class MemoryUsage:
    used_bytes: int | None = None
    total_bytes: int | None = None


@dataclass(slots=True)
class CharacterDevice:
    major: int = 0
    minor: int = 0
    dev_name: str | None = None


@dataclass(slots=True)
class PciInfo:
    bdf: str | None = None
    driver: str | None = None
    vendor_id: str | None = None
    device_id: str | None = None
    class_id: str | None = None
    revision: str | None = None
    subsystem_vendor_id: str | None = None
    subsystem_device_id: str | None = None
    numa_node: int | None = None
    iommu_group: int | None = None
    current_link_speed: str | None = None
    current_link_width: int | None = None
    max_link_speed: str | None = None
    max_link_width: int | None = None
    reset_method: str | None = None


@dataclass(slots=True)
class PciResource:
    index: int = 0
    start: int = 0
    end: int = 0
    flags: int = 0
    size_bytes: int = 0


@dataclass(slots=True)
class PowerInfo:
    runtime_status: str | None = None
    control: str | None = None
    runtime_enabled: str | None = None
    runtime_active_time_ms: int | None = None
    runtime_suspended_time_ms: int | None = None
    runtime_usage: int | None = None
    runtime_active_kids: int | None = None
    autosuspend_delay_ms: int | None = None


@dataclass(slots=True)
class NamedCounter:
    name: str = ""
    value: int = 0


@dataclass(slots=True)
class FirmwareTelemetry:
    ai_clock_mhz: int | None = None
    axi_clock_mhz: int | None = None
    arc_clock_mhz: int | None = None
    heartbeat: int | None = None
    thermal_trip_count: int | None = None
    serial: str | None = None
    card_type: str | None = None
    asic_id: str | None = None
    fw_bundle_version: str | None = None
    m3_app_fw_version: str | None = None
    m3_bl_fw_version: str | None = None
    arc_fw_version: str | None = None
    eth_fw_version: str | None = None
    ttflash_version: str | None = None


@dataclass(slots=True)
class HwmonSensor:
    name: str = ""
    label: str | None = None
    unit: str = ""
    value: int = 0


@dataclass(slots=True)
class MemoryTelemetry:
    used_bytes: int | None = None
    total_bytes: int | None = None
    free_bytes: int | None = None
    available_bytes: int | None = None
    bandwidth_bytes_per_second: int | None = None
    type: str | None = None
    controller_layout: str | None = None
    controller_count: int | None = None
    controllers_per_asic: int | None = None
    channel_count: int | None = None


@dataclass(slots=True)
class TensixTelemetry:
    used: int | None = None
    available: int | None = None
    total: int | None = None
    mesh_rows: int | None = None
    mesh_cols: int | None = None
    topology: str | None = None
    active_regions: str | None = None
    source: str | None = None


@dataclass(slots=True)
class MetaliumWorkloadTelemetry:
    workload_id: str = ""
    pod_namespace: str | None = None
    pod_name: str | None = None
    container_name: str | None = None
    active: int | None = None
    programs_observed: int | None = None
    tensix_cores_used: int | None = None
    tensix_cores_total: int | None = None
    sample_timestamp_seconds: int | None = None
    stale: bool = False


@dataclass(slots=True)
class HealthTelemetry:
    fault_code: str | None = None
    fault_reason: str | None = None
    reset_required: int | None = None
    oom_fault_count: int | None = None
    hang_fault_count: int | None = None


@dataclass(slots=True)
class InterconnectLink:
    name: str = ""
    type: str | None = None
    state: str | None = None
    peer: str | None = None
    speed_gbps: int | None = None
    ring_id: str | None = None


@dataclass(slots=True)
class AllocationTelemetry:
    claim_namespace: str | None = None
    claim_name: str | None = None
    claim_uid: str | None = None
    pod_namespace: str | None = None
    pod_name: str | None = None
    container_name: str | None = None


@dataclass(slots=True)
class JanitorTelemetry:
    state: str | None = None
    quarantine_reason: str | None = None
    last_scrub_status: str | None = None
    last_reset_status: str | None = None
    scrub_count: int | None = None
    reset_count: int | None = None
    last_scrub_timestamp_seconds: int | None = None
    last_reset_timestamp_seconds: int | None = None


@dataclass(slots=True)
class DeviceTelemetry:
    id: str = ""
    sysfs_path: Path = Path()
    architecture: str | None = None
    board_type: str | None = None
    health: str | None = None
    character_device: CharacterDevice | None = None
    pci: PciInfo = field(default_factory=PciInfo)
    pci_resources: list[PciResource] = field(default_factory=list)
    power: PowerInfo = field(default_factory=PowerInfo)
    pcie_counters: list[NamedCounter] = field(default_factory=list)
    firmware: FirmwareTelemetry = field(default_factory=FirmwareTelemetry)
    hwmon_sensors: list[HwmonSensor] = field(default_factory=list)
    memory: MemoryTelemetry = field(default_factory=MemoryTelemetry)
    tensix: TensixTelemetry = field(default_factory=TensixTelemetry)
    metalium_workloads: list[MetaliumWorkloadTelemetry] = field(default_factory=list)
    health_detail: HealthTelemetry = field(default_factory=HealthTelemetry)
    interconnect_links: list[InterconnectLink] = field(default_factory=list)
    allocation: AllocationTelemetry = field(default_factory=AllocationTelemetry)
    janitor: JanitorTelemetry = field(default_factory=JanitorTelemetry)


@dataclass(slots=True)
class CollectorConfig:
    sysfs_root: Path = Path("/sys/class/tenstorrent")
    allocation_state_root: Path | None = None
    janitor_state_root: Path | None = None
    metalium_profiler_state_root: Path | None = None
    metalium_profiler_stale_after_seconds: int = 15
    collect_hwmon: bool = False
    collect_pcie_counters: bool = False


def _empty_issues() -> dict[CollectionIssue, int]:
    return {issue: 0 for issue in CollectionIssue}


@dataclass(slots=True)
class SourceDiagnostics:
    source: TelemetrySource = TelemetrySource.SYSFS_ROOT
    configured: bool = False
    accessible: bool = False
    files_read: int = 0
    records_accepted: int = 0
    issues: dict[CollectionIssue, int] = field(default_factory=_empty_issues)


def _empty_sources() -> dict[TelemetrySource, SourceDiagnostics]:
    return {source: SourceDiagnostics(source=source) for source in TelemetrySource}


@dataclass(slots=True)
class CollectionResult:
    devices: list[DeviceTelemetry] = field(default_factory=list)
    sources: dict[TelemetrySource, SourceDiagnostics] = field(default_factory=_empty_sources)
    critical_sources_ok: bool = False
