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
  write_optional_string_field(output, "vendorId", device.pci.vendor_id, first);
  write_optional_string_field(output, "deviceId", device.pci.device_id, first);
  write_optional_string_field(output, "classId", device.pci.class_id, first);
  write_optional_string_field(output, "revision", device.pci.revision, first);
  write_optional_string_field(output, "subsystemVendorId",
                              device.pci.subsystem_vendor_id, first);
  write_optional_string_field(output, "subsystemDeviceId",
                              device.pci.subsystem_device_id, first);
  write_optional_number_field(output, "numaNode", device.pci.numa_node, first);
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

void write_spec(std::ostream& output, const std::optional<DeviceSpec>& spec) {
  if (!spec.has_value()) {
    output << "null";
    return;
  }

  bool first = true;
  output << '{';
  write_string_field(output, "cardType", spec->card_type, first);
  write_number_field(output, "asicCount", spec->asic_count, first);
  write_number_field(output, "tensixCores", spec->tensix_cores, first);
  write_number_field(output, "sramBytes", spec->sram_bytes, first);
  write_number_field(output, "memoryBytes", spec->memory_bytes, first);
  write_number_field(output, "memoryBandwidthBytesPerSecond",
                     spec->memory_bandwidth_bytes_per_second, first);
  output << '}';
}

void write_memory(std::ostream& output, const DeviceTelemetry& device) {
  bool first = true;
  output << '{';
  write_optional_number_field(output, "usedBytes", device.memory_used_bytes,
                              first);
  write_optional_number_field(output, "totalBytes", device.memory_total_bytes,
                              first);
  output << '}';
}

void write_tensix(std::ostream& output, const DeviceTelemetry& device) {
  bool first = true;
  output << '{';
  write_optional_number_field(output, "used", device.tensix_cores_used, first);
  write_optional_number_field(output, "available",
                              device.tensix_cores_available, first);
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
  write_field(output, "spec", [&]() { write_spec(output, device.spec); },
              first);
  write_field(output, "memory", [&]() { write_memory(output, device); },
              first);
  write_field(output, "tensix", [&]() { write_tensix(output, device); },
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
