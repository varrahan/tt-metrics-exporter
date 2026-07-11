# Test Suite

- `unit/` contains deterministic in-process contracts for collection, parsing,
  rendering, logging, runtime state, and the profiler publisher.
- `integration/` exercises the packaged service through subprocesses, sockets,
  signals, HTTP requests, and lifecycle transitions.
- `fixtures/` contains bounded regression inputs owned by tests.
- `support/` contains helpers used by tests and is not collected by pytest.

Run everything with `uv run scripts/ci/run_tests.py`. For a faster development
loop, run `uv run pytest tests/unit` before the integration suite.
