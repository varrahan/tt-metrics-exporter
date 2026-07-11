"""Stable JSON rendering for the telemetry contract."""

from __future__ import annotations

import json as json_module
from typing import Any

from ..models import DeviceTelemetry


def _device(device: DeviceTelemetry) -> dict[str, Any]:
    character = device.character_device
    return {
        "id": device.id,
        "sysfsPath": str(device.sysfs_path),
        "architecture": device.architecture,
        "boardType": device.board_type,
        "health": device.health,
        "characterDevice": None if character is None else {
            "major": character.major,
            "minor": character.minor,
            "name": character.dev_name,
            "path": None if character.dev_name is None else f"/dev/{character.dev_name}",
        },
        "pci": {
            "bdf": device.pci.bdf,
            "driver": device.pci.driver,
            "vendorId": device.pci.vendor_id,
            "deviceId": device.pci.device_id,
            "classId": device.pci.class_id,
            "revision": device.pci.revision,
            "subsystemVendorId": device.pci.subsystem_vendor_id,
            "subsystemDeviceId": device.pci.subsystem_device_id,
            "numaNode": device.pci.numa_node,
            "iommuGroup": device.pci.iommu_group,
            "currentLinkSpeed": device.pci.current_link_speed,
            "currentLinkWidth": device.pci.current_link_width,
            "maxLinkSpeed": device.pci.max_link_speed,
            "maxLinkWidth": device.pci.max_link_width,
            "resetMethod": device.pci.reset_method,
            "resources": [{
                "index": item.index, "start": item.start, "end": item.end,
                "flags": item.flags, "sizeBytes": item.size_bytes,
            } for item in device.pci_resources],
        },
        "power": {
            "runtimeStatus": device.power.runtime_status,
            "control": device.power.control,
            "runtimeEnabled": device.power.runtime_enabled,
            "runtimeActiveTimeMs": device.power.runtime_active_time_ms,
            "runtimeSuspendedTimeMs": device.power.runtime_suspended_time_ms,
            "runtimeUsage": device.power.runtime_usage,
            "runtimeActiveChildren": device.power.runtime_active_kids,
            "autosuspendDelayMs": device.power.autosuspend_delay_ms,
        },
        "firmware": {
            "aiClockMhz": device.firmware.ai_clock_mhz,
            "axiClockMhz": device.firmware.axi_clock_mhz,
            "arcClockMhz": device.firmware.arc_clock_mhz,
            "heartbeat": device.firmware.heartbeat,
            "thermalTripCount": device.firmware.thermal_trip_count,
            "serial": device.firmware.serial,
            "cardType": device.firmware.card_type,
            "asicId": device.firmware.asic_id,
            "fwBundleVersion": device.firmware.fw_bundle_version,
            "m3AppFwVersion": device.firmware.m3_app_fw_version,
            "m3BlFwVersion": device.firmware.m3_bl_fw_version,
            "arcFwVersion": device.firmware.arc_fw_version,
            "ethFwVersion": device.firmware.eth_fw_version,
            "ttflashVersion": device.firmware.ttflash_version,
        },
        "hwmonSensors": [{"name": item.name, "label": item.label,
                           "unit": item.unit, "value": item.value}
                          for item in device.hwmon_sensors],
        "pcieCounters": [{"name": item.name, "value": item.value}
                         for item in device.pcie_counters],
        "memory": {
            "usedBytes": device.memory.used_bytes,
            "totalBytes": device.memory.total_bytes,
            "freeBytes": device.memory.free_bytes,
            "availableBytes": device.memory.available_bytes,
            "bandwidthBytesPerSecond": device.memory.bandwidth_bytes_per_second,
            "type": device.memory.type,
            "controllerLayout": device.memory.controller_layout,
            "controllerCount": device.memory.controller_count,
            "controllersPerAsic": device.memory.controllers_per_asic,
            "channelCount": device.memory.channel_count,
        },
        "tensix": {
            "used": device.tensix.used, "available": device.tensix.available,
            "total": device.tensix.total, "meshRows": device.tensix.mesh_rows,
            "meshCols": device.tensix.mesh_cols, "topology": device.tensix.topology,
            "activeRegions": device.tensix.active_regions, "source": device.tensix.source,
        },
        "metaliumWorkloads": [{
            "workloadId": item.workload_id, "podNamespace": item.pod_namespace,
            "podName": item.pod_name, "containerName": item.container_name,
            "active": item.active, "programsObserved": item.programs_observed,
            "tensixCoresUsed": item.tensix_cores_used,
            "tensixCoresTotal": item.tensix_cores_total,
            "sampleTimestampSeconds": item.sample_timestamp_seconds,
            "stale": item.stale,
        } for item in device.metalium_workloads],
        "healthDetail": {
            "faultCode": device.health_detail.fault_code,
            "faultReason": device.health_detail.fault_reason,
            "resetRequired": device.health_detail.reset_required,
            "oomFaultCount": device.health_detail.oom_fault_count,
            "hangFaultCount": device.health_detail.hang_fault_count,
        },
        "interconnectLinks": [{
            "name": item.name, "type": item.type, "state": item.state,
            "peer": item.peer, "speedGbps": item.speed_gbps, "ringId": item.ring_id,
        } for item in device.interconnect_links],
        "allocation": {
            "claimNamespace": device.allocation.claim_namespace,
            "claimName": device.allocation.claim_name,
            "claimUid": device.allocation.claim_uid,
            "podNamespace": device.allocation.pod_namespace,
            "podName": device.allocation.pod_name,
            "containerName": device.allocation.container_name,
        },
        "janitor": {
            "state": device.janitor.state,
            "quarantineReason": device.janitor.quarantine_reason,
            "lastScrubStatus": device.janitor.last_scrub_status,
            "lastResetStatus": device.janitor.last_reset_status,
            "scrubCount": device.janitor.scrub_count,
            "resetCount": device.janitor.reset_count,
            "lastScrubTimestampSeconds": device.janitor.last_scrub_timestamp_seconds,
            "lastResetTimestampSeconds": device.janitor.last_reset_timestamp_seconds,
        },
    }


def render_devices_json(devices: list[DeviceTelemetry]) -> str:
    """Render the versioned DeviceList JSON document."""
    document = {
        "apiVersion": "telemetry.tenstorrent.com/v1",
        "kind": "DeviceList",
        "summary": {"devicesDiscovered": len(devices)},
        "devices": [_device(device) for device in devices],
    }
    return json_module.dumps(
        document, ensure_ascii=False, allow_nan=False, separators=(",", ":")
    ) + "\n"
