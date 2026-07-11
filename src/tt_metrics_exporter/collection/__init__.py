"""Secure telemetry acquisition from sysfs and node-local state."""

from .collector import SysfsCollector

__all__ = ["SysfsCollector"]
