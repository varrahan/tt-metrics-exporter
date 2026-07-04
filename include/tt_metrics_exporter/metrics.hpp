#pragma once

#include <string>
#include <vector>

#include "tt_metrics_exporter/device.hpp"

namespace tt::metrics {

std::string render_prometheus(const std::vector<DeviceTelemetry>& devices);

}  // namespace tt::metrics
