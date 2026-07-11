#!/usr/bin/env python3
"""Assert exporter monitoring and dashboard invariants."""

import json
from pathlib import Path
import sys

import yaml


def main() -> None:
    rendered = Path(sys.argv[1])
    rules_output = Path(sys.argv[2])
    documents = [doc for doc in yaml.safe_load_all(rendered.read_text()) if doc]
    monitor = next(doc for doc in documents if doc["kind"] == "ServiceMonitor")
    endpoint = monitor["spec"]["endpoints"][0]
    assert endpoint["interval"] == "15s"
    assert endpoint["scrapeTimeout"] == "5s"
    assert 0 < endpoint["sampleLimit"] <= 20000

    rule_resource = next(doc for doc in documents if doc["kind"] == "PrometheusRule")
    alerts = [
        rule
        for group in rule_resource["spec"]["groups"]
        for rule in group["rules"]
        if "alert" in rule
    ]
    assert len(alerts) >= 12
    for alert in alerts:
        assert alert.get("for")
        assert alert["labels"]["severity"] in {"warning", "critical"}
        assert {"summary", "description", "runbook_url"} <= alert["annotations"].keys()
    rules_output.write_text(yaml.safe_dump({"groups": rule_resource["spec"]["groups"]}))

    dashboard = json.loads(
        Path("deploy/kubernetes/monitoring/dashboard.json").read_text()
    )
    titles = {panel["title"] for panel in dashboard["panels"]}
    assert len(titles) >= 14
    required = {
        "Exporter readiness by node",
        "Snapshot age",
        "Collection duration",
        "Collection issues",
        "Discovered devices",
        "PCI and firmware identity",
        "Temperature and clocks",
        "Power management",
        "Memory and Tensix",
        "Workload core occupancy",
        "Profiler stale and rejected records",
        "Reset, quarantine, fault, OOM and hang",
        "Scale-out links",
        "Exporter version distribution",
    }
    assert required <= titles
    assert all("noValue" in panel.get("fieldConfig", {}).get("defaults", {}) for panel in dashboard["panels"])


if __name__ == "__main__":
    main()
