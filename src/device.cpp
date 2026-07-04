#include "tt_metrics_exporter/device.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace tt::metrics {
namespace {

namespace fs = std::filesystem;

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
  if (end == value.c_str()) {
    return std::nullopt;
  }
  return static_cast<std::int64_t>(parsed);
}

std::optional<std::uint64_t> parse_uint64(std::string_view input) {
  const std::string value = trim(input);
  if (value.empty()) {
    return std::nullopt;
  }

  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(value.c_str(), &end, 0);
  if (end == value.c_str()) {
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
  info.vendor_id = read_text_file(pci_root / "vendor");
  info.device_id = read_text_file(pci_root / "device");
  info.class_id = read_text_file(pci_root / "class");
  info.revision = read_text_file(pci_root / "revision");
  info.subsystem_vendor_id = read_text_file(pci_root / "subsystem_vendor");
  info.subsystem_device_id = read_text_file(pci_root / "subsystem_device");

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

std::optional<DeviceSpec> spec_for_card_type(
    const std::optional<std::string>& card_type) {
  if (!card_type.has_value()) {
    return std::nullopt;
  }

  const std::string normalized = normalize_card_type(*card_type);
  if (normalized == "n150") {
    return DeviceSpec{normalized, 1, 72, 108LL * 1000 * 1000,
                      12LL * 1000 * 1000 * 1000,
                      288LL * 1000 * 1000 * 1000};
  }
  if (normalized == "n300") {
    return DeviceSpec{normalized, 2, 128, 192LL * 1000 * 1000,
                      24LL * 1000 * 1000 * 1000,
                      576LL * 1000 * 1000 * 1000};
  }
  if (normalized == "p100") {
    return DeviceSpec{normalized, 1, 120, 180LL * 1000 * 1000,
                      28LL * 1000 * 1000 * 1000,
                      448LL * 1000 * 1000 * 1000};
  }
  if (normalized == "p150") {
    return DeviceSpec{normalized, 1, 120, 180LL * 1000 * 1000,
                      32LL * 1000 * 1000 * 1000,
                      512LL * 1000 * 1000 * 1000};
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

void populate_memory_usage(DeviceTelemetry& device) {
  auto content = read_text_file(device.sysfs_path / "memory_usage");
  if (!content.has_value()) {
    return;
  }

  const MemoryUsage memory = parse_memory_usage(*content);
  device.memory_used_bytes = memory.used_bytes;
  device.memory_total_bytes = memory.total_bytes;
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
    device.spec = spec_for_card_type(device.firmware.card_type);
    device.power = read_power_info(entry.path());
    if (auto content = read_text_file(entry.path() / "device" / "resource");
        content.has_value()) {
      device.pci_resources = parse_pci_resources(*content);
    }
    if (config_.collect_pcie_counters) {
      device.pcie_counters = read_pcie_counters(entry.path());
    }
    device.tensix_cores_used = read_first_int_file(
        entry.path(),
        {"tensix_cores_used", "tensix_used", "active_tensix_cores"});
    device.tensix_cores_available = read_first_int_file(
        entry.path(),
        {"tensix_cores_available", "tensix_available",
         "total_tensix_cores"});

    populate_memory_usage(device);
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
