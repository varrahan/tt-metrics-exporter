"""Small profiled TTNN workload for validating dynamic exporter metrics."""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import time

from metalium_profiler_publisher import MetaliumProfilerPublisher


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device-id", type=int, default=0)
    parser.add_argument("--iterations", type=int, default=30)
    parser.add_argument("--interval-seconds", type=float, default=1.0)
    parser.add_argument(
        "--state-root",
        type=Path,
        default=Path("/var/lib/tt-device-plugin/metalium-profiler"),
    )
    parser.add_argument("--workload-id", default="metalium-dynamic-example")
    parser.add_argument(
        "--pod-uid",
        default=os.environ.get("POD_UID", "metalium-dynamic-example"),
        help="Trusted workload-directory name used by the v2 validation layout",
    )
    parser.add_argument(
        "--device-key",
        help="Exporter device key (sysfs ID, PCI BDF, or character-device basename)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    os.environ.setdefault("TT_METAL_PROFILER_DISABLE_DUMP_TO_FILES", "1")
    MetaliumProfilerPublisher.validate_profiler_environment()

    import ttnn  # Profiler variables must be validated before TTNN initializes.

    device = ttnn.open_device(device_id=args.device_id)
    try:
        if not args.pod_uid or args.pod_uid in {".", ".."} or "/" in args.pod_uid:
            raise ValueError("--pod-uid must be one safe path component")
        workload_state_root = args.state_root / "v2" / "workloads" / args.pod_uid
        with MetaliumProfilerPublisher(
            workload_state_root,
            args.workload_id,
            device_keys=(args.device_id,),
            device_key_map=(
                {args.device_id: args.device_key} if args.device_key else None
            ),
        ) as publisher:
            tensor = ttnn.ones(
                (1, 1, 32, 32),
                dtype=ttnn.bfloat16,
                layout=ttnn.TILE_LAYOUT,
                device=device,
            )
            for iteration in range(args.iterations):
                tensor = ttnn.add(tensor, float(iteration + 1))
                ttnn.synchronize_device(device)
                summary = publisher.sample(device)
                print(json.dumps(summary, sort_keys=True), flush=True)
                if args.interval_seconds > 0:
                    time.sleep(args.interval_seconds)
    finally:
        ttnn.close_device(device)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
