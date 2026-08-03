"""
Runs the shared, language-agnostic conformance suite (../../conformance/
cases.json) against this Python implementation.

This is the enforcement mechanism for the "same functionality and results
across every language and every database" requirement: every port must run
these same cases, unmodified, against every backend it supports.

By default this runs against SQLite (a fresh temp file per case). Set
MULTILANG_DB_BACKEND=postgres or =mysql (plus MULTILANG_DB_HOST/_PORT/
_USER/_PASSWORD/_NAME) to run the exact same suite against a real server —
see ../../conformance/run-live-db-tests.sh, which stands up disposable
Postgres/MySQL containers and runs every port's suite against both.
"""

import json
import os

import pytest

from multilang import db_connector, retrieve_data, insert_data, ValidationError

_CASES_PATH = os.path.join(
    os.path.dirname(__file__), "..", "..", "conformance", "cases.json"
)

with open(_CASES_PATH, encoding="utf-8") as f:
    _SUITE = json.load(f)

_OPS = {"retrieve_data": retrieve_data, "insert_data": insert_data}

_BACKEND = os.environ.get("MULTILANG_DB_BACKEND", "sqlite")


def _isolate(conn):
    """
    Give `conn` a guaranteed-empty `strings` table before a case runs.

    SQLite gets a brand-new temp file per case (see the `conn` fixture),
    so there's nothing to do here for it. Postgres/MySQL share one
    long-lived server across the whole run, so each case truncates the
    table itself instead — cheaper than provisioning a throwaway
    database per case, and just as isolating since every case starts
    from zero rows either way.
    """
    if _BACKEND == "sqlite":
        return
    with conn._conn.cursor() as cur:
        cur.execute("TRUNCATE TABLE strings")
    conn._conn.commit()


@pytest.fixture
def conn(tmp_path):
    if _BACKEND == "sqlite":
        c = db_connector("sqlite", path=str(tmp_path / "conformance.db"))
    elif _BACKEND == "filesystem":
        c = db_connector("filesystem", path=str(tmp_path / "conformance-fs"))
    else:
        c = db_connector(_BACKEND)
        _isolate(c)
    try:
        yield c
    finally:
        c.close()


@pytest.mark.parametrize(
    "case", _SUITE["cases"], ids=[c["name"] for c in _SUITE["cases"]]
)
def test_conformance_case(case, conn):
    for step in case["operations"]:
        func = _OPS[step["op"]]
        args = step["args"]

        if step.get("expect_error"):
            with pytest.raises(ValidationError):
                func(conn, **args)
            continue

        result = func(conn, **args)
        if "expect" in step:
            assert result == step["expect"]
