#include "tt_metrics_exporter/json.hpp"

#include <ostream>
#include <sstream>

namespace tt::metrics {
namespace {

std::string json_escape(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char c : value) {
    switch (c) {
      case '"':
        escaped.append("\\\"");
        break;
      case '\\':
        escaped.append("\\\\");
        break;
      case '\b':
        escaped.append("\\b");
        break;
      case '\f':
        escaped.append("\\f");
        break;
      case '\n':
        escaped.append("\\n");
        break;
      case '\r':
        escaped.append("\\r");
        break;
      case '\t':
        escaped.append("\\t");
        break;
      default:
        escaped.push_back(c);
        break;
    }
  }
  return escaped;
}

void write_string(std::ostream& output, std::string_view value) {
  output << '"' << json_escape(value) << '"';
}

void write_optional_string(std::ostream& output,
                           const std::optional<std::string>& value) {
  if (!value.has_value()) {
    output << "null";
    return;
  }
  write_string(output, *value);
}

template <typename T>
void write_optional_number(std::ostream& output, const std::optional<T>& value) {
  if (!value.has_value()) {
    output << "null";
    return;
  }
  output << *value;
}

void write_field_name(std::ostream& output, std::string_view name) {
  write_string(output, name);
  output << ':';
}

template <typename Writer>
void write_field(std::ostream& output, std::string_view name, Writer writer,
                 bool& first) {
  if (!first) {
    output << ',';
  }
  first = false;
  write_field_name(output, name);
  writer();
}

void write_string_field(std::ostream& output, std::string_view name,
                        std::string_view value, bool& first) {
  write_field(output, name, [&]() { write_string(output, value); }, first);
}

void write_optional_string_field(std::ostream& output, std::string_view name,
                                 const std::optional<std::string>& value,
                                 bool& first) {
  write_field(output, name, [&]() { write_optional_string(output, value); },
              first);
}

template <typename T>
void write_optional_number_field(std::ostream& output, std::string_view name,
                                 const std::optional<T>& value, bool& first) {
  write_field(output, name, [&]() { write_optional_number(output, value); },
              first);
}

void write_number_field(std::ostream& output, std::string_view name,
                        std::int64_t value, bool& first) {
  write_field(output, name, [&]() { output << value; }, first);
}

void write_uint_field(std::ostream& output, std::string_view name,
                      std::uint64_t value, bool& first) {
  write_field(output, name, [&]() { output << value; }, first);
}

void write_character_device(std::ostream& output,
                            const std::optional<CharacterDevice>& device) {
  if (!device.has_value()) {
    output << "null";
    return;
  }

  bool first = true;
  output << '{';
  write_uint_field(output, "major", device->major, first);
  write_uint_field(output, "minor", device->minor, first);
  write_optional_string_field(output, "name", device->dev_name, first);
  if (device->dev_name.has_value()) {
    write_field(output, "path",
                [&]() {
                  write_string(output, "/dev/" + *device->dev_name);
                },
                first);
  } else {
    write_field(output, "path", [&]() { output << "null"; }, first);
  }
  output << '}';
}

void write_pci_resources(std::ostream& output,
                         const std::vector<PciResource>& resources) {
  output << '[';
  for (std::size_t index = 0; index < resources.size(); ++index) {
    if (index > 0) {
      output << ',';
    }
    const auto& resource = resources[index];
    bool first = true;
    output << '{';
    write_number_field(output, "index", resource.index, first);
    write_uint_field(output, "start", resource.start, first);
    write_uint_field(output, "end", resource.end, first);
    write_uint_field(output, "flags", resource.flags, first);
    write_uint_field(output, "sizeBytes", resource.size_bytes, first);
    output << '}';
  }
  output << ']';
}

void write_pci(std::ostream& output, const DeviceTelemetry& device) {
  bool first = true;
  output << '{';
  write_optional_string_field(output, "bdf", device.pci.bdf, first);
  write_optional_string_field(output, "driver", device.pci.driver, first);
  write_optional_string_field(output, "vendorId", device.pci.vendor_id, first);
  write_optional_string_field(output, "deviceId", device.pci.device_id, first);
  write_optional_string_field(output, "classId", device.pci.class_id, first);
  write_optional_string_field(output, "revision", device.pci.revision, first);
  write_optional_string_field(output, "subsystemVendorId",
                              device.pci.subsystem_vendor_id, first);
  write_optional_string_field(output, "subsystemDeviceId",
                              device.pci.subsystem_device_id, first);
  write_optional_number_field(output, "numaNode", device.pci.numa_node, first);
  write_optional_number_field(output, "iommuGroup", device.pci.iommu_group,
                              first);
  write_optional_string_field(output, "currentLinkSpeed",
                              device.pci.current_link_speed, first);
  write_optional_number_field(output, "currentLinkWidth",
                              device.pci.current_link_width, first);
  write_optional_string_field(output, "maxLinkSpeed",
                              device.pci.max_link_speed, first);
  write_optional_number_field(output, "maxLinkWidth",
                              device.pci.max_link_width, first);
  write_optional_string_field(output, "resetMethod", device.pci.reset_method,
                              first);
  write_field(output, "resources",
              [&]() { write_pci_resources(output, device.pci_resources); },
              first);
  output << '}';
}

void write_power(std::ostream& output, const PowerInfo& power) {
  bool first = true;
  output << '{';
  write_optional_string_field(output, "runtimeStatus", power.runtime_status,
                              first);
  write_optional_string_field(output, "control", power.control, first);
  write_optional_string_field(output, "runtimeEnabled", power.runtime_enabled,
                              first);
  write_optional_number_field(output, "runtimeActiveTimeMs",
                              power.runtime_active_time_ms, first);
  write_optional_number_field(output, "runtimeSuspendedTimeMs",
                              power.runtime_suspended_time_ms, first);
  write_optional_number_field(output, "runtimeUsage", power.runtime_usage,
                              first);
  write_optional_number_field(output, "runtimeActiveChildren",
                              power.runtime_active_kids, first);
  write_optional_number_field(output, "autosuspendDelayMs",
                              power.autosuspend_delay_ms, first);
  output << '}';
}

void write_firmware(std::ostream& output, const FirmwareTelemetry& firmware) {
  bool first = true;
  output << '{';
  write_optional_number_field(output, "aiClockMhz", firmware.ai_clock_mhz,
                              first);
  write_optional_number_field(output, "axiClockMhz", firmware.axi_clock_mhz,
                              first);
  write_optional_number_field(output, "arcClockMhz", firmware.arc_clock_mhz,
                              first);
  write_optional_number_field(output, "heartbeat", firmware.heartbeat, first);
  write_optional_number_field(output, "thermalTripCount",
                              firmware.thermal_trip_count, first);
  write_optional_string_field(output, "serial", firmware.serial, first);
  write_optional_string_field(output, "cardType", firmware.card_type, first);
  write_optional_string_field(output, "asicId", firmware.asic_id, first);
  write_optional_string_field(output, "fwBundleVersion",
                              firmware.fw_bundle_version, first);
  write_optional_string_field(output, "m3AppFwVersion",
                              firmware.m3_app_fw_version, first);
  write_optional_string_field(output, "m3BlFwVersion",
                              firmware.m3_bl_fw_version, first);
  write_optional_string_field(output, "arcFwVersion", firmware.arc_fw_version,
                              first);
  write_optional_string_field(output, "ethFwVersion", firmware.eth_fw_version,
                              first);
  write_optional_string_field(output, "ttflashVersion",
                              firmware.ttflash_version, first);
  output << '}';
}

void write_hwmon(std::ostream& output,
                 const std::vector<HwmonSensor>& sensors) {
  output << '[';
  for (std::size_t index = 0; index < sensors.size(); ++index) {
    if (index > 0) {
      output << ',';
    }
    const auto& sensor = sensors[index];
    bool first = true;
    output << '{';
    write_string_field(output, "name", sensor.name, first);
    write_optional_string_field(output, "label", sensor.label, first);
    write_string_field(output, "unit", sensor.unit, first);
    write_number_field(output, "value", sensor.value, first);
    output << '}';
  }
  output << ']';
}

void write_named_counters(std::ostream& output,
                          const std::vector<NamedCounter>& counters) {
  output << '[';
  for (std::size_t index = 0; index < counters.size(); ++index) {
    if (index > 0) {
      output << ',';
    }
    const auto& counter = counters[index];
    bool first = true;
    output << '{';
    write_string_field(output, "name", counter.name, first);
    write_uint_field(output, "value", counter.value, first);
    output << '}';
  }
  output << ']';
}

void write_memory(std::ostream& output, const DeviceTelemetry& device) {
  bool first = true;
  output << '{';
  write_optional_number_field(output, "usedBytes", device.memory.used_bytes,
                              first);
  write_optional_number_field(output, "totalBytes", device.memory.total_bytes,
                              first);
  write_optional_number_field(output, "freeBytes", device.memory.free_bytes,
                              first);
  write_optional_number_field(output, "availableBytes",
                              device.memory.available_bytes, first);
  write_optional_number_field(output, "bandwidthBytesPerSecond",
                              device.memory.bandwidth_bytes_per_second, first);
  write_optional_string_field(output, "type", device.memory.type, first);
  write_optional_string_field(output, "controllerLayout",
                              device.memory.controller_layout, first);
  write_optional_number_field(output, "controllerCount",
                              device.memory.controller_count, first);
  write_optional_number_field(output, "controllersPerAsic",
                              device.memory.controllers_per_asic, first);
  write_optional_number_field(output, "channelCount",
                              device.memory.channel_count, first);
  output << '}';
}

void write_tensix(std::ostream& output, const DeviceTelemetry& device) {
  bool first = true;
  output << '{';
  write_optional_number_field(output, "used", device.tensix.used, first);
  write_optional_number_field(output, "available", device.tensix.available,
                              first);
  write_optional_number_field(output, "total", device.tensix.total, first);
  write_optional_number_field(output, "meshRows", device.tensix.mesh_rows,
                              first);
  write_optional_number_field(output, "meshCols", device.tensix.mesh_cols,
                              first);
  write_optional_string_field(output, "topology", device.tensix.topology,
                              first);
  write_optional_string_field(output, "activeRegions",
                              device.tensix.active_regions, first);
  write_optional_string_field(output, "source", device.tensix.source, first);
  output << '}';
}

void write_metalium_workloads(
    std::ostream& output,
    const std::vector<MetaliumWorkloadTelemetry>& workloads) {
  output << '[';
  for (std::size_t index = 0; index < workloads.size(); ++index) {
    if (index > 0) {
      output << ',';
    }
    const auto& workload = workloads[index];
    bool first = true;
    output << '{';
    write_string_field(output, "workloadId", workload.workload_id, first);
    write_optional_string_field(output, "podNamespace", workload.pod_namespace,
                                first);
    write_optional_string_field(output, "podName", workload.pod_name, first);
    write_optional_string_field(output, "containerName", workload.container_name,
                                first);
    write_optional_number_field(output, "active", workload.active, first);
    write_optional_number_field(output, "programsObserved",
                                workload.programs_observed, first);
    write_optional_number_field(output, "tensixCoresUsed",
                                workload.tensix_cores_used, first);
    write_optional_number_field(output, "tensixCoresTotal",
                                workload.tensix_cores_total, first);
    write_optional_number_field(output, "sampleTimestampSeconds",
                                workload.sample_timestamp_seconds, first);
    write_field(output, "stale",
                [&]() { output << (workload.stale ? "true" : "false"); },
                first);
    output << '}';
  }
  output << ']';
}

void write_health_detail(std::ostream& output,
                         const HealthTelemetry& health) {
  bool first = true;
  output << '{';
  write_optional_string_field(output, "faultCode", health.fault_code, first);
  write_optional_string_field(output, "faultReason", health.fault_reason,
                              first);
  write_optional_number_field(output, "resetRequired", health.reset_required,
                              first);
  write_optional_number_field(output, "oomFaultCount", health.oom_fault_count,
                              first);
  write_optional_number_field(output, "hangFaultCount", health.hang_fault_count,
                              first);
  output << '}';
}

void write_interconnect_links(std::ostream& output,
                              const std::vector<InterconnectLink>& links) {
  output << '[';
  for (std::size_t index = 0; index < links.size(); ++index) {
    if (index > 0) {
      output << ',';
    }
    const auto& link = links[index];
    bool first = true;
    output << '{';
    write_string_field(output, "name", link.name, first);
    write_optional_string_field(output, "type", link.type, first);
    write_optional_string_field(output, "state", link.state, first);
    write_optional_string_field(output, "peer", link.peer, first);
    write_optional_number_field(output, "speedGbps", link.speed_gbps, first);
    write_optional_string_field(output, "ringId", link.ring_id, first);
    output << '}';
  }
  output << ']';
}

void write_allocation(std::ostream& output,
                      const AllocationTelemetry& allocation) {
  bool first = true;
  output << '{';
  write_optional_string_field(output, "claimNamespace",
                              allocation.claim_namespace, first);
  write_optional_string_field(output, "claimName", allocation.claim_name,
                              first);
  write_optional_string_field(output, "claimUid", allocation.claim_uid, first);
  write_optional_string_field(output, "podNamespace", allocation.pod_namespace,
                              first);
  write_optional_string_field(output, "podName", allocation.pod_name, first);
  write_optional_string_field(output, "containerName",
                              allocation.container_name, first);
  output << '}';
}

void write_janitor(std::ostream& output, const JanitorTelemetry& janitor) {
  bool first = true;
  output << '{';
  write_optional_string_field(output, "state", janitor.state, first);
  write_optional_string_field(output, "quarantineReason",
                              janitor.quarantine_reason, first);
  write_optional_string_field(output, "lastScrubStatus",
                              janitor.last_scrub_status, first);
  write_optional_string_field(output, "lastResetStatus",
                              janitor.last_reset_status, first);
  write_optional_number_field(output, "scrubCount", janitor.scrub_count,
                              first);
  write_optional_number_field(output, "resetCount", janitor.reset_count,
                              first);
  write_optional_number_field(output, "lastScrubTimestampSeconds",
                              janitor.last_scrub_timestamp_seconds, first);
  write_optional_number_field(output, "lastResetTimestampSeconds",
                              janitor.last_reset_timestamp_seconds, first);
  output << '}';
}

void write_device(std::ostream& output, const DeviceTelemetry& device) {
  bool first = true;
  output << '{';
  write_string_field(output, "id", device.id, first);
  write_string_field(output, "sysfsPath", device.sysfs_path.string(), first);
  write_optional_string_field(output, "architecture", device.architecture,
                              first);
  write_optional_string_field(output, "boardType", device.board_type, first);
  write_optional_string_field(output, "health", device.health, first);
  write_field(output, "characterDevice",
              [&]() { write_character_device(output, device.character_device); },
              first);
  write_field(output, "pci", [&]() { write_pci(output, device); }, first);
  write_field(output, "power", [&]() { write_power(output, device.power); },
              first);
  write_field(output, "firmware",
              [&]() { write_firmware(output, device.firmware); }, first);
  write_field(output, "hwmonSensors",
              [&]() { write_hwmon(output, device.hwmon_sensors); }, first);
  write_field(output, "pcieCounters",
              [&]() { write_named_counters(output, device.pcie_counters); },
              first);
  write_field(output, "memory", [&]() { write_memory(output, device); },
              first);
  write_field(output, "tensix", [&]() { write_tensix(output, device); },
              first);
  write_field(output, "metaliumWorkloads",
              [&]() {
                write_metalium_workloads(output, device.metalium_workloads);
              },
              first);
  write_field(output, "healthDetail",
              [&]() { write_health_detail(output, device.health_detail); },
              first);
  write_field(output, "interconnectLinks",
              [&]() { write_interconnect_links(output, device.interconnect_links); },
              first);
  write_field(output, "allocation",
              [&]() { write_allocation(output, device.allocation); }, first);
  write_field(output, "janitor", [&]() { write_janitor(output, device.janitor); },
              first);
  output << '}';
}

}  // namespace

std::string render_devices_json(const std::vector<DeviceTelemetry>& devices) {
  std::ostringstream output;
  bool first = true;

  output << '{';
  write_string_field(output, "apiVersion", "telemetry.tenstorrent.com/v1",
                     first);
  write_string_field(output, "kind", "DeviceList", first);
  write_field(output, "summary",
              [&]() {
                bool summary_first = true;
                output << '{';
                write_number_field(output, "devicesDiscovered",
                                   static_cast<std::int64_t>(devices.size()),
                                   summary_first);
                output << '}';
              },
              first);
  write_field(output, "devices",
              [&]() {
                output << '[';
                for (std::size_t index = 0; index < devices.size(); ++index) {
                  if (index > 0) {
                    output << ',';
                  }
                  write_device(output, devices[index]);
                }
                output << ']';
              },
              first);
  output << "}\n";
  return output.str();
}

}  // namespace tt::metrics
