"""Thread-safe publication of complete, immutable exporter generations."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
from threading import Lock

from ..models import CollectionResult


@dataclass(frozen=True, slots=True)
class ExporterSnapshot:
    generation: int
    collected_at: float
    rendered_at: float
    collection: CollectionResult
    prometheus: str
    devices_json: str


class SnapshotStore:
    def __init__(self) -> None:
        self._lock = Lock()
        self._snapshot: ExporterSnapshot | None = None

    def load(self) -> ExporterSnapshot | None:
        with self._lock:
            return self._snapshot

    def publish(self, collection: CollectionResult, prometheus: str,
                devices_json: str, collected_at: float,
                rendered_at: float) -> ExporterSnapshot:
        with self._lock:
            generation = 1 if self._snapshot is None else self._snapshot.generation + 1
            snapshot = ExporterSnapshot(
                generation, collected_at, rendered_at, deepcopy(collection),
                prometheus, devices_json,
            )
            self._snapshot = snapshot
            return snapshot
