#!/usr/bin/env python3
"""Soak a running exporter and record bounded resource-growth evidence."""

import argparse
import csv
import json
from pathlib import Path
import re
import threading
import time
import urllib.request


def process_stats(pid: int | None) -> tuple[int, int, int]:
    if pid is None:
        return 0, 0, 0
    status = Path(f"/proc/{pid}/status").read_text()
    rss_match = re.search(r"^VmRSS:\s+(\d+) kB$", status, re.MULTILINE)
    threads_match = re.search(r"^Threads:\s+(\d+)$", status, re.MULTILINE)
    rss = int(rss_match.group(1)) * 1024 if rss_match else 0
    threads = int(threads_match.group(1)) if threads_match else 0
    descriptors = len(list(Path(f"/proc/{pid}/fd").iterdir()))
    return rss, threads, descriptors


def scrape(url: str, results: list[tuple[float, int, str]]) -> None:
    started = time.monotonic()
    try:
        with urllib.request.urlopen(url, timeout=3) as response:
            payload = response.read().decode()
            results.append((time.monotonic() - started, len(payload), payload))
    except Exception:
        results.append((time.monotonic() - started, 0, ""))


def metric(payload: str, name: str) -> float:
    match = re.search(rf"^{re.escape(name)} ([0-9.eE+-]+)$", payload, re.MULTILINE)
    return float(match.group(1)) if match else 0.0


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--endpoint", default="http://127.0.0.1:9400/metrics")
    parser.add_argument("--pid", type=int)
    parser.add_argument("--duration-seconds", type=int, default=72 * 60 * 60)
    parser.add_argument("--interval-seconds", type=float, default=5)
    parser.add_argument("--concurrency", type=int, default=8)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--maximum-rss-growth-bytes", type=int, default=16 * 1024 * 1024)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)

    rows = []
    deadline = time.monotonic() + args.duration_seconds
    while time.monotonic() < deadline:
        results: list[tuple[float, int, str]] = []
        workers = [
            threading.Thread(target=scrape, args=(args.endpoint, results))
            for _ in range(args.concurrency)
        ]
        for worker in workers:
            worker.start()
        for worker in workers:
            worker.join()
        rss, threads, descriptors = process_stats(args.pid)
        successful = [result for result in results if result[1] > 0]
        payload = successful[0][2] if successful else ""
        rows.append(
            {
                "timestamp": int(time.time()),
                "successful_scrapes": len(successful),
                "maximum_scrape_seconds": max((r[0] for r in results), default=0),
                "payload_bytes": max((r[1] for r in results), default=0),
                "series": sum(1 for line in payload.splitlines() if line and not line.startswith("#")),
                "collection_seconds": metric(payload, "tt_exporter_collection_duration_seconds"),
                "snapshot_age_seconds": metric(payload, "tt_exporter_snapshot_age_seconds"),
                "rss_bytes": rss,
                "threads": threads,
                "file_descriptors": descriptors,
            }
        )
        time.sleep(args.interval_seconds)

    with args.output.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    warm = rows[len(rows) // 4 :] or rows
    summary = {
        "samples": len(rows),
        "rss_growth_bytes": warm[-1]["rss_bytes"] - warm[0]["rss_bytes"],
        "thread_growth": warm[-1]["threads"] - warm[0]["threads"],
        "descriptor_growth": warm[-1]["file_descriptors"] - warm[0]["file_descriptors"],
        "maximum_series": max(row["series"] for row in rows),
        "maximum_scrape_seconds": max(row["maximum_scrape_seconds"] for row in rows),
    }
    args.output.with_suffix(".json").write_text(json.dumps(summary, indent=2) + "\n")
    if (
        summary["rss_growth_bytes"] > args.maximum_rss_growth_bytes
        or summary["thread_growth"] > 2
        or summary["descriptor_growth"] > 2
    ):
        raise SystemExit("sustained resource growth exceeded the soak budget")


if __name__ == "__main__":
    main()
