from __future__ import annotations

import pytest

from scripts.operations.soak_test import summarize, validate


def rows() -> list[dict[str, int | float]]:
    return [
        {"successful_scrapes": 4, "failed_scrapes": 0, "rss_bytes": 100, "threads": 2, "file_descriptors": 5, "series": 12, "maximum_scrape_seconds": 0.1},
        {"successful_scrapes": 4, "failed_scrapes": 0, "rss_bytes": 120, "threads": 2, "file_descriptors": 5, "series": 13, "maximum_scrape_seconds": 0.2},
    ]


def test_summarize_records_availability_latency_and_growth() -> None:
    summary = summarize(rows())
    assert summary == {"samples": 2, "successful_scrapes": 8, "failed_scrapes": 0, "rss_growth_bytes": 20, "thread_growth": 0, "descriptor_growth": 0, "maximum_series": 13, "maximum_scrape_seconds": 0.2}


@pytest.mark.parametrize(
    ("field", "value", "message"),
    [
        ("failed_scrapes", 1, "failed scrapes"),
        ("maximum_series", 0, "required series"),
        ("maximum_scrape_seconds", 4.0, "scrape latency"),
        ("rss_growth_bytes", 1025, "resource growth"),
        ("thread_growth", 3, "resource growth"),
        ("descriptor_growth", 3, "resource growth"),
    ],
)
def test_validate_rejects_failed_soak_evidence(field: str, value: int | float, message: str) -> None:
    summary = summarize(rows())
    summary[field] = value
    with pytest.raises(SystemExit, match=message):
        validate(summary, 1024, 0, 3.0, 1)


def test_validate_accepts_healthy_soak_evidence() -> None:
    validate(summarize(rows()), 1024, 0, 3.0, 1)
