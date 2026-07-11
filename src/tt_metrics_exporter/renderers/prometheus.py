"""Stable Prometheus text rendering for the telemetry contract."""

from __future__ import annotations

from collections.abc import Callable, Iterable
from typing import Any

from ..models import DeviceTelemetry, MetaliumWorkloadTelemetry


def _value(value: object | None) -> str:
    return "unknown" if value is None or value == "" else str(value)


def _escape(value: object | None) -> str:
    return _value(value).replace("\\", "\\\\").replace("\n", "\\n").replace('"', '\\"')


def _labels(pairs: Iterable[tuple[str, object | None]]) -> str:
    return ",".join(f'{name}="{_escape(value)}"' for name, value in pairs)


def _device_labels(device: DeviceTelemetry) -> list[tuple[str, object | None]]:
    return [
        ("device", device.id),
        ("architecture", device.architecture),
        ("board_type", device.board_type),
        ("pci_bdf", device.pci.bdf),
    ]


def _info_labels(device: DeviceTelemetry) -> list[tuple[str, object | None]]:
    character = device.character_device
    return _device_labels(device) + [
        ("sysfs_path", str(device.sysfs_path)),
        ("health", device.health),
        ("dev_major", None if character is None else character.major),
        ("dev_minor", None if character is None else character.minor),
        ("dev_name", None if character is None else character.dev_name),
        ("pci_vendor_id", device.pci.vendor_id),
        ("pci_device_id", device.pci.device_id),
        ("pci_driver", device.pci.driver),
        ("pci_class_id", device.pci.class_id),
        ("pci_revision", device.pci.revision),
        ("pci_subsystem_vendor_id", device.pci.subsystem_vendor_id),
        ("pci_subsystem_device_id", device.pci.subsystem_device_id),
        ("numa_node", device.pci.numa_node),
        ("iommu_group", device.pci.iommu_group),
        ("pci_current_link_speed", device.pci.current_link_speed),
        ("pci_current_link_width", device.pci.current_link_width),
        ("pci_max_link_speed", device.pci.max_link_speed),
        ("pci_max_link_width", device.pci.max_link_width),
        ("pci_reset_method", device.pci.reset_method),
    ]


def _workload_labels(device: DeviceTelemetry, workload: MetaliumWorkloadTelemetry) -> list[tuple[str, object | None]]:
    return _device_labels(device) + [
        ("workload_id", workload.workload_id),
        ("pod_namespace", workload.pod_namespace),
        ("pod_name", workload.pod_name),
        ("container_name", workload.container_name),
    ]


SampleWriter = Callable[[DeviceTelemetry], Iterable[str]]


class _Renderer:
    def __init__(self, devices: list[DeviceTelemetry]) -> None:
        self.devices = devices
        self.lines: list[str] = []

    def family(self, name: str, help_text: str, metric_type: str, writer: SampleWriter) -> None:
        self.lines += [f"# HELP {name} {help_text}", f"# TYPE {name} {metric_type}"]
        for device in self.devices:
            self.lines.extend(writer(device))
        self.lines.append("")

    def optional(self, name: str, help_text: str, metric_type: str, getter: Callable[[DeviceTelemetry], object | None]) -> None:
        def samples(device: DeviceTelemetry) -> Iterable[str]:
            value = getter(device)
            if value is not None:
                yield f"{name}{{{_labels(_device_labels(device))}}} {value}"

        self.family(name, help_text, metric_type, samples)

    def workload(self, name: str, help_text: str, writer: Callable[[DeviceTelemetry, MetaliumWorkloadTelemetry], str | None]) -> None:
        def samples(device: DeviceTelemetry) -> Iterable[str]:
            for workload in device.metalium_workloads:
                sample = writer(device, workload)
                if sample is not None:
                    yield sample

        self.family(name, help_text, "gauge", samples)


def render_prometheus(devices: list[DeviceTelemetry]) -> str:
    """Render all public Prometheus metric families in contract order."""
    out = _Renderer(devices)
    out.lines += [
        "# HELP tt_devices_discovered Number of Tenstorrent sysfs devices discovered.",
        "# TYPE tt_devices_discovered gauge",
        f"tt_devices_discovered {len(devices)}",
        "",
    ]

    for name, help_text in (
        ("tt_device_info", "Static Tenstorrent device identity and PCI metadata."),
        ("tt_device_present", "Whether a Tenstorrent sysfs device is present."),
    ):
        out.family(name, help_text, "gauge", lambda d, n=name: (f"{n}{{{_labels(_info_labels(d))}}} 1",))

    def firmware_labels(d: DeviceTelemetry) -> list[tuple[str, object]]:
        return _device_labels(d) + [
            ("serial", d.firmware.serial),
            ("card_type", d.firmware.card_type),
            ("asic_id", d.firmware.asic_id),
            ("fw_bundle_version", d.firmware.fw_bundle_version),
            ("m3_app_fw_version", d.firmware.m3_app_fw_version),
            ("m3_bl_fw_version", d.firmware.m3_bl_fw_version),
            ("arc_fw_version", d.firmware.arc_fw_version),
            ("eth_fw_version", d.firmware.eth_fw_version),
            ("ttflash_version", d.firmware.ttflash_version),
        ]

    out.family("tt_firmware_info", "Firmware-reported Tenstorrent board and firmware identity.", "gauge", lambda d: (f"tt_firmware_info{{{_labels(firmware_labels(d))}}} 1",))

    def clocks(device: DeviceTelemetry) -> Iterable[str]:
        for clock, value in (("ai", device.firmware.ai_clock_mhz), ("axi", device.firmware.axi_clock_mhz), ("arc", device.firmware.arc_clock_mhz)):
            if value is not None:
                yield (f"tt_firmware_clock_frequency_mhz{{{_labels(_device_labels(device) + [('clock', clock)])}}} {value}")

    out.family("tt_firmware_clock_frequency_mhz", "Firmware-reported Tenstorrent clock frequencies in MHz.", "gauge", clocks)

    optional_families: list[tuple[str, str, str, Callable[[DeviceTelemetry], Any]]] = [
        ("tt_firmware_heartbeat_total", "Firmware heartbeat counter; increasing values indicate live firmware.", "counter", lambda d: d.firmware.heartbeat),
        ("tt_thermal_trips_total", "Firmware-reported critical thermal trip count since power cycle.", "counter", lambda d: d.firmware.thermal_trip_count),
    ]
    for spec in optional_families:
        out.optional(*spec)

    def sensors(device: DeviceTelemetry) -> Iterable[str]:
        for item in device.hwmon_sensors:
            labels = _device_labels(device) + [("sensor", item.name), ("label", item.label), ("unit", item.unit)]
            yield f"tt_hwmon_sensor_value{{{_labels(labels)}}} {item.value}"

    out.family("tt_hwmon_sensor_value", "Raw Tenstorrent hwmon sensor value with Linux hwmon units.", "gauge", sensors)

    def resources(device: DeviceTelemetry) -> Iterable[str]:
        for item in device.pci_resources:
            labels = _device_labels(device) + [("resource", item.index), ("start", hex(item.start)), ("end", hex(item.end)), ("flags", hex(item.flags))]
            yield f"tt_pci_resource_size_bytes{{{_labels(labels)}}} {item.size_bytes}"

    out.family("tt_pci_resource_size_bytes", "Size of non-empty PCI resource ranges exposed by the device.", "gauge", resources)

    def link_info(device: DeviceTelemetry) -> Iterable[str]:
        for state, speed in (("current", device.pci.current_link_speed), ("max", device.pci.max_link_speed)):
            if speed is not None:
                yield f"tt_pci_link_info{{{_labels(_device_labels(device) + [('state', state), ('speed', speed)])}}} 1"

    out.family("tt_pci_link_info", "PCIe link speed labels read from backing PCI sysfs.", "gauge", link_info)

    def link_width(device: DeviceTelemetry) -> Iterable[str]:
        for state, width in (("current", device.pci.current_link_width), ("max", device.pci.max_link_width)):
            if width is not None:
                yield f"tt_pci_link_width_lanes{{{_labels(_device_labels(device) + [('state', state)])}}} {width}"

    out.family("tt_pci_link_width_lanes", "PCIe link width in lanes read from backing PCI sysfs.", "gauge", link_width)
    out.family("tt_power_state_info", "Runtime power-management state labels for the device.", "gauge", lambda d: (f"tt_power_state_info{{{_labels(_device_labels(d) + [('runtime_status', d.power.runtime_status), ('control', d.power.control), ('runtime_enabled', d.power.runtime_enabled)])}}} 1",))

    optional_families = [
        ("tt_power_runtime_active_time_ms", "Milliseconds that Linux runtime PM reports the device active.", "gauge", lambda d: d.power.runtime_active_time_ms),
        ("tt_power_runtime_suspended_time_ms", "Milliseconds that Linux runtime PM reports the device suspended.", "gauge", lambda d: d.power.runtime_suspended_time_ms),
        ("tt_power_runtime_usage", "Linux runtime PM usage count for the device.", "gauge", lambda d: d.power.runtime_usage),
        ("tt_power_runtime_active_children", "Linux runtime PM active child count for the device.", "gauge", lambda d: d.power.runtime_active_kids),
        ("tt_power_autosuspend_delay_ms", "Linux runtime PM autosuspend delay in milliseconds.", "gauge", lambda d: d.power.autosuspend_delay_ms),
    ]
    for spec in optional_families:
        out.optional(*spec)

    def counters(device: DeviceTelemetry) -> Iterable[str]:
        for item in device.pcie_counters:
            yield f"tt_pcie_perf_counter_words_total{{{_labels(_device_labels(device) + [('counter', item.name)])}}} {item.value}"

    out.family("tt_pcie_perf_counter_words_total", "Optional PCIe performance counter values in words.", "counter", counters)

    optional_families = [
        ("tt_memory_used_bytes", "Tenstorrent device memory currently used in bytes.", "gauge", lambda d: d.memory.used_bytes),
        ("tt_memory_total_bytes", "Tenstorrent device total memory in bytes.", "gauge", lambda d: d.memory.total_bytes),
        ("tt_memory_free_bytes", "Tenstorrent device memory free in bytes.", "gauge", lambda d: d.memory.free_bytes),
        ("tt_memory_available_bytes", "Tenstorrent device memory available in bytes.", "gauge", lambda d: d.memory.available_bytes),
        ("tt_memory_bandwidth_bytes_per_second", "Observed or reported local memory bandwidth in bytes per second.", "gauge", lambda d: d.memory.bandwidth_bytes_per_second),
    ]
    for spec in optional_families:
        out.optional(*spec)

    def memory_info(device: DeviceTelemetry) -> Iterable[str]:
        if device.memory.type is not None or device.memory.controller_layout is not None:
            labels = _device_labels(device) + [("type", device.memory.type), ("controller_layout", device.memory.controller_layout)]
            yield f"tt_memory_info{{{_labels(labels)}}} 1"

    out.family("tt_memory_info", "Memory technology and controller layout reported by safe sources.", "gauge", memory_info)

    optional_families = [
        ("tt_memory_controller_count", "Memory controller count reported by a safe source.", "gauge", lambda d: d.memory.controller_count),
        ("tt_memory_controllers_per_asic", "Memory controllers per ASIC reported by a safe source.", "gauge", lambda d: d.memory.controllers_per_asic),
        ("tt_memory_channel_count", "Memory channel count reported by a safe source.", "gauge", lambda d: d.memory.channel_count),
        ("tt_tensix_cores_used", "Tenstorrent Tensix cores currently active or reserved.", "gauge", lambda d: d.tensix.used),
        ("tt_tensix_cores_available", "Tenstorrent Tensix cores available on the device.", "gauge", lambda d: d.tensix.available),
        ("tt_tensix_cores_total", "Total Tensix cores reported by a safe source.", "gauge", lambda d: d.tensix.total),
        ("tt_tensix_mesh_rows", "Tensix mesh row count reported by a safe source.", "gauge", lambda d: d.tensix.mesh_rows),
        ("tt_tensix_mesh_cols", "Tensix mesh column count reported by a safe source.", "gauge", lambda d: d.tensix.mesh_cols),
    ]
    for spec in optional_families:
        out.optional(*spec)

    def tensix_info(device: DeviceTelemetry) -> Iterable[str]:
        if device.tensix.topology is not None or device.tensix.active_regions is not None:
            labels = _device_labels(device) + [("topology", device.tensix.topology), ("active_regions", device.tensix.active_regions)]
            yield f"tt_tensix_info{{{_labels(labels)}}} 1"

    out.family("tt_tensix_info", "Tensix topology labels reported by a safe source.", "gauge", tensix_info)

    def workload_sample(name: str, device: DeviceTelemetry, workload: MetaliumWorkloadTelemetry, value: object) -> str:
        return f"{name}{{{_labels(_workload_labels(device, workload))}}} {value}"

    out.workload("tt_metalium_workload_active", "Whether a fresh TT-Metalium workload profiler sample reported activity.", lambda d, w: workload_sample("tt_metalium_workload_active", d, w, int(not w.stale and (w.active or 0) != 0)))
    out.workload("tt_metalium_workload_profile_stale", "Whether the TT-Metalium workload profiler sample exceeded its freshness window.", lambda d, w: workload_sample("tt_metalium_workload_profile_stale", d, w, int(w.stale)))

    def optional_workload(name: str, help_text: str, attribute: str, fresh_only: bool = True) -> None:
        def sample(d: DeviceTelemetry, w: MetaliumWorkloadTelemetry) -> str | None:
            value = getattr(w, attribute)
            return None if value is None or (fresh_only and w.stale) else workload_sample(name, d, w, value)

        out.workload(name, help_text, sample)

    optional_workload("tt_metalium_workload_tensix_cores_used", "Maximum Tensix core footprint among programs in the latest fresh TT-Metalium sample.", "tensix_cores_used")
    optional_workload("tt_metalium_workload_tensix_cores_total", "Tensix cores available to programs in the latest fresh TT-Metalium sample.", "tensix_cores_total")

    def occupancy(d: DeviceTelemetry, w: MetaliumWorkloadTelemetry) -> str | None:
        if w.stale or w.tensix_cores_used is None or not w.tensix_cores_total or w.tensix_cores_total < 0:
            return None
        return workload_sample("tt_metalium_workload_core_occupancy_ratio", d, w, format(w.tensix_cores_used / w.tensix_cores_total, ".6g"))

    out.workload("tt_metalium_workload_core_occupancy_ratio", "Latest profiled program core footprint divided by available Tensix cores; this is spatial occupancy, not time-weighted busy percentage.", occupancy)
    optional_workload("tt_metalium_workload_programs_observed", "Programs represented by the latest fresh TT-Metalium profiler read.", "programs_observed")
    optional_workload("tt_metalium_workload_sample_timestamp_seconds", "Unix timestamp of the latest TT-Metalium workload profiler sample.", "sample_timestamp_seconds", False)

    def health_info(device: DeviceTelemetry) -> Iterable[str]:
        health = device.health_detail
        if health.fault_code is not None or health.fault_reason is not None:
            yield f"tt_health_fault_info{{{_labels(_device_labels(device) + [('fault_code', health.fault_code), ('fault_reason', health.fault_reason)])}}} 1"

    out.family("tt_health_fault_info", "Fault details reported by safe driver or janitor sources.", "gauge", health_info)
    for spec in [
        ("tt_health_reset_required", "Whether a safe source reports that the device requires reset.", "gauge", lambda d: d.health_detail.reset_required),
        ("tt_health_oom_faults_total", "OOM fault count reported by a safe source.", "counter", lambda d: d.health_detail.oom_fault_count),
        ("tt_health_hang_faults_total", "Device hang fault count reported by a safe source.", "counter", lambda d: d.health_detail.hang_fault_count),
    ]:
        out.optional(*spec)

    def interconnect_info(device: DeviceTelemetry) -> Iterable[str]:
        for link in device.interconnect_links:
            labels = _device_labels(device) + [("link", link.name), ("type", link.type), ("state", link.state), ("peer", link.peer), ("ring_id", link.ring_id)]
            yield f"tt_interconnect_link_info{{{_labels(labels)}}} 1"

    out.family("tt_interconnect_link_info", "Scale-out or fabric link state reported by safe sources.", "gauge", interconnect_info)

    def interconnect_speed(device: DeviceTelemetry) -> Iterable[str]:
        for link in device.interconnect_links:
            if link.speed_gbps is not None:
                yield f"tt_interconnect_link_speed_gbps{{{_labels(_device_labels(device) + [('link', link.name)])}}} {link.speed_gbps}"

    out.family("tt_interconnect_link_speed_gbps", "Interconnect link speed in Gbps reported by safe sources.", "gauge", interconnect_speed)

    def allocation(device: DeviceTelemetry) -> Iterable[str]:
        item = device.allocation
        values = [item.claim_namespace, item.claim_name, item.claim_uid, item.pod_namespace, item.pod_name, item.container_name]
        if any(value is not None for value in values):
            names = ["claim_namespace", "claim_name", "claim_uid", "pod_namespace", "pod_name", "container_name"]
            yield f"tt_dra_allocation_info{{{_labels(_device_labels(device) + list(zip(names, values)))}}} 1"

    out.family("tt_dra_allocation_info", "Kubernetes DRA allocation labels from a node-local state source.", "gauge", allocation)

    def janitor(device: DeviceTelemetry) -> Iterable[str]:
        item = device.janitor
        presence = [item.state, item.quarantine_reason, item.last_scrub_status, item.last_reset_status, item.scrub_count, item.reset_count, item.last_scrub_timestamp_seconds, item.last_reset_timestamp_seconds]
        if any(value is not None for value in presence):
            labels = _device_labels(device) + [("state", item.state), ("quarantine_reason", item.quarantine_reason), ("last_scrub_status", item.last_scrub_status), ("last_reset_status", item.last_reset_status)]
            yield f"tt_janitor_state_info{{{_labels(labels)}}} 1"

    out.family("tt_janitor_state_info", "Hardware janitor state labels from a node-local state source.", "gauge", janitor)
    for spec in [
        ("tt_janitor_scrub_total", "Total scrub attempts reported by the hardware janitor.", "counter", lambda d: d.janitor.scrub_count),
        ("tt_janitor_reset_total", "Total reset attempts reported by the hardware janitor.", "counter", lambda d: d.janitor.reset_count),
        ("tt_janitor_last_scrub_timestamp_seconds", "Unix timestamp for the last scrub attempt.", "gauge", lambda d: d.janitor.last_scrub_timestamp_seconds),
        ("tt_janitor_last_reset_timestamp_seconds", "Unix timestamp for the last reset attempt.", "gauge", lambda d: d.janitor.last_reset_timestamp_seconds),
    ]:
        out.optional(*spec)
    return "\n".join(out.lines) + "\n"
