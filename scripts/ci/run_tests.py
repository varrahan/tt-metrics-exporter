#!/usr/bin/env python3
"""Run the complete Python test suite with pytest."""

from __future__ import annotations

import os
from pathlib import Path
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = str(ROOT / "src")
sys.path.insert(0, SOURCE)
sys.path.insert(1, str(ROOT))
os.environ["PYTHONPATH"] = os.pathsep.join((SOURCE, str(ROOT), os.environ.get("PYTHONPATH", "")))


if __name__ == "__main__":
    os.environ.setdefault("PYTHONDONTWRITEBYTECODE", "1")
    raise SystemExit(pytest.main([str(ROOT / "tests")]))
