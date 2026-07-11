#!/usr/bin/env python3
"""Unit contracts for bounded structured logging."""

import io
import json
import unittest

from tt_metrics_exporter.app.logging import Logger, LogLevel


class LoggingTest(unittest.TestCase):
    def test_json_bounds_fields_and_level(self) -> None:
        output = io.StringIO()
        logger = Logger("json", LogLevel.INFO, output, wall_time=lambda: 0)
        logger.log(LogLevel.DEBUG, "hidden", "hidden")
        logger.log(LogLevel.INFO, "event", "line\nmessage", {"valid_field": "x" * 300, "bad-name": "ignored"})
        record = json.loads(output.getvalue())
        self.assertEqual(record["timestamp"], "1970-01-01T00:00:00.000Z")
        self.assertEqual(record["event"], "event")
        self.assertEqual(record["message"], "line\nmessage")
        self.assertEqual(len(record["valid_field"]), 256)
        self.assertNotIn("bad-name", record)

    def test_rate_limit_is_bounded(self) -> None:
        output = io.StringIO()
        now = [0.0]
        logger = Logger(output=output, monotonic=lambda: now[0], wall_time=lambda: 0)
        logger.log_rate_limited(LogLevel.WARN, "event", "first", "key")
        logger.log_rate_limited(LogLevel.WARN, "event", "hidden", "key")
        now[0] = 60
        logger.log_rate_limited(LogLevel.WARN, "event", "second", "key")
        self.assertEqual(len(output.getvalue().splitlines()), 2)
        self.assertNotIn("hidden", output.getvalue())


if __name__ == "__main__":
    unittest.main()
