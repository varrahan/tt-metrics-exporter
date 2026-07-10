#include "tt_metrics_exporter/metrics.hpp"

#include <ostream>
#include <sstream>

namespace tt::metrics {
namespace {

std::string label_value(const std::optional<std::string>& value) {
  if (!value.has_value() || value->empty()) {
    return "unknown";
  }
  return *value;
}

std::string label_value(const std::optional<std::int64_t>& value) {
  if (!value.has_value()) {
    return "unknown";
  }
  return std::to_string(*value);
}

std::string hex_label(std::uint64_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << value;
  return output.str();
}

std::string escape_label(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char c : value) {
    if (c == '\\' || c == '"') {
      escaped.push_back('\\');
      escaped.push_back(c);
    } else if (c == '\n') {
      escaped.append("\\n");
    } else {
      escaped.push_back(c);
    }
  }
  return escaped;
}

void write_help_type(std::ostream& output, std::string_view name,
                     std::string_view help, std::string_view type) {
  output << "# HELP " << name << ' ' << help << '\n'
         << "# TYPE " << name << ' ' << type << '\n';
}

void write_device_labels(std::ostream& output, const DeviceTelemetry& device) {
  output << "device=\"" << escape_label(device.id) << "\",architecture=\""
         << escape_label(label_value(device.architecture)) << "\",board_type=\""
         << escape_label(label_value(device.board_type)) << "\",pci_bdf=\""
         << escape_label(label_value(device.pci.bdf)) << "\"";
}

void write_info_labels(std::ostream& output, const DeviceTelemetry& device) {
  write_device_labels(output, device);
  output << ",sysfs_path=\"" << escape_label(device.sysfs_path.string())
         << "\",health=\"" << escape_label(label_value(device.health)) << "\"";
  if (device.character_device.has_value()) {
    output << ",dev_major=\"" << device.character_device->major
           << "\",dev_minor=\"" << device.character_device->minor
           << "\",dev_name=\""
           << escape_label(label_value(device.character_device->dev_name))
           << "\"";
  } else {
    output << ",dev_major=\"unknown\",dev_minor=\"unknown\",dev_name=\"unknown\"";
  }

  output << ",pci_vendor_id=\"" << escape_label(label_value(device.pci.vendor_id))
         << "\",pci_device_id=\""
         << escape_label(label_value(device.pci.device_id))
         << "\",pci_driver=\"" << escape_label(label_value(device.pci.driver))
         << "\",pci_class_id=\"" << escape_label(label_value(device.pci.class_id))
         << "\",pci_revision=\""
         << escape_label(label_value(device.pci.revision))
         << "\",pci_subsystem_vendor_id=\""
         << escape_label(label_value(device.pci.subsystem_vendor_id))
         << "\",pci_subsystem_device_id=\""
         << escape_label(label_value(device.pci.subsystem_device_id))
         << "\",numa_node=\"" << escape_label(label_value(device.pci.numa_node))
         << "\",iommu_group=\""
         << escape_label(label_value(device.pci.iommu_group))
         << "\",pci_current_link_speed=\""
         << escape_label(label_value(device.pci.current_link_speed))
         << "\",pci_current_link_width=\""
         << escape_label(label_value(device.pci.current_link_width))
         << "\",pci_max_link_speed=\""
         << escape_label(label_value(device.pci.max_link_speed))
         << "\",pci_max_link_width=\""
         << escape_label(label_value(device.pci.max_link_width))
         << "\",pci_reset_method=\""
         << escape_label(label_value(device.pci.reset_method))
         << "\"";
}

void write_firmware_labels(std::ostream& output, const DeviceTelemetry& device) {
  output << "serial=\"" << escape_label(label_value(device.firmware.serial))
         << "\",card_type=\""
         << escape_label(label_value(device.firmware.card_type))
         << "\",asic_id=\"" << escape_label(label_value(device.firmware.asic_id))
         << "\",fw_bundle_version=\""
         << escape_label(label_value(device.firmware.fw_bundle_version))
         << "\",m3_app_fw_version=\""
         << escape_label(label_value(device.firmware.m3_app_fw_version))
         << "\",m3_bl_fw_version=\""
         << escape_label(label_value(device.firmware.m3_bl_fw_version))
         << "\",arc_fw_version=\""
         << escape_label(label_value(device.firmware.arc_fw_version))
         << "\",eth_fw_version=\""
         << escape_label(label_value(device.firmware.eth_fw_version))
         << "\",ttflash_version=\""
         << escape_label(label_value(device.firmware.ttflash_version)) << "\"";
}

void write_optional_clock(std::ostream& output, const DeviceTelemetry& device,
                          std::string_view clock,
                          const std::optional<std::int64_t>& value) {
  if (!value.has_value()) {
    return;
  }
  output << "tt_firmware_clock_frequency_mhz{";
  write_device_labels(output, device);
  output << ",clock=\"" << clock << "\"} " << *value << '\n';
}

void write_optional_pci_link_info(
    std::ostream& output, const DeviceTelemetry& device, std::string_view state,
    const std::optional<std::string>& speed) {
  if (!speed.has_value()) {
    return;
  }
  output << "tt_pci_link_info{";
  write_device_labels(output, device);
  output << ",state=\"" << state << "\",speed=\""
         << escape_label(*speed) << "\"} 1\n";
}

void write_optional_pci_link_width(
    std::ostream& output, const DeviceTelemetry& device, std::string_view state,
    const std::optional<std::int64_t>& width) {
  if (!width.has_value()) {
    return;
  }
  output << "tt_pci_link_width_lanes{";
  write_device_labels(output, device);
  output << ",state=\"" << state << "\"} " << *width << '\n';
}

void write_optional_uint_metric(std::ostream& output, std::string_view metric,
                                const DeviceTelemetry& device,
                                const std::optional<std::uint64_t>& value) {
  if (!value.has_value()) {
    return;
  }
  output << metric << "{";
  write_device_labels(output, device);
  output << "} " << *value << '\n';
}

void write_optional_int_metric(std::ostream& output, std::string_view metric,
                               const DeviceTelemetry& device,
                               const std::optional<std::int64_t>& value) {
  if (!value.has_value()) {
    return;
  }
  output << metric << "{";
  write_device_labels(output, device);
  output << "} " << *value << '\n';
}

bool has_allocation(const AllocationTelemetry& allocation) {
  return allocation.claim_namespace.has_value() ||
         allocation.claim_name.has_value() || allocation.claim_uid.has_value() ||
         allocation.pod_namespace.has_value() || allocation.pod_name.has_value() ||
         allocation.container_name.has_value();
}

bool has_janitor(const JanitorTelemetry& janitor) {
  return janitor.state.has_value() || janitor.quarantine_reason.has_value() ||
         janitor.last_scrub_status.has_value() ||
         janitor.last_reset_status.has_value() ||
         janitor.scrub_count.has_value() || janitor.reset_count.has_value() ||
         janitor.last_scrub_timestamp_seconds.has_value() ||
         janitor.last_reset_timestamp_seconds.has_value();
}

void write_metalium_workload_labels(
    std::ostream& output, const DeviceTelemetry& device,
    const MetaliumWorkloadTelemetry& workload) {
  write_device_labels(output, device);
  output << ",workload_id=\"" << escape_label(workload.workload_id)
         << "\",pod_namespace=\""
         << escape_label(label_value(workload.pod_namespace))
         << "\",pod_name=\"" << escape_label(label_value(workload.pod_name))
         << "\",container_name=\""
         << escape_label(label_value(workload.container_name)) << "\"";
}

}  // namespace

std::string render_prometheus(const std::vector<DeviceTelemetry>& devices) {
  std::ostringstream output;

  write_help_type(output, "tt_devices_discovered",
                  "Number of Tenstorrent sysfs devices discovered.", "gauge");
  output << "tt_devices_discovered " << devices.size() << "\n\n";

  write_help_type(output, "tt_device_info",
                  "Static Tenstorrent device identity and PCI metadata.", "gauge");
  for (const auto& device : devices) {
    output << "tt_device_info{";
    write_info_labels(output, device);
    output << "} 1\n";
  }
  output << '\n';

  write_help_type(output, "tt_device_present",
                  "Whether a Tenstorrent sysfs device is present.", "gauge");
  for (const auto& device : devices) {
    output << "tt_device_present{";
    write_info_labels(output, device);
    output << "} 1\n";
  }
  output << '\n';

  write_help_type(output, "tt_firmware_info",
                  "Firmware-reported Tenstorrent board and firmware identity.",
                  "gauge");
  for (const auto& device : devices) {
    output << "tt_firmware_info{";
    write_device_labels(output, device);
    output << ',';
    write_firmware_labels(output, device);
    output << "} 1\n";
  }
  output << '\n';

  write_help_type(output, "tt_firmware_clock_frequency_mhz",
                  "Firmware-reported Tenstorrent clock frequencies in MHz.",
                  "gauge");
  for (const auto& device : devices) {
    write_optional_clock(output, device, "ai", device.firmware.ai_clock_mhz);
    write_optional_clock(output, device, "axi", device.firmware.axi_clock_mhz);
    write_optional_clock(output, device, "arc", device.firmware.arc_clock_mhz);
  }
  output << '\n';

  write_help_type(output, "tt_firmware_heartbeat",
                  "Firmware heartbeat counter; increasing values indicate live firmware.",
                  "counter");
  for (const auto& device : devices) {
    if (!device.firmware.heartbeat.has_value()) {
      continue;
    }
    output << "tt_firmware_heartbeat{";
    write_device_labels(output, device);
    output << "} " << *device.firmware.heartbeat << '\n';
  }
  output << '\n';

  write_help_type(output, "tt_thermal_trip_count",
                  "Firmware-reported critical thermal trip count since power cycle.",
                  "counter");
  for (const auto& device : devices) {
    if (!device.firmware.thermal_trip_count.has_value()) {
      continue;
    }
    output << "tt_thermal_trip_count{";
    write_device_labels(output, device);
    output << "} " << *device.firmware.thermal_trip_count << '\n';
  }
  output << '\n';

  write_help_type(output, "tt_hwmon_sensor_value",
                  "Raw Tenstorrent hwmon sensor value with Linux hwmon units.",
                  "gauge");
  for (const auto& device : devices) {
    for (const auto& sensor : device.hwmon_sensors) {
      output << "tt_hwmon_sensor_value{";
      write_device_labels(output, device);
      output << ",sensor=\"" << escape_label(sensor.name) << "\",label=\""
             << escape_label(label_value(sensor.label)) << "\",unit=\""
             << escape_label(sensor.unit) << "\"} " << sensor.value << '\n';
    }
  }
  output << '\n';

  write_help_type(output, "tt_pci_resource_size_bytes",
                  "Size of non-empty PCI resource ranges exposed by the device.",
                  "gauge");
  for (const auto& device : devices) {
    for (const auto& resource : device.pci_resources) {
      output << "tt_pci_resource_size_bytes{";
      write_device_labels(output, device);
      output << ",resource=\"" << resource.index << "\",start=\""
             << escape_label(hex_label(resource.start)) << "\",end=\""
             << escape_label(hex_label(resource.end)) << "\",flags=\""
             << escape_label(hex_label(resource.flags)) << "\"} "
             << resource.size_bytes << '\n';
    }
  }
  output << '\n';

  write_help_type(output, "tt_pci_link_info",
                  "PCIe link speed labels read from backing PCI sysfs.",
                  "gauge");
  for (const auto& device : devices) {
    write_optional_pci_link_info(output, device, "current",
                                 device.pci.current_link_speed);
    write_optional_pci_link_info(output, device, "max",
                                 device.pci.max_link_speed);
  }
  output << '\n';

  write_help_type(output, "tt_pci_link_width_lanes",
                  "PCIe link width in lanes read from backing PCI sysfs.",
                  "gauge");
  for (const auto& device : devices) {
    write_optional_pci_link_width(output, device, "current",
                                  device.pci.current_link_width);
    write_optional_pci_link_width(output, device, "max",
                                  device.pci.max_link_width);
  }
  output << '\n';

  write_help_type(output, "tt_power_state_info",
                  "Runtime power-management state labels for the device.",
                  "gauge");
  for (const auto& device : devices) {
    output << "tt_power_state_info{";
    write_device_labels(output, device);
    output << ",runtime_status=\""
           << escape_label(label_value(device.power.runtime_status))
           << "\",control=\"" << escape_label(label_value(device.power.control))
           << "\",runtime_enabled=\""
           << escape_label(label_value(device.power.runtime_enabled)) << "\"} 1\n";
  }
  output << '\n';

  write_help_type(output, "tt_power_runtime_active_time_ms",
                  "Milliseconds that Linux runtime PM reports the device active.",
                  "gauge");
  for (const auto& device : devices) {
    if (!device.power.runtime_active_time_ms.has_value()) {
      continue;
    }
    output << "tt_power_runtime_active_time_ms{";
    write_device_labels(output, device);
    output << "} " << *device.power.runtime_active_time_ms << '\n';
  }
  output << '\n';

  write_help_type(
      output, "tt_power_runtime_suspended_time_ms",
      "Milliseconds that Linux runtime PM reports the device suspended.",
      "gauge");
  for (const auto& device : devices) {
    if (!device.power.runtime_suspended_time_ms.has_value()) {
      continue;
    }
    output << "tt_power_runtime_suspended_time_ms{";
    write_device_labels(output, device);
    output << "} " << *device.power.runtime_suspended_time_ms << '\n';
  }
  output << '\n';

  write_help_type(output, "tt_power_runtime_usage",
                  "Linux runtime PM usage count for the device.", "gauge");
  for (const auto& device : devices) {
    if (!device.power.runtime_usage.has_value()) {
      continue;
    }
    output << "tt_power_runtime_usage{";
    write_device_labels(output, device);
    output << "} " << *device.power.runtime_usage << '\n';
  }
  output << '\n';

  write_help_type(output, "tt_power_runtime_active_children",
                  "Linux runtime PM active child count for the device.", "gauge");
  for (const auto& device : devices) {
    if (!device.power.runtime_active_kids.has_value()) {
      continue;
    }
    output << "tt_power_runtime_active_children{";
    write_device_labels(output, device);
    output << "} " << *device.power.runtime_active_kids << '\n';
  }
  output << '\n';

  write_help_type(output, "tt_power_autosuspend_delay_ms",
                  "Linux runtime PM autosuspend delay in milliseconds.", "gauge");
  for (const auto& device : devices) {
    if (!device.power.autosuspend_delay_ms.has_value()) {
      continue;
    }
    output << "tt_power_autosuspend_delay_ms{";
    write_device_labels(output, device);
    output << "} " << *device.power.autosuspend_delay_ms << '\n';
  }
  output << '\n';

  write_help_type(output, "tt_pcie_perf_counter_words_total",
                  "Optional PCIe performance counter values in words.", "counter");
  for (const auto& device : devices) {
    for (const auto& counter : device.pcie_counters) {
      output << "tt_pcie_perf_counter_words_total{";
      write_device_labels(output, device);
      output << ",counter=\"" << escape_label(counter.name) << "\"} "
             << counter.value << '\n';
    }
  }
  output << '\n';

  write_help_type(output, "tt_memory_used_bytes",
                  "Tenstorrent device memory currently used in bytes.", "gauge");
  for (const auto& device : devices) {
    write_optional_uint_metric(output, "tt_memory_used_bytes", device,
                               device.memory.used_bytes);
  }
  output << '\n';

  write_help_type(output, "tt_memory_total_bytes",
                  "Tenstorrent device total memory in bytes.", "gauge");
  for (const auto& device : devices) {
    write_optional_uint_metric(output, "tt_memory_total_bytes", device,
                               device.memory.total_bytes);
  }
  output << '\n';

  write_help_type(output, "tt_memory_free_bytes",
                  "Tenstorrent device memory free in bytes.", "gauge");
  for (const auto& device : devices) {
    write_optional_uint_metric(output, "tt_memory_free_bytes", device,
                               device.memory.free_bytes);
  }
  output << '\n';

  write_help_type(output, "tt_memory_available_bytes",
                  "Tenstorrent device memory available in bytes.", "gauge");
  for (const auto& device : devices) {
    write_optional_uint_metric(output, "tt_memory_available_bytes", device,
                               device.memory.available_bytes);
  }
  output << '\n';

  write_help_type(output, "tt_memory_bandwidth_bytes_per_second",
                  "Observed or reported local memory bandwidth in bytes per second.",
                  "gauge");
  for (const auto& device : devices) {
    write_optional_uint_metric(output, "tt_memory_bandwidth_bytes_per_second",
                               device, device.memory.bandwidth_bytes_per_second);
  }
  output << '\n';

  write_help_type(output, "tt_memory_info",
                  "Memory technology and controller layout reported by safe sources.",
                  "gauge");
  for (const auto& device : devices) {
    if (!device.memory.type.has_value() &&
        !device.memory.controller_layout.has_value()) {
      continue;
    }
    output << "tt_memory_info{";
    write_device_labels(output, device);
    output << ",type=\"" << escape_label(label_value(device.memory.type))
           << "\",controller_layout=\""
           << escape_label(label_value(device.memory.controller_layout))
           << "\"} 1\n";
  }
  output << '\n';

  write_help_type(output, "tt_memory_controller_count",
                  "Memory controller count reported by a safe source.", "gauge");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_memory_controller_count", device,
                              device.memory.controller_count);
  }
  output << '\n';

  write_help_type(output, "tt_memory_controllers_per_asic",
                  "Memory controllers per ASIC reported by a safe source.",
                  "gauge");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_memory_controllers_per_asic", device,
                              device.memory.controllers_per_asic);
  }
  output << '\n';

  write_help_type(output, "tt_memory_channel_count",
                  "Memory channel count reported by a safe source.", "gauge");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_memory_channel_count", device,
                              device.memory.channel_count);
  }
  output << '\n';

  write_help_type(output, "tt_tensix_cores_used",
                  "Tenstorrent Tensix cores currently active or reserved.",
                  "gauge");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_tensix_cores_used", device,
                              device.tensix.used);
  }
  output << '\n';

  write_help_type(output, "tt_tensix_cores_available",
                  "Tenstorrent Tensix cores available on the device.", "gauge");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_tensix_cores_available", device,
                              device.tensix.available);
  }
  output << '\n';

  write_help_type(output, "tt_tensix_cores_total",
                  "Total Tensix cores reported by a safe source.", "gauge");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_tensix_cores_total", device,
                              device.tensix.total);
  }
  output << '\n';

  write_help_type(output, "tt_tensix_mesh_rows",
                  "Tensix mesh row count reported by a safe source.", "gauge");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_tensix_mesh_rows", device,
                              device.tensix.mesh_rows);
  }
  output << '\n';

  write_help_type(output, "tt_tensix_mesh_cols",
                  "Tensix mesh column count reported by a safe source.", "gauge");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_tensix_mesh_cols", device,
                              device.tensix.mesh_cols);
  }
  output << '\n';

  write_help_type(output, "tt_tensix_info",
                  "Tensix topology labels reported by a safe source.", "gauge");
  for (const auto& device : devices) {
    if (!device.tensix.topology.has_value() &&
        !device.tensix.active_regions.has_value()) {
      continue;
    }
    output << "tt_tensix_info{";
    write_device_labels(output, device);
    output << ",topology=\"" << escape_label(label_value(device.tensix.topology))
           << "\",active_regions=\""
           << escape_label(label_value(device.tensix.active_regions))
           << "\"} 1\n";
  }
  output << '\n';

  write_help_type(
      output, "tt_metalium_workload_active",
      "Whether a fresh TT-Metalium workload profiler sample reported activity.",
      "gauge");
  for (const auto& device : devices) {
    for (const auto& workload : device.metalium_workloads) {
      output << "tt_metalium_workload_active{";
      write_metalium_workload_labels(output, device, workload);
      output << "} "
             << (!workload.stale && workload.active.value_or(0) != 0 ? 1 : 0)
             << '\n';
    }
  }
  output << '\n';

  write_help_type(output, "tt_metalium_workload_profile_stale",
                  "Whether the TT-Metalium workload profiler sample exceeded its freshness window.",
                  "gauge");
  for (const auto& device : devices) {
    for (const auto& workload : device.metalium_workloads) {
      output << "tt_metalium_workload_profile_stale{";
      write_metalium_workload_labels(output, device, workload);
      output << "} " << (workload.stale ? 1 : 0) << '\n';
    }
  }
  output << '\n';

  write_help_type(
      output, "tt_metalium_workload_tensix_cores_used",
      "Maximum Tensix core footprint among programs in the latest fresh TT-Metalium sample.",
      "gauge");
  for (const auto& device : devices) {
    for (const auto& workload : device.metalium_workloads) {
      if (workload.stale || !workload.tensix_cores_used.has_value()) {
        continue;
      }
      output << "tt_metalium_workload_tensix_cores_used{";
      write_metalium_workload_labels(output, device, workload);
      output << "} " << *workload.tensix_cores_used << '\n';
    }
  }
  output << '\n';

  write_help_type(
      output, "tt_metalium_workload_tensix_cores_total",
      "Tensix cores available to programs in the latest fresh TT-Metalium sample.",
      "gauge");
  for (const auto& device : devices) {
    for (const auto& workload : device.metalium_workloads) {
      if (workload.stale || !workload.tensix_cores_total.has_value()) {
        continue;
      }
      output << "tt_metalium_workload_tensix_cores_total{";
      write_metalium_workload_labels(output, device, workload);
      output << "} " << *workload.tensix_cores_total << '\n';
    }
  }
  output << '\n';

  write_help_type(
      output, "tt_metalium_workload_core_occupancy_ratio",
      "Latest profiled program core footprint divided by available Tensix cores; this is spatial occupancy, not time-weighted busy percentage.",
      "gauge");
  for (const auto& device : devices) {
    for (const auto& workload : device.metalium_workloads) {
      if (workload.stale || !workload.tensix_cores_used.has_value() ||
          !workload.tensix_cores_total.has_value() ||
          *workload.tensix_cores_total <= 0) {
        continue;
      }
      output << "tt_metalium_workload_core_occupancy_ratio{";
      write_metalium_workload_labels(output, device, workload);
      output << "} "
             << static_cast<double>(*workload.tensix_cores_used) /
                    static_cast<double>(*workload.tensix_cores_total)
             << '\n';
    }
  }
  output << '\n';

  write_help_type(
      output, "tt_metalium_workload_programs_observed",
      "Programs represented by the latest fresh TT-Metalium profiler read.",
      "gauge");
  for (const auto& device : devices) {
    for (const auto& workload : device.metalium_workloads) {
      if (workload.stale || !workload.programs_observed.has_value()) {
        continue;
      }
      output << "tt_metalium_workload_programs_observed{";
      write_metalium_workload_labels(output, device, workload);
      output << "} " << *workload.programs_observed << '\n';
    }
  }
  output << '\n';

  write_help_type(
      output, "tt_metalium_workload_sample_timestamp_seconds",
      "Unix timestamp of the latest TT-Metalium workload profiler sample.",
      "gauge");
  for (const auto& device : devices) {
    for (const auto& workload : device.metalium_workloads) {
      if (!workload.sample_timestamp_seconds.has_value()) {
        continue;
      }
      output << "tt_metalium_workload_sample_timestamp_seconds{";
      write_metalium_workload_labels(output, device, workload);
      output << "} " << *workload.sample_timestamp_seconds << '\n';
    }
  }
  output << '\n';

  write_help_type(output, "tt_health_fault_info",
                  "Fault details reported by safe driver or janitor sources.",
                  "gauge");
  for (const auto& device : devices) {
    if (!device.health_detail.fault_code.has_value() &&
        !device.health_detail.fault_reason.has_value()) {
      continue;
    }
    output << "tt_health_fault_info{";
    write_device_labels(output, device);
    output << ",fault_code=\""
           << escape_label(label_value(device.health_detail.fault_code))
           << "\",fault_reason=\""
           << escape_label(label_value(device.health_detail.fault_reason))
           << "\"} 1\n";
  }
  output << '\n';

  write_help_type(output, "tt_health_reset_required",
                  "Whether a safe source reports that the device requires reset.",
                  "gauge");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_health_reset_required", device,
                              device.health_detail.reset_required);
  }
  output << '\n';

  write_help_type(output, "tt_health_oom_faults_total",
                  "OOM fault count reported by a safe source.", "counter");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_health_oom_faults_total", device,
                              device.health_detail.oom_fault_count);
  }
  output << '\n';

  write_help_type(output, "tt_health_hang_faults_total",
                  "Device hang fault count reported by a safe source.", "counter");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_health_hang_faults_total", device,
                              device.health_detail.hang_fault_count);
  }
  output << '\n';

  write_help_type(output, "tt_interconnect_link_info",
                  "Scale-out or fabric link state reported by safe sources.",
                  "gauge");
  for (const auto& device : devices) {
    for (const auto& link : device.interconnect_links) {
      output << "tt_interconnect_link_info{";
      write_device_labels(output, device);
      output << ",link=\"" << escape_label(link.name) << "\",type=\""
             << escape_label(label_value(link.type)) << "\",state=\""
             << escape_label(label_value(link.state)) << "\",peer=\""
             << escape_label(label_value(link.peer)) << "\",ring_id=\""
             << escape_label(label_value(link.ring_id)) << "\"} 1\n";
    }
  }
  output << '\n';

  write_help_type(output, "tt_interconnect_link_speed_gbps",
                  "Interconnect link speed in Gbps reported by safe sources.",
                  "gauge");
  for (const auto& device : devices) {
    for (const auto& link : device.interconnect_links) {
      if (!link.speed_gbps.has_value()) {
        continue;
      }
      output << "tt_interconnect_link_speed_gbps{";
      write_device_labels(output, device);
      output << ",link=\"" << escape_label(link.name) << "\"} "
             << *link.speed_gbps << '\n';
    }
  }
  output << '\n';

  write_help_type(output, "tt_dra_allocation_info",
                  "Kubernetes DRA allocation labels from a node-local state source.",
                  "gauge");
  for (const auto& device : devices) {
    if (!has_allocation(device.allocation)) {
      continue;
    }
    output << "tt_dra_allocation_info{";
    write_device_labels(output, device);
    output << ",claim_namespace=\""
           << escape_label(label_value(device.allocation.claim_namespace))
           << "\",claim_name=\""
           << escape_label(label_value(device.allocation.claim_name))
           << "\",claim_uid=\""
           << escape_label(label_value(device.allocation.claim_uid))
           << "\",pod_namespace=\""
           << escape_label(label_value(device.allocation.pod_namespace))
           << "\",pod_name=\""
           << escape_label(label_value(device.allocation.pod_name))
           << "\",container_name=\""
           << escape_label(label_value(device.allocation.container_name))
           << "\"} 1\n";
  }
  output << '\n';

  write_help_type(output, "tt_janitor_state_info",
                  "Hardware janitor state labels from a node-local state source.",
                  "gauge");
  for (const auto& device : devices) {
    if (!has_janitor(device.janitor)) {
      continue;
    }
    output << "tt_janitor_state_info{";
    write_device_labels(output, device);
    output << ",state=\"" << escape_label(label_value(device.janitor.state))
           << "\",quarantine_reason=\""
           << escape_label(label_value(device.janitor.quarantine_reason))
           << "\",last_scrub_status=\""
           << escape_label(label_value(device.janitor.last_scrub_status))
           << "\",last_reset_status=\""
           << escape_label(label_value(device.janitor.last_reset_status))
           << "\"} 1\n";
  }
  output << '\n';

  write_help_type(output, "tt_janitor_scrub_total",
                  "Total scrub attempts reported by the hardware janitor.",
                  "counter");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_janitor_scrub_total", device,
                              device.janitor.scrub_count);
  }
  output << '\n';

  write_help_type(output, "tt_janitor_reset_total",
                  "Total reset attempts reported by the hardware janitor.",
                  "counter");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_janitor_reset_total", device,
                              device.janitor.reset_count);
  }
  output << '\n';

  write_help_type(output, "tt_janitor_last_scrub_timestamp_seconds",
                  "Unix timestamp for the last scrub attempt.", "gauge");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_janitor_last_scrub_timestamp_seconds",
                              device, device.janitor.last_scrub_timestamp_seconds);
  }
  output << '\n';

  write_help_type(output, "tt_janitor_last_reset_timestamp_seconds",
                  "Unix timestamp for the last reset attempt.", "gauge");
  for (const auto& device : devices) {
    write_optional_int_metric(output, "tt_janitor_last_reset_timestamp_seconds",
                              device, device.janitor.last_reset_timestamp_seconds);
  }
  output << '\n';

  return output.str();
}

}  // namespace tt::metrics
