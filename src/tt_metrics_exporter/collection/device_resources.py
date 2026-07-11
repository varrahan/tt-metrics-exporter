"""Memory and Tensix resource discovery from device sysfs."""

from __future__ import annotations

from ..models import DeviceTelemetry, SourceDiagnostics
from .parsers import parse_memory_usage, parse_mesh_dimensions


class DeviceResourceReader:
    def _populate_memory(self, device: DeviceTelemetry,
                         diagnostics: SourceDiagnostics) -> None:
        root, memory = device.sysfs_path, device.memory
        usage = self._read_first_text(
            root, ("memory_usage", "dram_usage", "device_memory_usage", "tt_memory_usage"),
            diagnostics,
        )
        if usage is not None:
            parsed = parse_memory_usage(usage)
            memory.used_bytes, memory.total_bytes = parsed.used_bytes, parsed.total_bytes
        if memory.used_bytes is None:
            memory.used_bytes = self._read_first_byte(root, (
                "memory_used_bytes", "dram_used_bytes", "device_memory_used_bytes",
                "allocated_memory_bytes",
            ), diagnostics)
        if memory.total_bytes is None:
            memory.total_bytes = self._read_first_byte(root, (
                "memory_total_bytes", "memory_capacity_bytes", "memory_size_bytes",
                "dram_total_bytes", "dram_capacity_bytes", "dram_size_bytes",
            ), diagnostics)
        memory.free_bytes = self._read_first_byte(
            root, ("memory_free_bytes", "dram_free_bytes", "device_memory_free_bytes"),
            diagnostics,
        )
        memory.available_bytes = self._read_first_byte(
            root, ("memory_available_bytes", "dram_available_bytes",
                   "device_memory_available_bytes"), diagnostics,
        )
        memory.bandwidth_bytes_per_second = self._read_first_byte(root, (
            "memory_bandwidth_bytes_per_second", "dram_bandwidth_bytes_per_second",
            "gddr_bandwidth_bytes_per_second",
        ), diagnostics)
        memory.type = self._read_first_text(
            root, ("memory_type", "dram_type", "gddr_type"), diagnostics
        )
        memory.controller_layout = self._read_first_text(root, (
            "gddr_controller_layout", "dram_controller_layout", "memory_controller_layout",
        ), diagnostics)
        memory.controller_count = self._read_first_int(root, (
            "gddr_controller_count", "dram_controller_count", "memory_controller_count",
        ), diagnostics)
        memory.controllers_per_asic = self._read_first_int(root, (
            "gddr_controllers_per_asic", "dram_controllers_per_asic",
            "memory_controllers_per_asic",
        ), diagnostics)
        memory.channel_count = self._read_first_int(root, (
            "dram_channel_count", "gddr_channel_count", "memory_channel_count",
        ), diagnostics)

    def _populate_tensix(self, device: DeviceTelemetry,
                         diagnostics: SourceDiagnostics) -> None:
        root, tensix = device.sysfs_path, device.tensix
        tensix.used = self._read_first_int(root, (
            "tensix_cores_used", "tensix_used", "active_tensix_cores",
            "tensix_active_cores",
        ), diagnostics)
        tensix.available = self._read_first_int(root, (
            "tensix_cores_available", "tensix_available", "available_tensix_cores",
        ), diagnostics)
        tensix.total = self._read_first_int(root, (
            "tensix_cores_total", "total_tensix_cores", "tensix_total",
            "tensix_core_count",
        ), diagnostics)
        tensix.mesh_rows = self._read_first_int(root, (
            "tensix_mesh_rows", "tensix_grid_rows", "tensix_rows", "core_grid_rows",
        ), diagnostics)
        tensix.mesh_cols = self._read_first_int(root, (
            "tensix_mesh_cols", "tensix_grid_cols", "tensix_cols", "core_grid_cols",
        ), diagnostics)
        if tensix.mesh_rows is None or tensix.mesh_cols is None:
            dimensions = self._read_first_text(
                root, ("tensix_mesh", "tensix_grid", "core_grid", "worker_grid"),
                diagnostics,
            )
            parsed = parse_mesh_dimensions(dimensions or "")
            if parsed is not None:
                tensix.mesh_rows, tensix.mesh_cols = parsed
        tensix.topology = self._read_first_text(
            root, ("tensix_topology", "tensix_layout"), diagnostics
        )
        tensix.active_regions = self._read_first_text(
            root, ("tensix_active_regions", "active_core_ranges", "active_core_grids"),
            diagnostics,
        )
        if any(value is not None for value in (
            tensix.used, tensix.available, tensix.total, tensix.mesh_rows,
            tensix.mesh_cols, tensix.topology, tensix.active_regions,
        )):
            tensix.source = "sysfs"
