# Thin wrapper around each port's own tooling -- doesn't replace it,
# just removes the need to `cd` into five directories by hand.
#
# Every test target runs inside a disposable, single-use container --
# never directly against a host toolchain. See AGENTS.md ("Tests always
# run in disposable containers") and conformance/run-unit-tests.sh /
# conformance/run-live-db-tests.sh for why: reproducible regardless of
# what's installed on the host, and nothing (installed deps, temp DB
# files, build artifacts) is left behind once the container exits.
# Requires docker or podman.

.PHONY: test test-python test-javascript test-php test-go test-c \
        conformance conformance-% help

help:
	@echo "test               - run every port's unit tests (SQLite/filesystem), each in a disposable container"
	@echo "test-<lang>        - run just one port's unit tests (python|javascript|php|go|c)"
	@echo "conformance        - full live-DB suite, all ports, real Postgres+MySQL, disposable containers"
	@echo "conformance-<lang> - live-DB suite for one port only"

test:
	./conformance/run-unit-tests.sh

test-python:
	./conformance/run-unit-tests.sh python

test-javascript:
	./conformance/run-unit-tests.sh javascript

test-php:
	./conformance/run-unit-tests.sh php

test-go:
	./conformance/run-unit-tests.sh go

test-c:
	./conformance/run-unit-tests.sh c

conformance:
	./conformance/run-live-db-tests.sh

conformance-%:
	./conformance/run-live-db-tests.sh $*
