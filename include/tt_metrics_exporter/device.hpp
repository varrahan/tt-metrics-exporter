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
  std::optional<std::string> bdf;
  std::optional<std::string> driver;
  std::optional<std::string> vendor_id;
  std::optional<std::string> device_id;
  std::optional<std::string> class_id;
  std::optional<std::string> revision;
  std::optional<std::string> subsystem_vendor_id;
  std::optional<std::string> subsystem_device_id;
  std::optional<std::int64_t> numa_node;
  std::optional<std::int64_t> iommu_group;
  std::optional<std::string> current_link_speed;
  std::optional<std::int64_t> current_link_width;
  std::optional<std::string> max_link_speed;
  std::optional<std::int64_t> max_link_width;
  std::optional<std::string> reset_method;
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

struct MemoryTelemetry {
  std::optional<std::uint64_t> used_bytes;
  std::optional<std::uint64_t> total_bytes;
  std::optional<std::uint64_t> free_bytes;
  std::optional<std::uint64_t> available_bytes;
  std::optional<std::uint64_t> bandwidth_bytes_per_second;
  std::optional<std::string> type;
  std::optional<std::string> controller_layout;
  std::optional<std::int64_t> controller_count;
  std::optional<std::int64_t> controllers_per_asic;
  std::optional<std::int64_t> channel_count;
};

struct TensixTelemetry {
  std::optional<std::int64_t> used;
  std::optional<std::int64_t> available;
  std::optional<std::int64_t> total;
  std::optional<std::int64_t> mesh_rows;
  std::optional<std::int64_t> mesh_cols;
  std::optional<std::string> topology;
  std::optional<std::string> active_regions;
  std::optional<std::string> source;
};

struct MetaliumWorkloadTelemetry {
  std::string workload_id;
  std::optional<std::string> pod_namespace;
  std::optional<std::string> pod_name;
  std::optional<std::string> container_name;
  std::optional<std::int64_t> active;
  std::optional<std::int64_t> programs_observed;
  std::optional<std::int64_t> tensix_cores_used;
  std::optional<std::int64_t> tensix_cores_total;
  std::optional<std::int64_t> sample_timestamp_seconds;
  bool stale = false;
};

struct HealthTelemetry {
  std::optional<std::string> fault_code;
  std::optional<std::string> fault_reason;
  std::optional<std::int64_t> reset_required;
  std::optional<std::int64_t> oom_fault_count;
  std::optional<std::int64_t> hang_fault_count;
};

struct InterconnectLink {
  std::string name;
  std::optional<std::string> type;
  std::optional<std::string> state;
  std::optional<std::string> peer;
  std::optional<std::int64_t> speed_gbps;
  std::optional<std::string> ring_id;
};

struct AllocationTelemetry {
  std::optional<std::string> claim_namespace;
  std::optional<std::string> claim_name;
  std::optional<std::string> claim_uid;
  std::optional<std::string> pod_namespace;
  std::optional<std::string> pod_name;
  std::optional<std::string> container_name;
};

struct JanitorTelemetry {
  std::optional<std::string> state;
  std::optional<std::string> quarantine_reason;
  std::optional<std::string> last_scrub_status;
  std::optional<std::string> last_reset_status;
  std::optional<std::int64_t> scrub_count;
  std::optional<std::int64_t> reset_count;
  std::optional<std::int64_t> last_scrub_timestamp_seconds;
  std::optional<std::int64_t> last_reset_timestamp_seconds;
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
  MemoryTelemetry memory;
  TensixTelemetry tensix;
  std::vector<MetaliumWorkloadTelemetry> metalium_workloads;
  HealthTelemetry health_detail;
  std::vector<InterconnectLink> interconnect_links;
  AllocationTelemetry allocation;
  JanitorTelemetry janitor;
  std::optional<std::uint64_t> memory_used_bytes;
  std::optional<std::uint64_t> memory_total_bytes;
  std::optional<std::int64_t> tensix_cores_used;
  std::optional<std::int64_t> tensix_cores_available;
};

struct CollectorConfig {
  std::filesystem::path sysfs_root = "/sys/class/tenstorrent";
  std::filesystem::path allocation_state_root;
  std::filesystem::path janitor_state_root;
  std::filesystem::path metalium_profiler_state_root;
  std::int64_t metalium_profiler_stale_after_seconds = 15;
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
