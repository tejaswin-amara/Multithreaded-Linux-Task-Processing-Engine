# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-09-18

### Added
- **Core Memory & Safety**: Replaced standard allocations with `safe_malloc` and `safe_calloc`; integrated the `loom_log` thread-safe logging subsystem across all modules.
- **Task Queue Hardening**: Implemented monotonic condition variable timeouts (`CLOCK_MONOTONIC`), `queue_try_push`, and `queue_push_timeout`.
- **Test Suite Expansion**: Added unit test coverage in `tests/test_suite.c` covering high-contention worker dispatch, queue capacity bounds, and history circular buffer rollover.
- **Automation & Packaging**: Added multi-stage Ubuntu Dockerfile and GitHub Actions CI workflow covering ASan, TSan, Valgrind, and stress test gates.

### Changed
- **Stress Testing**: Refactored `tests/stress_test.sh` to execute against `bin/task_engine` under concurrent CLI and HTTP workloads.
- **Documentation**: Synchronized requirements, architecture diagrams (Mermaid), test plans, and user manuals with Ubuntu platform baselines.
