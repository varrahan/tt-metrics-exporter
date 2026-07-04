#pragma once

#include "tt_metrics_exporter/device.hpp"

#include <string>
#include <vector>

namespace tt::metrics {

std::string render_devices_json(const std::vector<DeviceTelemetry>& devices);

}  // namespace tt::metrics
