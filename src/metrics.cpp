#include "tt_metrics_exporter/metrics.hpp"

#include <iomanip>
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
         << escape_label(label_value(device.board_type)) << "\"";
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
         << "\",pci_class_id=\"" << escape_label(label_value(device.pci.class_id))
         << "\",pci_revision=\""
         << escape_label(label_value(device.pci.revision))
         << "\",pci_subsystem_vendor_id=\""
         << escape_label(label_value(device.pci.subsystem_vendor_id))
         << "\",pci_subsystem_device_id=\""
         << escape_label(label_value(device.pci.subsystem_device_id))
         << "\",numa_node=\"" << escape_label(label_value(device.pci.numa_node))
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

  write_help_type(output, "tt_device_spec_asic_count",
                  "Static ASIC count inferred from firmware-reported card type.",
                  "gauge");
  for (const auto& device : devices) {
    if (!device.spec.has_value()) {
      continue;
    }
    output << "tt_device_spec_asic_count{";
    write_device_labels(output, device);
    output << ",card_type=\"" << escape_label(device.spec->card_type) << "\"} "
           << device.spec->asic_count << '\n';
  }
  output << '\n';

  write_help_type(output, "tt_device_spec_tensix_cores",
                  "Static Tensix core count inferred from firmware-reported card type.",
                  "gauge");
  for (const auto& device : devices) {
    if (!device.spec.has_value()) {
      continue;
    }
    output << "tt_device_spec_tensix_cores{";
    write_device_labels(output, device);
    output << ",card_type=\"" << escape_label(device.spec->card_type) << "\"} "
           << device.spec->tensix_cores << '\n';
  }
  output << '\n';

  write_help_type(output, "tt_device_spec_sram_bytes",
                  "Static SRAM capacity inferred from firmware-reported card type.",
                  "gauge");
  for (const auto& device : devices) {
    if (!device.spec.has_value()) {
      continue;
    }
    output << "tt_device_spec_sram_bytes{";
    write_device_labels(output, device);
    output << ",card_type=\"" << escape_label(device.spec->card_type) << "\"} "
           << device.spec->sram_bytes << '\n';
  }
  output << '\n';

  write_help_type(output, "tt_device_spec_memory_bytes",
                  "Static local memory capacity inferred from firmware-reported card type.",
                  "gauge");
  for (const auto& device : devices) {
    if (!device.spec.has_value()) {
      continue;
    }
    output << "tt_device_spec_memory_bytes{";
    write_device_labels(output, device);
    output << ",card_type=\"" << escape_label(device.spec->card_type) << "\"} "
           << device.spec->memory_bytes << '\n';
  }
  output << '\n';

  write_help_type(
      output, "tt_device_spec_memory_bandwidth_bytes_per_second",
      "Static memory bandwidth inferred from firmware-reported card type.",
      "gauge");
  for (const auto& device : devices) {
    if (!device.spec.has_value()) {
      continue;
    }
    output << "tt_device_spec_memory_bandwidth_bytes_per_second{";
    write_device_labels(output, device);
    output << ",card_type=\"" << escape_label(device.spec->card_type) << "\"} "
           << device.spec->memory_bandwidth_bytes_per_second << '\n';
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
    if (!device.memory_used_bytes.has_value()) {
      continue;
    }
    output << "tt_memory_used_bytes{";
    write_device_labels(output, device);
    output << "} " << *device.memory_used_bytes << '\n';
  }
  output << '\n';

  write_help_type(output, "tt_memory_total_bytes",
                  "Tenstorrent device total memory in bytes.", "gauge");
  for (const auto& device : devices) {
    if (!device.memory_total_bytes.has_value()) {
      continue;
    }
    output << "tt_memory_total_bytes{";
    write_device_labels(output, device);
    output << "} " << *device.memory_total_bytes << '\n';
  }
  output << '\n';

  write_help_type(output, "tt_tensix_cores_used",
                  "Tenstorrent Tensix cores currently active or reserved.",
                  "gauge");
  for (const auto& device : devices) {
    if (!device.tensix_cores_used.has_value()) {
      continue;
    }
    output << "tt_tensix_cores_used{";
    write_device_labels(output, device);
    output << "} " << *device.tensix_cores_used << '\n';
  }
  output << '\n';

  write_help_type(output, "tt_tensix_cores_available",
                  "Tenstorrent Tensix cores available on the device.", "gauge");
  for (const auto& device : devices) {
    if (!device.tensix_cores_available.has_value()) {
      continue;
    }
    output << "tt_tensix_cores_available{";
    write_device_labels(output, device);
    output << "} " << *device.tensix_cores_available << '\n';
  }
  output << '\n';

  return output.str();
}

}  // namespace tt::metrics
