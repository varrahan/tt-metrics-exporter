#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tt::metrics {

struct MemoryUsage {
  std::optional<std::uint64_t> used_bytes;
  std::optional<std::uint64_t> total_bytes;
};

struct CharacterDevice {
  std::uint64_t major = 0;
  std::uint64_t minor = 0;
  std::optional<std::string> dev_name;
};

struct PciInfo {
  std::optional<std::string> vendor_id;
  std::optional<std::string> device_id;
  std::optional<std::string> class_id;
  std::optional<std::string> revision;
  std::optional<std::string> subsystem_vendor_id;
  std::optional<std::string> subsystem_device_id;
  std::optional<std::int64_t> numa_node;
};

struct PciResource {
  int index = 0;
  std::uint64_t start = 0;
  std::uint64_t end = 0;
  std::uint64_t flags = 0;
  std::uint64_t size_bytes = 0;
};

struct PowerInfo {
  std::optional<std::string> runtime_status;
  std::optional<std::string> control;
  std::optional<std::string> runtime_enabled;
  std::optional<std::int64_t> runtime_active_time_ms;
  std::optional<std::int64_t> runtime_suspended_time_ms;
  std::optional<std::int64_t> runtime_usage;
  std::optional<std::int64_t> runtime_active_kids;
  std::optional<std::int64_t> autosuspend_delay_ms;
};

struct NamedCounter {
  std::string name;
  std::uint64_t value = 0;
};

struct FirmwareTelemetry {
  std::optional<std::int64_t> ai_clock_mhz;
  std::optional<std::int64_t> axi_clock_mhz;
  std::optional<std::int64_t> arc_clock_mhz;
  std::optional<std::int64_t> heartbeat;
  std::optional<std::int64_t> thermal_trip_count;
  std::optional<std::string> serial;
  std::optional<std::string> card_type;
  std::optional<std::string> asic_id;
  std::optional<std::string> fw_bundle_version;
  std::optional<std::string> m3_app_fw_version;
  std::optional<std::string> m3_bl_fw_version;
  std::optional<std::string> arc_fw_version;
  std::optional<std::string> eth_fw_version;
  std::optional<std::string> ttflash_version;
};

struct HwmonSensor {
  std::string name;
  std::optional<std::string> label;
  std::string unit;
  std::int64_t value = 0;
};

struct DeviceSpec {
  std::string card_type;
  std::int64_t asic_count = 0;
  std::int64_t tensix_cores = 0;
  std::int64_t sram_bytes = 0;
  std::int64_t memory_bytes = 0;
  std::int64_t memory_bandwidth_bytes_per_second = 0;
};

struct DeviceTelemetry {
  std::string id;
  std::filesystem::path sysfs_path;
  std::optional<std::string> architecture;
  std::optional<std::string> board_type;
  std::optional<std::string> health;
  std::optional<CharacterDevice> character_device;
  PciInfo pci;
  std::vector<PciResource> pci_resources;
  PowerInfo power;
  std::vector<NamedCounter> pcie_counters;
  FirmwareTelemetry firmware;
  std::vector<HwmonSensor> hwmon_sensors;
  std::optional<DeviceSpec> spec;
  std::optional<std::uint64_t> memory_used_bytes;
  std::optional<std::uint64_t> memory_total_bytes;
  std::optional<std::int64_t> tensix_cores_used;
  std::optional<std::int64_t> tensix_cores_available;
};

struct CollectorConfig {
  std::filesystem::path sysfs_root = "/sys/class/tenstorrent";
  bool collect_pcie_counters = false;
};

class SysfsCollector {
 public:
  explicit SysfsCollector(CollectorConfig config);

  std::vector<DeviceTelemetry> collect() const;

 private:
  CollectorConfig config_;
};

MemoryUsage parse_memory_usage(std::string_view content);
std::vector<PciResource> parse_pci_resources(std::string_view content);

}  // namespace tt::metrics
