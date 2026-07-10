#include "tt_metrics_exporter/device.hpp"
#include "tt_metrics_exporter/json.hpp"
#include "tt_metrics_exporter/metrics.hpp"

#include <cassert>
#include <chrono>
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
  const fs::path base = make_temp_root();
  const fs::path root = base / "sysfs";
  const fs::path allocation_root = base / "allocations";
  const fs::path janitor_root = base / "janitor";
  const fs::path device = root / "0";
  fs::create_directories(device);
  fs::create_directories(device / "device");
  fs::create_directories(device / "device" / "hwmon" / "hwmon0");
  fs::create_directories(device / "power");
  fs::create_directories(device / "scaleout_links" / "eth0");
  fs::create_directories(allocation_root / "0");
  fs::create_directories(janitor_root / "0");
  write_file(device / "memory_usage", "used_bytes 1024\ntotal_bytes 4096\n");
  write_file(device / "memory_free_bytes", "2048\n");
  write_file(device / "memory_available_bytes", "3072\n");
  write_file(device / "memory_bandwidth_bytes_per_second", "123456789\n");
  write_file(device / "memory_type", "GDDR6\n");
  write_file(device / "gddr_controller_layout", "localizedControllers\n");
  write_file(device / "gddr_controller_count", "12\n");
  write_file(device / "gddr_controllers_per_asic", "6\n");
  write_file(device / "dram_channel_count", "6\n");
  write_file(device / "architecture", "wormhole\n");
  write_file(device / "board_type", "n300\n");
  write_file(device / "health", "ok\n");
  write_file(device / "fault_code", "none\n");
  write_file(device / "fault_reason", "clear\n");
  write_file(device / "reset_required", "false\n");
  write_file(device / "oom_fault_count", "0\n");
  write_file(device / "hang_fault_count", "1\n");
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
  write_file(device / "device" / "uevent",
             "DRIVER=tenstorrent\nPCI_SLOT_NAME=0000:01:00.0\n");
  write_file(device / "device" / "vendor", "0x1e52\n");
  write_file(device / "device" / "device", "0x401e\n");
  write_file(device / "device" / "class", "0x120000\n");
  write_file(device / "device" / "revision", "0x01\n");
  write_file(device / "device" / "subsystem_vendor", "0x1af4\n");
  write_file(device / "device" / "subsystem_device", "0x1100\n");
  write_file(device / "device" / "numa_node", "-1\n");
  write_file(device / "device" / "iommu_group", "17\n");
  write_file(device / "device" / "current_link_speed", "16.0 GT/s PCIe\n");
  write_file(device / "device" / "current_link_width", "8\n");
  write_file(device / "device" / "max_link_speed", "16.0 GT/s PCIe\n");
  write_file(device / "device" / "max_link_width", "16\n");
  write_file(device / "device" / "reset_method", "bus\n");
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
  write_file(device / "tensix_cores_total", "80\n");
  write_file(device / "tensix_mesh", "8x10\n");
  write_file(device / "tensix_topology", "2dMesh\n");
  write_file(device / "tensix_active_regions", "0,0-1,3\n");
  write_file(device / "scaleout_links" / "eth0" / "state", "up\n");
  write_file(device / "scaleout_links" / "eth0" / "peer", "0000:02:00.0\n");
  write_file(device / "scaleout_links" / "eth0" / "speed_gbps", "200\n");
  write_file(device / "scaleout_links" / "eth0" / "ring_id", "ring-a\n");
  write_file(allocation_root / "0" / "claim_namespace", "default\n");
  write_file(allocation_root / "0" / "claim_name", "tt-claim\n");
  write_file(allocation_root / "0" / "claim_uid", "claim-uid\n");
  write_file(allocation_root / "0" / "pod_namespace", "default\n");
  write_file(allocation_root / "0" / "pod_name", "tt-pod\n");
  write_file(allocation_root / "0" / "container_name", "worker\n");
  write_file(janitor_root / "0" / "state", "healthy\n");
  write_file(janitor_root / "0" / "quarantine_reason", "none\n");
  write_file(janitor_root / "0" / "last_scrub_status", "ok\n");
  write_file(janitor_root / "0" / "last_reset_status", "ok\n");
  write_file(janitor_root / "0" / "scrub_count", "3\n");
  write_file(janitor_root / "0" / "reset_count", "2\n");
  write_file(janitor_root / "0" / "last_scrub_timestamp_seconds", "123\n");
  write_file(janitor_root / "0" / "last_reset_timestamp_seconds", "456\n");

  tt::metrics::CollectorConfig config;
  config.sysfs_root = root;
  config.allocation_state_root = allocation_root;
  config.janitor_state_root = janitor_root;
  tt::metrics::SysfsCollector collector(config);
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
  assert(devices[0].memory.free_bytes.has_value());
  assert(*devices[0].memory.free_bytes == 2048);
  assert(devices[0].memory.available_bytes.has_value());
  assert(*devices[0].memory.available_bytes == 3072);
  assert(devices[0].memory.bandwidth_bytes_per_second.has_value());
  assert(*devices[0].memory.bandwidth_bytes_per_second == 123456789ULL);
  assert(devices[0].memory.type.has_value());
  assert(*devices[0].memory.type == "GDDR6");
  assert(devices[0].memory.controller_count.has_value());
  assert(*devices[0].memory.controller_count == 12);
  assert(devices[0].character_device.has_value());
  assert(devices[0].character_device->major == 241);
  assert(devices[0].character_device->minor == 0);
  assert(devices[0].character_device->dev_name.has_value());
  assert(*devices[0].character_device->dev_name == "tenstorrent/0");
  assert(devices[0].pci.bdf.has_value());
  assert(*devices[0].pci.bdf == "0000:01:00.0");
  assert(devices[0].pci.driver.has_value());
  assert(*devices[0].pci.driver == "tenstorrent");
  assert(devices[0].pci.vendor_id.has_value());
  assert(*devices[0].pci.vendor_id == "0x1e52");
  assert(devices[0].pci.device_id.has_value());
  assert(*devices[0].pci.device_id == "0x401e");
  assert(devices[0].pci.numa_node.has_value());
  assert(*devices[0].pci.numa_node == -1);
  assert(devices[0].pci.iommu_group.has_value());
  assert(*devices[0].pci.iommu_group == 17);
  assert(devices[0].pci.current_link_speed.has_value());
  assert(*devices[0].pci.current_link_speed == "16.0 GT/s PCIe");
  assert(devices[0].pci.current_link_width.has_value());
  assert(*devices[0].pci.current_link_width == 8);
  assert(devices[0].pci.max_link_width.has_value());
  assert(*devices[0].pci.max_link_width == 16);
  assert(devices[0].pci.reset_method.has_value());
  assert(*devices[0].pci.reset_method == "bus");
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
  assert(devices[0].tensix_cores_used.has_value());
  assert(*devices[0].tensix_cores_used == 8);
  assert(devices[0].tensix_cores_available.has_value());
  assert(*devices[0].tensix_cores_available == 72);
  assert(devices[0].tensix.total.has_value());
  assert(*devices[0].tensix.total == 80);
  assert(devices[0].tensix.mesh_rows.has_value());
  assert(*devices[0].tensix.mesh_rows == 8);
  assert(devices[0].tensix.mesh_cols.has_value());
  assert(*devices[0].tensix.mesh_cols == 10);
  assert(devices[0].health_detail.hang_fault_count.has_value());
  assert(*devices[0].health_detail.hang_fault_count == 1);
  assert(devices[0].interconnect_links.size() == 1);
  assert(devices[0].interconnect_links[0].speed_gbps.has_value());
  assert(*devices[0].interconnect_links[0].speed_gbps == 200);
  assert(devices[0].allocation.claim_name.has_value());
  assert(*devices[0].allocation.claim_name == "tt-claim");
  assert(devices[0].janitor.scrub_count.has_value());
  assert(*devices[0].janitor.scrub_count == 3);

  const std::string metrics = tt::metrics::render_prometheus(devices);
  assert(metrics.find("tt_devices_discovered 1") != std::string::npos);
  assert(metrics.find("tt_device_info") != std::string::npos);
  assert(metrics.find("dev_major=\"241\"") != std::string::npos);
  assert(metrics.find("dev_minor=\"0\"") != std::string::npos);
  assert(metrics.find("dev_name=\"tenstorrent/0\"") != std::string::npos);
  assert(metrics.find("pci_vendor_id=\"0x1e52\"") != std::string::npos);
  assert(metrics.find("pci_device_id=\"0x401e\"") != std::string::npos);
  assert(metrics.find("pci_bdf=\"0000:01:00.0\"") != std::string::npos);
  assert(metrics.find("pci_driver=\"tenstorrent\"") != std::string::npos);
  assert(metrics.find("numa_node=\"-1\"") != std::string::npos);
  assert(metrics.find("iommu_group=\"17\"") != std::string::npos);
  assert(metrics.find("pci_current_link_speed=\"16.0 GT/s PCIe\"") !=
         std::string::npos);
  assert(metrics.find("pci_current_link_width=\"8\"") != std::string::npos);
  assert(metrics.find("pci_reset_method=\"bus\"") != std::string::npos);
  assert(metrics.find("tt_pci_resource_size_bytes") != std::string::npos);
  assert(metrics.find("resource=\"0\"") != std::string::npos);
  assert(metrics.find("536870912") != std::string::npos);
  assert(metrics.find("tt_pci_link_info") != std::string::npos);
  assert(metrics.find("state=\"current\",speed=\"16.0 GT/s PCIe\"") !=
         std::string::npos);
  assert(metrics.find("tt_pci_link_width_lanes") != std::string::npos);
  assert(metrics.find("state=\"max\"} 16") != std::string::npos);
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
  assert(metrics.find("tt_device_spec_tensix_cores") == std::string::npos);
  assert(metrics.find("tt_device_spec_memory_bytes") == std::string::npos);
  assert(metrics.find("tt_memory_used_bytes") != std::string::npos);
  assert(metrics.find("tt_memory_total_bytes") != std::string::npos);
  assert(metrics.find("tt_memory_free_bytes") != std::string::npos);
  assert(metrics.find("tt_memory_available_bytes") != std::string::npos);
  assert(metrics.find("tt_memory_bandwidth_bytes_per_second") != std::string::npos);
  assert(metrics.find("tt_memory_controller_count") != std::string::npos);
  assert(metrics.find("tt_tensix_cores_used") != std::string::npos);
  assert(metrics.find("tt_tensix_cores_available") != std::string::npos);
  assert(metrics.find("tt_tensix_cores_total") != std::string::npos);
  assert(metrics.find("tt_tensix_mesh_rows") != std::string::npos);
  assert(metrics.find("tt_health_hang_faults_total") != std::string::npos);
  assert(metrics.find("tt_interconnect_link_info") != std::string::npos);
  assert(metrics.find("peer=\"0000:02:00.0\"") != std::string::npos);
  assert(metrics.find("tt_dra_allocation_info") != std::string::npos);
  assert(metrics.find("claim_name=\"tt-claim\"") != std::string::npos);
  assert(metrics.find("tt_janitor_state_info") != std::string::npos);
  assert(metrics.find("tt_janitor_scrub_total") != std::string::npos);
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
  assert(json.find("\"bdf\":\"0000:01:00.0\"") != std::string::npos);
  assert(json.find("\"driver\":\"tenstorrent\"") != std::string::npos);
  assert(json.find("\"vendorId\":\"0x1e52\"") != std::string::npos);
  assert(json.find("\"deviceId\":\"0x401e\"") != std::string::npos);
  assert(json.find("\"iommuGroup\":17") != std::string::npos);
  assert(json.find("\"currentLinkSpeed\":\"16.0 GT/s PCIe\"") !=
         std::string::npos);
  assert(json.find("\"currentLinkWidth\":8") != std::string::npos);
  assert(json.find("\"maxLinkWidth\":16") != std::string::npos);
  assert(json.find("\"resetMethod\":\"bus\"") != std::string::npos);
  assert(json.find("\"spec\"") == std::string::npos);
  assert(json.find("\"tensixCores\":128") == std::string::npos);
  assert(json.find("\"memoryBytes\":24000000000") == std::string::npos);
  assert(json.find("\"usedBytes\":1024") != std::string::npos);
  assert(json.find("\"freeBytes\":2048") != std::string::npos);
  assert(json.find("\"bandwidthBytesPerSecond\":123456789") != std::string::npos);
  assert(json.find("\"controllerCount\":12") != std::string::npos);
  assert(json.find("\"total\":80") != std::string::npos);
  assert(json.find("\"meshRows\":8") != std::string::npos);
  assert(json.find("\"activeRegions\":\"0,0-1,3\"") != std::string::npos);
  assert(json.find("\"available\":72") != std::string::npos);
  assert(json.find("\"faultReason\":\"clear\"") != std::string::npos);
  assert(json.find("\"interconnectLinks\"") != std::string::npos);
  assert(json.find("\"speedGbps\":200") != std::string::npos);
  assert(json.find("\"claimName\":\"tt-claim\"") != std::string::npos);
  assert(json.find("\"scrubCount\":3") != std::string::npos);

  fs::remove_all(base);
}

void test_missing_root_is_empty() {
  tt::metrics::CollectorConfig config;
  config.sysfs_root = "/definitely/missing/tenstorrent";
  tt::metrics::SysfsCollector collector(config);
  assert(collector.collect().empty());
}

void test_metalium_workload_state_collection() {
  const fs::path base = make_temp_root();
  const fs::path root = base / "sysfs";
  const fs::path profiler_root = base / "metalium-profiler";
  fs::create_directories(root / "0" / "device");
  fs::create_directories(profiler_root / "0");

  const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
  write_file(profiler_root / "0" / "worker.state",
             "schema_version=1\n"
             "workload_id=default/worker-0\n"
             "pod_namespace=default\n"
             "pod_name=worker-0\n"
             "container_name=model\n"
             "active=1\n"
             "programs_observed=3\n"
             "tensix_cores_used=24\n"
             "tensix_cores_total=80\n"
             "sample_timestamp_seconds=" +
                 std::to_string(now) + "\n");

  tt::metrics::CollectorConfig config;
  config.sysfs_root = root;
  config.metalium_profiler_state_root = profiler_root;
  config.metalium_profiler_stale_after_seconds = 15;
  tt::metrics::SysfsCollector collector(config);
  auto devices = collector.collect();

  assert(devices.size() == 1);
  assert(devices[0].metalium_workloads.size() == 1);
  assert(devices[0].metalium_workloads[0].workload_id == "default/worker-0");
  assert(!devices[0].metalium_workloads[0].stale);
  assert(devices[0].tensix.used.has_value());
  assert(*devices[0].tensix.used == 24);
  assert(devices[0].tensix.total.has_value());
  assert(*devices[0].tensix.total == 80);
  assert(devices[0].tensix.available.has_value());
  assert(*devices[0].tensix.available == 56);
  assert(devices[0].tensix.source.has_value());
  assert(*devices[0].tensix.source == "metalium_profiler");

  const std::string metrics = tt::metrics::render_prometheus(devices);
  assert(metrics.find("tt_metalium_workload_active") != std::string::npos);
  assert(metrics.find("workload_id=\"default/worker-0\"") !=
         std::string::npos);
  assert(metrics.find("tt_metalium_workload_tensix_cores_used") !=
         std::string::npos);
  assert(metrics.find("tt_metalium_workload_core_occupancy_ratio") !=
         std::string::npos);

  const std::string json = tt::metrics::render_devices_json(devices);
  assert(json.find("\"metaliumWorkloads\"") != std::string::npos);
  assert(json.find("\"workloadId\":\"default/worker-0\"") !=
         std::string::npos);
  assert(json.find("\"tensixCoresUsed\":24") != std::string::npos);
  assert(json.find("\"source\":\"metalium_profiler\"") !=
         std::string::npos);

  write_file(profiler_root / "0" / "worker.state",
             "schema_version=1\n"
             "workload_id=default/worker-0\n"
             "active=1\n"
             "programs_observed=3\n"
             "tensix_cores_used=24\n"
             "tensix_cores_total=80\n"
             "sample_timestamp_seconds=1\n");
  devices = collector.collect();
  assert(devices[0].metalium_workloads[0].stale);
  assert(!devices[0].tensix.used.has_value());

  write_file(profiler_root / "0" / "worker.state",
             "schema_version=1\n"
             "workload_id=default/worker-0\n"
             "active=2\n"
             "programs_observed=3\n"
             "tensix_cores_used=24\n"
             "tensix_cores_total=80\n"
             "sample_timestamp_seconds=" +
                 std::to_string(now) + "\n");
  devices = collector.collect();
  assert(devices[0].metalium_workloads.empty());

  write_file(profiler_root / "0" / "worker.state",
             "schema_version=1\n"
             "workload_id=default/worker-0\n"
             "active=1\n"
             "programs_observed=3\n"
             "tensix_cores_used=24\n"
             "tensix_cores_total=80\n"
             "sample_timestamp_seconds=" +
                 std::to_string(now + 60) + "\n");
  devices = collector.collect();
  assert(devices[0].metalium_workloads[0].stale);

  fs::remove_all(base);
}

}  // namespace

int main() {
  test_parse_memory_usage_keyed_values();
  test_parse_memory_usage_unkeyed_values();
  test_parse_pci_resources();
  test_sysfs_collection_and_rendering();
  test_missing_root_is_empty();
  test_metalium_workload_state_collection();
  std::cout << "tt_metrics_exporter_tests passed\n";
  return 0;
}
