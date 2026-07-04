#include "tt_metrics_exporter/device.hpp"
#include "tt_metrics_exporter/json.hpp"
#include "tt_metrics_exporter/metrics.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

void write_file(const fs::path& path, const std::string& content) {
  std::ofstream file(path);
  assert(file);
  file << content;
}

fs::path make_temp_root() {
  fs::path root = fs::temp_directory_path() /
                  ("tt-metrics-exporter-test-" + std::to_string(::getpid()));
  fs::remove_all(root);
  fs::create_directories(root);
  return root;
}

void test_parse_memory_usage_keyed_values() {
  const auto memory =
      tt::metrics::parse_memory_usage("used_bytes: 1024\n"
                                      "total_bytes: 4096\n");
  assert(memory.used_bytes.has_value());
  assert(memory.total_bytes.has_value());
  assert(*memory.used_bytes == 1024);
  assert(*memory.total_bytes == 4096);
}

void test_parse_memory_usage_unkeyed_values() {
  const auto memory = tt::metrics::parse_memory_usage("2048 8192\n");
  assert(memory.used_bytes.has_value());
  assert(memory.total_bytes.has_value());
  assert(*memory.used_bytes == 2048);
  assert(*memory.total_bytes == 8192);
}

void test_parse_pci_resources() {
  const auto resources = tt::metrics::parse_pci_resources(
      "0x0000000800000000 0x000000081fffffff 0x0000000000140204\n"
      "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
      "0x0000000822000000 0x00000008220fffff 0x0000000000140204\n");

  assert(resources.size() == 2);
  assert(resources[0].index == 0);
  assert(resources[0].start == 0x0000000800000000ULL);
  assert(resources[0].end == 0x000000081fffffffULL);
  assert(resources[0].flags == 0x0000000000140204ULL);
  assert(resources[0].size_bytes == 0x20000000ULL);
  assert(resources[1].index == 2);
  assert(resources[1].size_bytes == 0x100000ULL);
}

void test_sysfs_collection_and_rendering() {
  const fs::path root = make_temp_root();
  const fs::path device = root / "0";
  fs::create_directories(device);
  fs::create_directories(device / "device");
  fs::create_directories(device / "device" / "hwmon" / "hwmon0");
  fs::create_directories(device / "power");
  write_file(device / "memory_usage", "used_bytes 1024\ntotal_bytes 4096\n");
  write_file(device / "architecture", "wormhole\n");
  write_file(device / "board_type", "n300\n");
  write_file(device / "health", "ok\n");
  write_file(device / "tt_aiclk", "1000\n");
  write_file(device / "tt_axiclk", "800\n");
  write_file(device / "tt_arcclk", "333\n");
  write_file(device / "tt_heartbeat", "42\n");
  write_file(device / "tt_therm_trip_count", "1\n");
  write_file(device / "tt_serial", "0xabc\n");
  write_file(device / "tt_card_type", "n300s\n");
  write_file(device / "tt_asic_id", "0x1234\n");
  write_file(device / "tt_fw_bundle_ver", "1.2.3\n");
  write_file(device / "tt_m3app_fw_ver", "2.3.4\n");
  write_file(device / "tt_m3bl_fw_ver", "3.4.5\n");
  write_file(device / "tt_arc_fw_ver", "4.5.6\n");
  write_file(device / "tt_eth_fw_ver", "5.6.7\n");
  write_file(device / "tt_ttflash_ver", "6.7.8\n");
  write_file(device / "dev", "241:0\n");
  write_file(device / "uevent", "MAJOR=241\nMINOR=0\nDEVNAME=tenstorrent/0\n");
  write_file(device / "device" / "vendor", "0x1e52\n");
  write_file(device / "device" / "device", "0x401e\n");
  write_file(device / "device" / "class", "0x120000\n");
  write_file(device / "device" / "revision", "0x01\n");
  write_file(device / "device" / "subsystem_vendor", "0x1af4\n");
  write_file(device / "device" / "subsystem_device", "0x1100\n");
  write_file(device / "device" / "numa_node", "-1\n");
  write_file(device / "device" / "resource",
             "0x0000000800000000 0x000000081fffffff 0x0000000000140204\n"
             "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
             "0x0000000822000000 0x00000008220fffff 0x0000000000140204\n");
  write_file(device / "power" / "runtime_status", "unsupported\n");
  write_file(device / "power" / "runtime_active_time", "7\n");
  write_file(device / "power" / "runtime_suspended_time", "11\n");
  write_file(device / "power" / "runtime_usage", "3\n");
  write_file(device / "power" / "runtime_active_kids", "2\n");
  write_file(device / "power" / "autosuspend_delay_ms", "250\n");
  write_file(device / "power" / "control", "auto\n");
  write_file(device / "power" / "runtime_enabled", "disabled\n");
  write_file(device / "device" / "hwmon" / "hwmon0" / "name", "wormhole\n");
  write_file(device / "device" / "hwmon" / "hwmon0" / "temp1_label", "asic\n");
  write_file(device / "device" / "hwmon" / "hwmon0" / "temp1_input", "55000\n");
  write_file(device / "device" / "hwmon" / "hwmon0" / "power1_label", "board\n");
  write_file(device / "device" / "hwmon" / "hwmon0" / "power1_input", "160000000\n");
  write_file(device / "tensix_cores_used", "8\n");
  write_file(device / "tensix_cores_available", "72\n");

  tt::metrics::SysfsCollector collector(tt::metrics::CollectorConfig{root});
  const auto devices = collector.collect();
  assert(devices.size() == 1);
  assert(devices[0].id == "0");
  assert(devices[0].architecture.has_value());
  assert(*devices[0].architecture == "wormhole");
  assert(devices[0].board_type.has_value());
  assert(*devices[0].board_type == "n300");
  assert(devices[0].health.has_value());
  assert(*devices[0].health == "ok");
  assert(devices[0].memory_used_bytes.has_value());
  assert(*devices[0].memory_used_bytes == 1024);
  assert(devices[0].memory_total_bytes.has_value());
  assert(*devices[0].memory_total_bytes == 4096);
  assert(devices[0].character_device.has_value());
  assert(devices[0].character_device->major == 241);
  assert(devices[0].character_device->minor == 0);
  assert(devices[0].character_device->dev_name.has_value());
  assert(*devices[0].character_device->dev_name == "tenstorrent/0");
  assert(devices[0].pci.vendor_id.has_value());
  assert(*devices[0].pci.vendor_id == "0x1e52");
  assert(devices[0].pci.device_id.has_value());
  assert(*devices[0].pci.device_id == "0x401e");
  assert(devices[0].pci.numa_node.has_value());
  assert(*devices[0].pci.numa_node == -1);
  assert(devices[0].pci_resources.size() == 2);
  assert(devices[0].power.runtime_status.has_value());
  assert(*devices[0].power.runtime_status == "unsupported");
  assert(devices[0].power.runtime_active_time_ms.has_value());
  assert(*devices[0].power.runtime_active_time_ms == 7);
  assert(devices[0].power.autosuspend_delay_ms.has_value());
  assert(*devices[0].power.autosuspend_delay_ms == 250);
  assert(devices[0].firmware.ai_clock_mhz.has_value());
  assert(*devices[0].firmware.ai_clock_mhz == 1000);
  assert(devices[0].firmware.card_type.has_value());
  assert(*devices[0].firmware.card_type == "n300s");
  assert(devices[0].firmware.heartbeat.has_value());
  assert(*devices[0].firmware.heartbeat == 42);
  assert(devices[0].hwmon_sensors.size() == 2);
  assert(devices[0].spec.has_value());
  assert(devices[0].spec->card_type == "n300");
  assert(devices[0].spec->tensix_cores == 128);
  assert(devices[0].spec->memory_bytes == 24000000000LL);
  assert(devices[0].tensix_cores_used.has_value());
  assert(*devices[0].tensix_cores_used == 8);
  assert(devices[0].tensix_cores_available.has_value());
  assert(*devices[0].tensix_cores_available == 72);

  const std::string metrics = tt::metrics::render_prometheus(devices);
  assert(metrics.find("tt_devices_discovered 1") != std::string::npos);
  assert(metrics.find("tt_device_info") != std::string::npos);
  assert(metrics.find("dev_major=\"241\"") != std::string::npos);
  assert(metrics.find("dev_minor=\"0\"") != std::string::npos);
  assert(metrics.find("dev_name=\"tenstorrent/0\"") != std::string::npos);
  assert(metrics.find("pci_vendor_id=\"0x1e52\"") != std::string::npos);
  assert(metrics.find("pci_device_id=\"0x401e\"") != std::string::npos);
  assert(metrics.find("numa_node=\"-1\"") != std::string::npos);
  assert(metrics.find("tt_pci_resource_size_bytes") != std::string::npos);
  assert(metrics.find("resource=\"0\"") != std::string::npos);
  assert(metrics.find("536870912") != std::string::npos);
  assert(metrics.find("tt_power_state_info") != std::string::npos);
  assert(metrics.find("runtime_status=\"unsupported\"") != std::string::npos);
  assert(metrics.find("runtime_enabled=\"disabled\"") != std::string::npos);
  assert(metrics.find("tt_power_runtime_active_time_ms") != std::string::npos);
  assert(metrics.find("tt_power_autosuspend_delay_ms") != std::string::npos);
  assert(metrics.find("tt_firmware_info") != std::string::npos);
  assert(metrics.find("card_type=\"n300s\"") != std::string::npos);
  assert(metrics.find("fw_bundle_version=\"1.2.3\"") != std::string::npos);
  assert(metrics.find("tt_firmware_clock_frequency_mhz") != std::string::npos);
  assert(metrics.find("clock=\"ai\"") != std::string::npos);
  assert(metrics.find("tt_firmware_heartbeat") != std::string::npos);
  assert(metrics.find("tt_thermal_trip_count") != std::string::npos);
  assert(metrics.find("tt_hwmon_sensor_value") != std::string::npos);
  assert(metrics.find("sensor=\"temp1_input\"") != std::string::npos);
  assert(metrics.find("unit=\"millidegrees_celsius\"") != std::string::npos);
  assert(metrics.find("tt_device_spec_tensix_cores") != std::string::npos);
  assert(metrics.find("tt_device_spec_memory_bytes") != std::string::npos);
  assert(metrics.find("24000000000") != std::string::npos);
  assert(metrics.find("tt_memory_used_bytes") != std::string::npos);
  assert(metrics.find("tt_memory_total_bytes") != std::string::npos);
  assert(metrics.find("tt_tensix_cores_used") != std::string::npos);
  assert(metrics.find("tt_tensix_cores_available") != std::string::npos);
  assert(metrics.find("architecture=\"wormhole\"") != std::string::npos);
  assert(metrics.find("board_type=\"n300\"") != std::string::npos);
  assert(metrics.find("health=\"ok\"") != std::string::npos);

  const std::string json = tt::metrics::render_devices_json(devices);
  assert(json.find("\"apiVersion\":\"telemetry.tenstorrent.com/v1\"") !=
         std::string::npos);
  assert(json.find("\"kind\":\"DeviceList\"") != std::string::npos);
  assert(json.find("\"devicesDiscovered\":1") != std::string::npos);
  assert(json.find("\"id\":\"0\"") != std::string::npos);
  assert(json.find("\"architecture\":\"wormhole\"") != std::string::npos);
  assert(json.find("\"path\":\"/dev/tenstorrent/0\"") != std::string::npos);
  assert(json.find("\"vendorId\":\"0x1e52\"") != std::string::npos);
  assert(json.find("\"deviceId\":\"0x401e\"") != std::string::npos);
  assert(json.find("\"cardType\":\"n300\"") != std::string::npos);
  assert(json.find("\"tensixCores\":128") != std::string::npos);
  assert(json.find("\"memoryBytes\":24000000000") != std::string::npos);
  assert(json.find("\"usedBytes\":1024") != std::string::npos);
  assert(json.find("\"available\":72") != std::string::npos);

  fs::remove_all(root);
}

void test_missing_root_is_empty() {
  tt::metrics::SysfsCollector collector(
      tt::metrics::CollectorConfig{"/definitely/missing/tenstorrent"});
  assert(collector.collect().empty());
}

}  // namespace

int main() {
  test_parse_memory_usage_keyed_values();
  test_parse_memory_usage_unkeyed_values();
  test_parse_pci_resources();
  test_sysfs_collection_and_rendering();
  test_missing_root_is_empty();
  std::cout << "tt_metrics_exporter_tests passed\n";
  return 0;
}
