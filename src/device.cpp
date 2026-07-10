#include "tt_metrics_exporter/device.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>
#include <utility>

namespace tt::metrics {
namespace {

namespace fs = std::filesystem;

constexpr std::uintmax_t kMaximumMetaliumStateFileBytes = 16 * 1024;
constexpr std::size_t kMaximumMetaliumWorkloadsPerDevice = 1024;

std::string trim(std::string_view input) {
  std::size_t begin = 0;
  while (begin < input.size() &&
         std::isspace(static_cast<unsigned char>(input[begin])) != 0) {
    ++begin;
  }

  std::size_t end = input.size();
  while (end > begin &&
         std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
    --end;
  }

  return std::string(input.substr(begin, end - begin));
}

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::vector<std::string> tokenize(std::string_view line) {
  std::string normalized;
  normalized.reserve(line.size());

  for (char c : line) {
    const auto uc = static_cast<unsigned char>(c);
    if (std::isalnum(uc) != 0 || c == '.' || c == '_' || c == '-') {
      normalized.push_back(c);
    } else {
      normalized.push_back(' ');
    }
  }

  std::istringstream stream(normalized);
  std::vector<std::string> tokens;
  for (std::string token; stream >> token;) {
    tokens.push_back(token);
  }
  return tokens;
}

std::optional<std::uint64_t> byte_multiplier(std::string unit) {
  unit = lower_ascii(std::move(unit));
  if (unit.empty() || unit == "b" || unit == "byte" || unit == "bytes") {
    return 1;
  }
  if (unit == "k" || unit == "kb" || unit == "kib") {
    return 1024ULL;
  }
  if (unit == "m" || unit == "mb" || unit == "mib") {
    return 1024ULL * 1024ULL;
  }
  if (unit == "g" || unit == "gb" || unit == "gib") {
    return 1024ULL * 1024ULL * 1024ULL;
  }
  if (unit == "t" || unit == "tb" || unit == "tib") {
    return 1024ULL * 1024ULL * 1024ULL * 1024ULL;
  }
  return std::nullopt;
}

std::optional<std::uint64_t> parse_byte_value(const std::string& token,
                                              const std::string& next_token) {
  if (token.empty()) {
    return std::nullopt;
  }

  char* end = nullptr;
  const long double value = std::strtold(token.c_str(), &end);
  if (end == token.c_str() || !std::isfinite(static_cast<double>(value)) ||
      value < 0) {
    return std::nullopt;
  }

  std::string suffix(end);
  if (suffix.empty()) {
    suffix = next_token;
  }

  const auto multiplier = byte_multiplier(suffix);
  if (!multiplier.has_value()) {
    if (end != token.c_str() + token.size()) {
      return std::nullopt;
    }
    return static_cast<std::uint64_t>(value);
  }

  return static_cast<std::uint64_t>(value * *multiplier);
}

std::optional<std::int64_t> parse_int64(std::string_view input) {
  const std::string value = trim(input);
  if (value.empty()) {
    return std::nullopt;
  }

  char* end = nullptr;
  const long long parsed = std::strtoll(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0') {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(parsed);
}

std::optional<std::int64_t> parse_boolish_int(std::string_view input) {
  const std::string value = lower_ascii(trim(input));
  if (value == "true" || value == "yes" || value == "required" ||
      value == "needed") {
    return 1;
  }
  if (value == "false" || value == "no" || value == "not_required" ||
      value == "none") {
    return 0;
  }
  return parse_int64(value);
}

std::optional<std::uint64_t> parse_uint64(std::string_view input) {
  const std::string value = trim(input);
  if (value.empty()) {
    return std::nullopt;
  }

  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(value.c_str(), &end, 0);
  if (end == value.c_str() || *end != '\0') {
    return std::nullopt;
  }
  return static_cast<std::uint64_t>(parsed);
}

std::optional<std::string> read_text_file(const fs::path& path) {
  std::ifstream file(path);
  if (!file) {
    return std::nullopt;
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  const std::string value = trim(buffer.str());
  if (value.empty()) {
    return std::nullopt;
  }
  return value;
}

std::optional<std::string> read_bounded_text_file(const fs::path& path,
                                                  std::uintmax_t maximum_size) {
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error || size > maximum_size) {
    return std::nullopt;
  }
  return read_text_file(path);
}

std::optional<std::string> read_first_text_file(
    const fs::path& root, const std::vector<std::string>& names) {
  for (const auto& name : names) {
    auto value = read_text_file(root / name);
    if (value.has_value()) {
      return value;
    }
  }
  return std::nullopt;
}

std::optional<std::uint64_t> read_uint_file(const fs::path& path) {
  auto value = read_text_file(path);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return parse_uint64(*value);
}

std::optional<std::int64_t> read_int_file(const fs::path& path) {
  auto value = read_text_file(path);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return parse_int64(*value);
}

std::optional<std::uint64_t> read_byte_file(const fs::path& path) {
  auto value = read_text_file(path);
  if (!value.has_value()) {
    return std::nullopt;
  }

  const auto tokens = tokenize(*value);
  if (tokens.empty()) {
    return std::nullopt;
  }
  const std::string unit = tokens.size() > 1 ? tokens[1] : std::string();
  return parse_byte_value(tokens[0], unit);
}

std::optional<std::int64_t> read_first_int_file(
    const fs::path& root, const std::vector<std::string>& names) {
  for (const auto& name : names) {
    auto value = read_text_file(root / name);
    if (!value.has_value()) {
      continue;
    }
    auto parsed = parse_int64(*value);
    if (parsed.has_value()) {
      return parsed;
    }
  }
  return std::nullopt;
}

std::optional<std::uint64_t> read_first_byte_file(
    const fs::path& root, const std::vector<std::string>& names) {
  for (const auto& name : names) {
    auto parsed = read_byte_file(root / name);
    if (parsed.has_value()) {
      return parsed;
    }
  }
  return std::nullopt;
}

std::optional<std::int64_t> read_first_boolish_file(
    const fs::path& root, const std::vector<std::string>& names) {
  for (const auto& name : names) {
    auto value = read_text_file(root / name);
    if (!value.has_value()) {
      continue;
    }
    auto parsed = parse_boolish_int(*value);
    if (parsed.has_value()) {
      return parsed;
    }
  }
  return std::nullopt;
}

std::optional<std::string> read_uevent_field(const fs::path& root,
                                             std::string_view field) {
  auto content = read_text_file(root / "uevent");
  if (!content.has_value()) {
    return std::nullopt;
  }

  std::istringstream lines(*content);
  const std::string prefix = std::string(field) + "=";
  for (std::string line; std::getline(lines, line);) {
    if (line.rfind(prefix, 0) == 0) {
      return trim(std::string_view(line).substr(prefix.size()));
    }
  }
  return std::nullopt;
}

std::optional<std::string> read_symlink_basename(const fs::path& path) {
  std::error_code error;
  const fs::path target = fs::read_symlink(path, error);
  if (error) {
    return std::nullopt;
  }

  const std::string name = target.filename().string();
  if (name.empty()) {
    return std::nullopt;
  }
  return name;
}

std::optional<std::string> read_pci_bdf(const fs::path& pci_root) {
  auto slot = read_uevent_field(pci_root, "PCI_SLOT_NAME");
  if (slot.has_value()) {
    return slot;
  }
  return read_symlink_basename(pci_root);
}

std::optional<std::string> read_pci_driver(const fs::path& pci_root) {
  auto driver = read_uevent_field(pci_root, "DRIVER");
  if (driver.has_value()) {
    return driver;
  }
  driver = read_symlink_basename(pci_root / "driver");
  if (driver.has_value()) {
    return driver;
  }
  return read_text_file(pci_root / "driver");
}

std::optional<std::int64_t> read_iommu_group(const fs::path& pci_root) {
  auto group = read_symlink_basename(pci_root / "iommu_group");
  if (group.has_value()) {
    auto parsed = parse_int64(*group);
    if (parsed.has_value()) {
      return parsed;
    }
  }

  auto group_text = read_text_file(pci_root / "iommu_group");
  if (!group_text.has_value()) {
    return std::nullopt;
  }
  return parse_int64(*group_text);
}

std::optional<CharacterDevice> read_character_device(const fs::path& root) {
  auto content = read_text_file(root / "dev");
  if (!content.has_value()) {
    return std::nullopt;
  }

  const std::size_t delimiter = content->find(':');
  if (delimiter == std::string::npos) {
    return std::nullopt;
  }

  auto major = parse_uint64(std::string_view(*content).substr(0, delimiter));
  auto minor = parse_uint64(std::string_view(*content).substr(delimiter + 1));
  if (!major.has_value() || !minor.has_value()) {
    return std::nullopt;
  }

  CharacterDevice character_device;
  character_device.major = *major;
  character_device.minor = *minor;
  character_device.dev_name = read_uevent_field(root, "DEVNAME");
  return character_device;
}

PciInfo read_pci_info(const fs::path& root) {
  const fs::path pci_root = root / "device";

  PciInfo info;
  info.bdf = read_pci_bdf(pci_root);
  info.driver = read_pci_driver(pci_root);
  info.vendor_id = read_text_file(pci_root / "vendor");
  info.device_id = read_text_file(pci_root / "device");
  info.class_id = read_text_file(pci_root / "class");
  info.revision = read_text_file(pci_root / "revision");
  info.subsystem_vendor_id = read_text_file(pci_root / "subsystem_vendor");
  info.subsystem_device_id = read_text_file(pci_root / "subsystem_device");
  info.iommu_group = read_iommu_group(pci_root);
  info.current_link_speed = read_text_file(pci_root / "current_link_speed");
  info.current_link_width = read_int_file(pci_root / "current_link_width");
  info.max_link_speed = read_text_file(pci_root / "max_link_speed");
  info.max_link_width = read_int_file(pci_root / "max_link_width");
  info.reset_method = read_text_file(pci_root / "reset_method");

  auto numa_node = read_text_file(pci_root / "numa_node");
  if (numa_node.has_value()) {
    info.numa_node = parse_int64(*numa_node);
  }

  return info;
}

std::string normalize_card_type(std::string value) {
  value = lower_ascii(trim(value));
  if (value == "n150d" || value == "n150s") {
    return "n150";
  }
  if (value == "n300d" || value == "n300s") {
    return "n300";
  }
  if (value == "p100a" || value == "p100b") {
    return "p100";
  }
  if (value == "p150a" || value == "p150b") {
    return "p150";
  }
  return value;
}

std::optional<std::string> infer_architecture_from_pci(
    const std::optional<std::string>& device_id) {
  if (!device_id.has_value()) {
    return std::nullopt;
  }

  const std::string id = lower_ascii(*device_id);
  if (id == "0x401e") {
    return "wormhole";
  }
  if (id == "0xb140") {
    return "blackhole";
  }
  return std::nullopt;
}

FirmwareTelemetry read_firmware_telemetry(const fs::path& root) {
  FirmwareTelemetry telemetry;
  telemetry.ai_clock_mhz = read_int_file(root / "tt_aiclk");
  telemetry.axi_clock_mhz = read_int_file(root / "tt_axiclk");
  telemetry.arc_clock_mhz = read_int_file(root / "tt_arcclk");
  telemetry.heartbeat = read_int_file(root / "tt_heartbeat");
  telemetry.thermal_trip_count = read_int_file(root / "tt_therm_trip_count");
  telemetry.serial = read_text_file(root / "tt_serial");
  telemetry.card_type = read_text_file(root / "tt_card_type");
  telemetry.asic_id = read_text_file(root / "tt_asic_id");
  telemetry.fw_bundle_version = read_text_file(root / "tt_fw_bundle_ver");
  telemetry.m3_app_fw_version = read_text_file(root / "tt_m3app_fw_ver");
  telemetry.m3_bl_fw_version = read_text_file(root / "tt_m3bl_fw_ver");
  telemetry.arc_fw_version = read_text_file(root / "tt_arc_fw_ver");
  telemetry.eth_fw_version = read_text_file(root / "tt_eth_fw_ver");
  telemetry.ttflash_version = read_text_file(root / "tt_ttflash_ver");
  return telemetry;
}

std::string hwmon_unit_for_sensor(std::string_view name) {
  if (name.rfind("temp", 0) == 0) {
    return "millidegrees_celsius";
  }
  if (name.rfind("in", 0) == 0) {
    return "millivolts";
  }
  if (name.rfind("curr", 0) == 0) {
    return "milliamps";
  }
  if (name.rfind("power", 0) == 0) {
    return "microwatts";
  }
  if (name.rfind("fan", 0) == 0) {
    return "rpm";
  }
  if (name.rfind("freq", 0) == 0) {
    return "hertz";
  }
  return "raw";
}

bool is_hwmon_input_file(const fs::path& path) {
  const std::string name = path.filename().string();
  return name.size() > 6 && name.rfind("_input") == name.size() - 6;
}

void collect_hwmon_dir(const fs::path& hwmon_dir,
                       std::vector<HwmonSensor>& sensors) {
  std::error_code error;
  if (!fs::is_directory(hwmon_dir, error)) {
    return;
  }

  fs::directory_iterator iterator(
      hwmon_dir, fs::directory_options::skip_permission_denied, error);
  if (error) {
    return;
  }

  for (const auto& entry : iterator) {
    std::error_code entry_error;
    if (!entry.is_regular_file(entry_error) ||
        !is_hwmon_input_file(entry.path())) {
      continue;
    }

    auto value = read_int_file(entry.path());
    if (!value.has_value()) {
      continue;
    }

    const std::string name = entry.path().filename().string();
    const std::string prefix = name.substr(0, name.size() - 6);
    sensors.push_back(HwmonSensor{
        name,
        read_text_file(hwmon_dir / (prefix + "_label")),
        hwmon_unit_for_sensor(prefix),
        *value,
    });
  }
}

std::vector<HwmonSensor> read_hwmon_sensors(const fs::path& root) {
  std::vector<HwmonSensor> sensors;
  for (const auto& base : {root / "hwmon", root / "device" / "hwmon"}) {
    std::error_code error;
    if (!fs::is_directory(base, error)) {
      continue;
    }

    fs::directory_iterator iterator(
        base, fs::directory_options::skip_permission_denied, error);
    if (error) {
      continue;
    }

    for (const auto& entry : iterator) {
      std::error_code entry_error;
      if (entry.is_directory(entry_error)) {
        collect_hwmon_dir(entry.path(), sensors);
      }
    }
  }

  std::sort(sensors.begin(), sensors.end(),
            [](const HwmonSensor& left, const HwmonSensor& right) {
              return left.name < right.name;
            });
  return sensors;
}

PowerInfo read_power_info(const fs::path& root) {
  const fs::path power_root = root / "power";

  PowerInfo info;
  info.runtime_status = read_text_file(power_root / "runtime_status");
  info.control = read_text_file(power_root / "control");
  info.runtime_enabled = read_text_file(power_root / "runtime_enabled");

  if (auto value = read_text_file(power_root / "runtime_active_time");
      value.has_value()) {
    info.runtime_active_time_ms = parse_int64(*value);
  }
  if (auto value = read_text_file(power_root / "runtime_suspended_time");
      value.has_value()) {
    info.runtime_suspended_time_ms = parse_int64(*value);
  }
  if (auto value = read_text_file(power_root / "runtime_usage");
      value.has_value()) {
    info.runtime_usage = parse_int64(*value);
  }
  if (auto value = read_text_file(power_root / "runtime_active_kids");
      value.has_value()) {
    info.runtime_active_kids = parse_int64(*value);
  }
  if (auto value = read_text_file(power_root / "autosuspend_delay_ms");
      value.has_value()) {
    info.autosuspend_delay_ms = parse_int64(*value);
  }

  return info;
}

std::vector<NamedCounter> read_pcie_counters(const fs::path& root) {
  std::vector<NamedCounter> counters;
  const fs::path counter_root = root / "pcie_perf_counters";

  std::error_code error;
  if (!fs::is_directory(counter_root, error)) {
    return counters;
  }

  fs::directory_iterator iterator(
      counter_root, fs::directory_options::skip_permission_denied, error);
  if (error) {
    return counters;
  }

  for (const auto& entry : iterator) {
    std::error_code entry_error;
    if (!entry.is_regular_file(entry_error)) {
      continue;
    }

    auto value = read_uint_file(entry.path());
    if (!value.has_value()) {
      continue;
    }
    counters.push_back(NamedCounter{entry.path().filename().string(), *value});
  }

  std::sort(counters.begin(), counters.end(),
            [](const NamedCounter& left, const NamedCounter& right) {
              return left.name < right.name;
            });
  return counters;
}

std::optional<std::pair<std::int64_t, std::int64_t>> parse_mesh_dimensions(
    std::string_view input) {
  const std::string value = lower_ascii(trim(input));
  const std::size_t x = value.find('x');
  if (x != std::string::npos) {
    auto rows = parse_int64(std::string_view(value).substr(0, x));
    auto cols = parse_int64(std::string_view(value).substr(x + 1));
    if (rows.has_value() && cols.has_value()) {
      return std::make_pair(*rows, *cols);
    }
  }

  const auto tokens = tokenize(input);
  if (tokens.size() < 2) {
    return std::nullopt;
  }

  auto rows = parse_int64(tokens[0]);
  auto cols = parse_int64(tokens[1]);
  if (!rows.has_value() || !cols.has_value()) {
    return std::nullopt;
  }
  return std::make_pair(*rows, *cols);
}

void populate_memory_telemetry(DeviceTelemetry& device) {
  auto content = read_first_text_file(
      device.sysfs_path, {"memory_usage", "dram_usage", "device_memory_usage",
                          "tt_memory_usage"});
  if (content.has_value()) {
    const MemoryUsage memory = parse_memory_usage(*content);
    device.memory.used_bytes = memory.used_bytes;
    device.memory.total_bytes = memory.total_bytes;
  }

  if (!device.memory.used_bytes.has_value()) {
    device.memory.used_bytes = read_first_byte_file(
        device.sysfs_path, {"memory_used_bytes", "dram_used_bytes",
                            "device_memory_used_bytes", "allocated_memory_bytes"});
  }
  if (!device.memory.total_bytes.has_value()) {
    device.memory.total_bytes = read_first_byte_file(
        device.sysfs_path,
        {"memory_total_bytes", "memory_capacity_bytes", "memory_size_bytes",
         "dram_total_bytes", "dram_capacity_bytes", "dram_size_bytes"});
  }
  device.memory.free_bytes = read_first_byte_file(
      device.sysfs_path,
      {"memory_free_bytes", "dram_free_bytes", "device_memory_free_bytes"});
  device.memory.available_bytes = read_first_byte_file(
      device.sysfs_path,
      {"memory_available_bytes", "dram_available_bytes",
       "device_memory_available_bytes"});
  device.memory.bandwidth_bytes_per_second = read_first_byte_file(
      device.sysfs_path,
      {"memory_bandwidth_bytes_per_second", "dram_bandwidth_bytes_per_second",
       "gddr_bandwidth_bytes_per_second"});
  device.memory.type = read_first_text_file(
      device.sysfs_path, {"memory_type", "dram_type", "gddr_type"});
  device.memory.controller_layout = read_first_text_file(
      device.sysfs_path, {"gddr_controller_layout", "dram_controller_layout",
                          "memory_controller_layout"});
  device.memory.controller_count = read_first_int_file(
      device.sysfs_path, {"gddr_controller_count", "dram_controller_count",
                          "memory_controller_count"});
  device.memory.controllers_per_asic = read_first_int_file(
      device.sysfs_path, {"gddr_controllers_per_asic",
                          "dram_controllers_per_asic",
                          "memory_controllers_per_asic"});
  device.memory.channel_count = read_first_int_file(
      device.sysfs_path,
      {"dram_channel_count", "gddr_channel_count", "memory_channel_count"});

  device.memory_used_bytes = device.memory.used_bytes;
  device.memory_total_bytes = device.memory.total_bytes;
}

void populate_tensix_telemetry(DeviceTelemetry& device) {
  device.tensix.used = read_first_int_file(
      device.sysfs_path,
      {"tensix_cores_used", "tensix_used", "active_tensix_cores",
       "tensix_active_cores"});
  device.tensix.available = read_first_int_file(
      device.sysfs_path, {"tensix_cores_available", "tensix_available",
                          "available_tensix_cores"});
  device.tensix.total = read_first_int_file(
      device.sysfs_path,
      {"tensix_cores_total", "total_tensix_cores", "tensix_total",
       "tensix_core_count"});
  device.tensix.mesh_rows = read_first_int_file(
      device.sysfs_path, {"tensix_mesh_rows", "tensix_grid_rows",
                          "tensix_rows", "core_grid_rows"});
  device.tensix.mesh_cols = read_first_int_file(
      device.sysfs_path, {"tensix_mesh_cols", "tensix_grid_cols",
                          "tensix_cols", "core_grid_cols"});
  if (!device.tensix.mesh_rows.has_value() ||
      !device.tensix.mesh_cols.has_value()) {
    auto dimensions = read_first_text_file(
        device.sysfs_path,
        {"tensix_mesh", "tensix_grid", "core_grid", "worker_grid"});
    if (dimensions.has_value()) {
      if (auto parsed = parse_mesh_dimensions(*dimensions); parsed.has_value()) {
        device.tensix.mesh_rows = parsed->first;
        device.tensix.mesh_cols = parsed->second;
      }
    }
  }
  device.tensix.topology = read_first_text_file(
      device.sysfs_path, {"tensix_topology", "tensix_layout"});
  device.tensix.active_regions = read_first_text_file(
      device.sysfs_path, {"tensix_active_regions", "active_core_ranges",
                          "active_core_grids"});

  if (device.tensix.used.has_value() || device.tensix.available.has_value() ||
      device.tensix.total.has_value() || device.tensix.mesh_rows.has_value() ||
      device.tensix.mesh_cols.has_value() ||
      device.tensix.topology.has_value() ||
      device.tensix.active_regions.has_value()) {
    device.tensix.source = "sysfs";
  }

  device.tensix_cores_used = device.tensix.used;
  device.tensix_cores_available = device.tensix.available;
}

void populate_health_detail(DeviceTelemetry& device) {
  device.health_detail.fault_code = read_first_text_file(
      device.sysfs_path, {"fault_code", "device_fault_code", "last_fault_code"});
  device.health_detail.fault_reason = read_first_text_file(
      device.sysfs_path,
      {"fault_reason", "device_fault_reason", "last_fault_reason",
       "reset_reason"});
  device.health_detail.reset_required = read_first_boolish_file(
      device.sysfs_path, {"reset_required", "needs_reset", "requires_reset"});
  device.health_detail.oom_fault_count = read_first_int_file(
      device.sysfs_path, {"oom_fault_count", "oom_count", "out_of_memory_count"});
  device.health_detail.hang_fault_count = read_first_int_file(
      device.sysfs_path, {"hang_fault_count", "hang_count", "device_hang_count"});
}

std::vector<InterconnectLink> read_interconnect_links(const fs::path& root) {
  std::vector<InterconnectLink> links;
  const std::vector<std::pair<fs::path, std::string>> bases = {
      {root / "scaleout_links", "scaleout"},
      {root / "ethernet_links", "ethernet"},
      {root / "fabric_links", "fabric"},
  };

  for (const auto& [base, default_type] : bases) {
    std::error_code error;
    if (!fs::is_directory(base, error)) {
      continue;
    }

    fs::directory_iterator iterator(
        base, fs::directory_options::skip_permission_denied, error);
    if (error) {
      continue;
    }

    for (const auto& entry : iterator) {
      std::error_code entry_error;
      if (!entry.is_directory(entry_error)) {
        continue;
      }

      InterconnectLink link;
      link.name = entry.path().filename().string();
      link.type = read_first_text_file(entry.path(), {"type", "link_type"});
      if (!link.type.has_value()) {
        link.type = default_type;
      }
      link.state = read_first_text_file(entry.path(), {"state", "status"});
      link.peer = read_first_text_file(
          entry.path(), {"peer", "remote", "remote_device", "remote_bdf"});
      link.speed_gbps = read_first_int_file(
          entry.path(), {"speed_gbps", "link_speed_gbps", "rate_gbps"});
      link.ring_id = read_first_text_file(entry.path(), {"ring_id", "ring"});
      links.push_back(std::move(link));
    }
  }

  std::sort(links.begin(), links.end(),
            [](const InterconnectLink& left, const InterconnectLink& right) {
              return left.name < right.name;
            });
  return links;
}

std::vector<fs::path> state_path_candidates(const fs::path& root,
                                            const DeviceTelemetry& device) {
  std::vector<fs::path> candidates;
  if (root.empty()) {
    return candidates;
  }
  candidates.push_back(root / device.id);
  if (device.pci.bdf.has_value()) {
    candidates.push_back(root / *device.pci.bdf);
  }
  if (device.character_device.has_value() &&
      device.character_device->dev_name.has_value()) {
    candidates.push_back(root / fs::path(*device.character_device->dev_name).filename());
  }
  return candidates;
}

std::optional<fs::path> first_existing_directory(
    const std::vector<fs::path>& candidates) {
  for (const auto& candidate : candidates) {
    std::error_code error;
    if (fs::is_directory(candidate, error)) {
      return candidate;
    }
  }
  return std::nullopt;
}

std::map<std::string, std::string> parse_key_value_state(
    std::string_view content) {
  std::map<std::string, std::string> values;
  std::istringstream lines{std::string(content)};
  for (std::string line; std::getline(lines, line);) {
    const std::size_t delimiter = line.find('=');
    if (delimiter == std::string::npos) {
      continue;
    }
    const std::string key = trim(std::string_view(line).substr(0, delimiter));
    const std::string value =
        trim(std::string_view(line).substr(delimiter + 1));
    if (!key.empty()) {
      values[key] = value;
    }
  }
  return values;
}

std::optional<std::string> state_string(
    const std::map<std::string, std::string>& values, std::string_view key) {
  auto value = values.find(std::string(key));
  if (value == values.end() || value->second.empty()) {
    return std::nullopt;
  }
  return value->second;
}

std::optional<std::int64_t> state_non_negative_int(
    const std::map<std::string, std::string>& values, std::string_view key) {
  auto value = state_string(values, key);
  if (!value.has_value()) {
    return std::nullopt;
  }
  auto parsed = parse_int64(*value);
  if (!parsed.has_value() || *parsed < 0) {
    return std::nullopt;
  }
  return parsed;
}

std::vector<MetaliumWorkloadTelemetry> read_metalium_workloads(
    const fs::path& root, const DeviceTelemetry& device,
    std::int64_t stale_after_seconds) {
  std::vector<MetaliumWorkloadTelemetry> workloads;
  auto state_dir = first_existing_directory(state_path_candidates(root, device));
  if (!state_dir.has_value()) {
    return workloads;
  }

  std::error_code error;
  fs::directory_iterator iterator(
      *state_dir, fs::directory_options::skip_permission_denied, error);
  if (error) {
    return workloads;
  }

  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  for (const auto& entry : iterator) {
    if (workloads.size() >= kMaximumMetaliumWorkloadsPerDevice) {
      break;
    }
    std::error_code entry_error;
    if (!entry.is_regular_file(entry_error) ||
        entry.path().extension() != ".state") {
      continue;
    }

    auto content = read_bounded_text_file(entry.path(),
                                          kMaximumMetaliumStateFileBytes);
    if (!content.has_value()) {
      continue;
    }
    const auto values = parse_key_value_state(*content);
    auto schema_version = state_non_negative_int(values, "schema_version");
    if (!schema_version.has_value() || *schema_version != 1) {
      continue;
    }

    MetaliumWorkloadTelemetry workload;
    workload.workload_id =
        state_string(values, "workload_id")
            .value_or(entry.path().stem().string());
    workload.pod_namespace = state_string(values, "pod_namespace");
    workload.pod_name = state_string(values, "pod_name");
    workload.container_name = state_string(values, "container_name");
    workload.active = state_non_negative_int(values, "active");
    workload.programs_observed =
        state_non_negative_int(values, "programs_observed");
    workload.tensix_cores_used =
        state_non_negative_int(values, "tensix_cores_used");
    workload.tensix_cores_total =
        state_non_negative_int(values, "tensix_cores_total");
    workload.sample_timestamp_seconds =
        state_non_negative_int(values, "sample_timestamp_seconds");
    if (!workload.active.has_value() || *workload.active > 1 ||
        !workload.programs_observed.has_value() ||
        !workload.tensix_cores_used.has_value() ||
        !workload.sample_timestamp_seconds.has_value() ||
        (workload.tensix_cores_total.has_value() &&
         (*workload.tensix_cores_total == 0 ||
          *workload.tensix_cores_used > *workload.tensix_cores_total))) {
      continue;
    }
    workload.stale =
        stale_after_seconds > 0 &&
        (*workload.sample_timestamp_seconds < now - stale_after_seconds ||
         *workload.sample_timestamp_seconds > now + stale_after_seconds);
    workloads.push_back(std::move(workload));
  }

  std::sort(workloads.begin(), workloads.end(),
            [](const MetaliumWorkloadTelemetry& left,
               const MetaliumWorkloadTelemetry& right) {
              return left.workload_id < right.workload_id;
            });
  return workloads;
}

void apply_metalium_workload_telemetry(DeviceTelemetry& device) {
  std::int64_t cores_used = 0;
  std::optional<std::int64_t> cores_total;
  bool has_fresh_workload = false;

  for (const auto& workload : device.metalium_workloads) {
    if (workload.stale) {
      continue;
    }
    has_fresh_workload = true;
    if (workload.active.value_or(0) != 0 &&
        workload.tensix_cores_used.has_value()) {
      cores_used += *workload.tensix_cores_used;
    }
    if (workload.tensix_cores_total.has_value() &&
        (!cores_total.has_value() ||
         *workload.tensix_cores_total > *cores_total)) {
      cores_total = workload.tensix_cores_total;
    }
  }

  if (!has_fresh_workload) {
    return;
  }
  if (cores_total.has_value() && cores_used > *cores_total) {
    cores_used = *cores_total;
  }
  if (!device.tensix.used.has_value()) {
    device.tensix.used = cores_used;
    device.tensix.source = "metalium_profiler";
  }
  if (!device.tensix.total.has_value() && cores_total.has_value()) {
    device.tensix.total = cores_total;
    device.tensix.source = "metalium_profiler";
  }
  if (!device.tensix.available.has_value() && cores_total.has_value()) {
    device.tensix.available = std::max<std::int64_t>(0, *cores_total - cores_used);
    device.tensix.source = "metalium_profiler";
  }
  device.tensix_cores_used = device.tensix.used;
  device.tensix_cores_available = device.tensix.available;
}

AllocationTelemetry read_allocation_telemetry(const fs::path& root,
                                              const DeviceTelemetry& device) {
  AllocationTelemetry allocation;
  auto state_dir = first_existing_directory(state_path_candidates(root, device));
  if (!state_dir.has_value()) {
    return allocation;
  }

  allocation.claim_namespace =
      read_text_file(*state_dir / "claim_namespace");
  allocation.claim_name = read_text_file(*state_dir / "claim_name");
  allocation.claim_uid = read_text_file(*state_dir / "claim_uid");
  allocation.pod_namespace = read_text_file(*state_dir / "pod_namespace");
  allocation.pod_name = read_text_file(*state_dir / "pod_name");
  allocation.container_name = read_text_file(*state_dir / "container_name");
  return allocation;
}

JanitorTelemetry read_janitor_telemetry(const fs::path& root,
                                        const DeviceTelemetry& device) {
  JanitorTelemetry janitor;
  auto state_dir = first_existing_directory(state_path_candidates(root, device));
  if (!state_dir.has_value()) {
    return janitor;
  }

  janitor.state = read_text_file(*state_dir / "state");
  janitor.quarantine_reason = read_text_file(*state_dir / "quarantine_reason");
  janitor.last_scrub_status = read_text_file(*state_dir / "last_scrub_status");
  janitor.last_reset_status = read_text_file(*state_dir / "last_reset_status");
  janitor.scrub_count = read_int_file(*state_dir / "scrub_count");
  janitor.reset_count = read_int_file(*state_dir / "reset_count");
  janitor.last_scrub_timestamp_seconds =
      read_int_file(*state_dir / "last_scrub_timestamp_seconds");
  janitor.last_reset_timestamp_seconds =
      read_int_file(*state_dir / "last_reset_timestamp_seconds");
  return janitor;
}

}  // namespace

SysfsCollector::SysfsCollector(CollectorConfig config)
    : config_(std::move(config)) {}

std::vector<DeviceTelemetry> SysfsCollector::collect() const {
  std::vector<DeviceTelemetry> devices;

  std::error_code error;
  if (!fs::exists(config_.sysfs_root, error) ||
      !fs::is_directory(config_.sysfs_root, error)) {
    return devices;
  }

  fs::directory_iterator iterator(
      config_.sysfs_root, fs::directory_options::skip_permission_denied, error);
  if (error) {
    return devices;
  }

  for (const auto& entry : iterator) {
    std::error_code entry_error;
    if (!entry.is_directory(entry_error)) {
      continue;
    }

    DeviceTelemetry device;
    device.id = entry.path().filename().string();
    device.sysfs_path = entry.path();
    device.architecture = read_first_text_file(
        entry.path(), {"architecture", "arch", "chip_arch",
                       "chip_architecture", "device_arch", "chip"});
    device.board_type = read_first_text_file(
        entry.path(),
        {"board_type", "board", "card_type", "card_series", "product_name"});
    device.health = read_first_text_file(
        entry.path(), {"health", "status", "device_status"});
    device.character_device = read_character_device(entry.path());
    device.pci = read_pci_info(entry.path());
    device.firmware = read_firmware_telemetry(entry.path());
    if (device.firmware.card_type.has_value()) {
      device.board_type = normalize_card_type(*device.firmware.card_type);
    }
    if (!device.architecture.has_value()) {
      device.architecture = infer_architecture_from_pci(device.pci.device_id);
    }
    device.hwmon_sensors = read_hwmon_sensors(entry.path());
    device.power = read_power_info(entry.path());
    if (auto content = read_text_file(entry.path() / "device" / "resource");
        content.has_value()) {
      device.pci_resources = parse_pci_resources(*content);
    }
    if (config_.collect_pcie_counters) {
      device.pcie_counters = read_pcie_counters(entry.path());
    }
    populate_memory_telemetry(device);
    populate_tensix_telemetry(device);
    device.metalium_workloads = read_metalium_workloads(
        config_.metalium_profiler_state_root, device,
        config_.metalium_profiler_stale_after_seconds);
    apply_metalium_workload_telemetry(device);
    populate_health_detail(device);
    device.interconnect_links = read_interconnect_links(entry.path());
    device.allocation =
        read_allocation_telemetry(config_.allocation_state_root, device);
    device.janitor = read_janitor_telemetry(config_.janitor_state_root, device);
    devices.push_back(std::move(device));
  }

  std::sort(devices.begin(), devices.end(),
            [](const DeviceTelemetry& left, const DeviceTelemetry& right) {
              return left.id < right.id;
            });
  return devices;
}

MemoryUsage parse_memory_usage(std::string_view content) {
  MemoryUsage usage;
  std::vector<std::uint64_t> unkeyed_values;

  std::istringstream lines{std::string(content)};
  for (std::string line; std::getline(lines, line);) {
    const auto tokens = tokenize(line);
    if (tokens.empty()) {
      continue;
    }

    for (std::size_t index = 0; index < tokens.size(); ++index) {
      const std::string key = lower_ascii(tokens[index]);
      const std::string next =
          index + 1 < tokens.size() ? tokens[index + 1] : std::string();
      const std::string unit =
          index + 2 < tokens.size() ? tokens[index + 2] : std::string();

      if (key.find("used") != std::string::npos ||
          key.find("allocated") != std::string::npos) {
        if (auto value = parse_byte_value(next, unit); value.has_value()) {
          usage.used_bytes = value;
        }
        continue;
      }

      if (key.find("total") != std::string::npos ||
          key.find("capacity") != std::string::npos ||
          key == "size") {
        if (auto value = parse_byte_value(next, unit); value.has_value()) {
          usage.total_bytes = value;
        }
        continue;
      }

      const std::string maybe_unit =
          index + 1 < tokens.size() ? tokens[index + 1] : std::string();
      if (auto value = parse_byte_value(tokens[index], maybe_unit);
          value.has_value()) {
        unkeyed_values.push_back(*value);
      }
    }
  }

  if (!usage.used_bytes.has_value() && !unkeyed_values.empty()) {
    usage.used_bytes = unkeyed_values[0];
  }
  if (!usage.total_bytes.has_value() && unkeyed_values.size() >= 2) {
    usage.total_bytes = unkeyed_values[1];
  }

  return usage;
}

std::vector<PciResource> parse_pci_resources(std::string_view content) {
  std::vector<PciResource> resources;
  std::istringstream lines{std::string(content)};

  int index = 0;
  for (std::string line; std::getline(lines, line); ++index) {
    std::istringstream fields(line);
    std::string start_text;
    std::string end_text;
    std::string flags_text;
    if (!(fields >> start_text >> end_text >> flags_text)) {
      continue;
    }

    auto start = parse_uint64(start_text);
    auto end = parse_uint64(end_text);
    auto flags = parse_uint64(flags_text);
    if (!start.has_value() || !end.has_value() || !flags.has_value()) {
      continue;
    }
    if (*start == 0 && *end == 0 && *flags == 0) {
      continue;
    }
    if (*end < *start) {
      continue;
    }

    PciResource resource;
    resource.index = index;
    resource.start = *start;
    resource.end = *end;
    resource.flags = *flags;
    resource.size_bytes = *end - *start + 1;
    resources.push_back(resource);
  }

  return resources;
}

}  // namespace tt::metrics
