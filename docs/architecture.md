# Architecture diagrams

Four views of the same system, from the inside of one port out to how a
consuming application sees it. All diagrams are Mermaid, rendered inline
by GitHub and most Markdown viewers — no separate image-generation step
to keep in sync with the code.

## 1. Inside a single port

Every port follows the same four-layer shape: public functions never
touch SQL directly, validation never touches a driver, and the backend
is the only layer that knows which database it's talking to.

```mermaid
flowchart TD
    App["Caller's code"] --> Pub["Public API\ndb_connector / insert_data / retrieve_data\n(insertData/retrieveData, ml_insert_data/ml_retrieve_data, etc.)"]
    Pub --> Val["Validation\nallow-list checks: BCP 47 language_id,\nidentifier string_id/context, length caps,\nASCII-only lowercasing\n(validation.md)"]
    Val -- "rejected: raises/throws\nValidationError before any SQL runs" --> App
    Val --> Conn["Connector\npicks a backend by name,\nresolves credentials from\nMULTILANG_DB_* env vars\n(connectors.md)"]
    Conn --> Iface["Backend interface\n(vtable in C, ABC/interface/trait\nelsewhere) — same 2 methods:\ninsert, retrieve"]
    Iface --> Sqlite["SQLite backend"]
    Iface --> Pg["Postgres backend\n(sslmode: prefer/require/disable)"]
    Iface --> My["MySQL backend\n(sslmode: prefer/require/disable)"]
    Iface --> Fs["Filesystem backend\n(no server/driver)"]
    Sqlite --> DB1[("strings table")]
    Pg --> DB2[("strings table")]
    My --> DB3[("strings table")]
    Fs --> DB4[("content.json per row")]
```

The point of drawing it this way: validation sits *before* the backend
split, so every backend receives already-clean input and can rely
exclusively on parameterized queries — no backend re-implements or
re-checks the allow-list rules, and no backend ever builds SQL by string
concatenation.

## 2. One fixture driving five ports and four backend families

`conformance/cases.json` is the only place a test case is defined. Each
port has a thin runner that replays it; none of the five hand-duplicate
the case list.

```mermaid
flowchart LR
    Fixture["conformance/cases.json\n(shared operations + expected\nresults/errors, 61 cases)"]

    Fixture --> PyR["Python runner\ntest_conformance.py"]
    Fixture --> JsR["JS runner\nconformance.test.js"]
    Fixture --> PhpR["PHP runner\nConformanceTest.php"]
    Fixture --> GoR["Go runner\nconformance_test.go"]
    Fixture --> CR["C runner\ntest_conformance.c"]
    Fixture --> CppR["C++ runner\ntest_cpp.cpp"]

    PyR & JsR & PhpR & GoR & CR & CppR --> B1[(SQLite)]
    PyR & JsR & PhpR & GoR & CR & CppR --> B2[(Postgres)]
    PyR & JsR & PhpR & GoR & CR & CppR --> B3[(MySQL)]
    PyR & JsR & PhpR & GoR & CR & CppR --> B4[(Filesystem)]
```

SQLite and the filesystem backend both run the full fixture on every
test invocation (no server, so no reason not to); Postgres/MySQL run it
via `MULTILANG_DB_BACKEND=postgres|mysql` against a real server — see
diagram 3, which the filesystem backend has no part of since it needs
no container.

A case failing on exactly one (port, backend) pair — not all of them —
is precisely how every cross-language inconsistency listed in
[`validation.md`](validation.md#resolved-cross-language-inconsistencies)
was actually found: the fixture is identical, so a divergent result
means the port's *implementation* diverged, not the test data.

## 3. Containerized live-DB test topology

`conformance/run-live-db-tests.sh` builds one image per language (not
per language×backend) and runs both backends from inside that single
container, addressed by container IP rather than DNS.

```mermaid
flowchart TB
    subgraph net["private container network"]
        PG["postgres container\n(version via $POSTGRES_VERSION)"]
        MY["mysql container\n(version via $MYSQL_VERSION)"]
        PYC["python container\nruns run-both-backends.sh"]
        JSC["javascript container\nruns run-both-backends.sh"]
        PHPC["php container\nruns run-both-backends.sh"]
        GOC["go container\n(conformance.test prebuilt\nat image-build time)"]
        CC["c container\n(make test/test_conformance\ntest/test_cpp)"]
    end

    Script["run-live-db-tests.sh\nresolves PG/MY container IPs via\npodman/docker inspect, passes them\nas MULTILANG_PG_HOST/MULTILANG_MY_HOST"] -.configures.-> PYC & JSC & PHPC & GOC & CC

    PYC -- "1st: postgres,\n2nd: mysql" --> PG
    PYC --> MY
    JSC --> PG
    JSC --> MY
    PHPC --> PG
    PHPC --> MY
    GOC --> PG
    GOC --> MY
    CC --> PG
    CC --> MY
```

Nothing here runs on the host — per the containerized-testing
constraint, `run-live-db-tests.sh` itself only orchestrates `podman`/
`docker` commands; the actual test process, compiler, and language
runtime all execute inside the containers it starts.

## 4. How a consuming application integrates the library

Where the three public entry points sit relative to the caller's own
code — the same shape regardless of which port or backend is chosen.

```mermaid
flowchart LR
    subgraph YourApp["Your application"]
        Startup["Startup:\ndb_connector(backend, credentials)\nor env-var-only db_connector()"]
        Business["Business logic:\ninsert_data(...) when content is\nauthored/translated\nretrieve_data(...) when rendering\na user-facing string"]
    end

    Startup --> Conn(("connection\nhandle/object"))
    Conn --> Business
    Business --> Lib["multilang library\n(validation -> connector -> backend)"]
    Lib --> DB[("strings table (SQLite/Postgres/MySQL)\nor content.json tree (filesystem)")]
    DB --> Lib --> Business --> Rendered["Rendered string\n(or None/null if that\ntranslation doesn't exist yet)"]
```

The library never owns application state beyond the connection handle:
no caching layer, no global singleton, no framework coupling — a caller
decides when to call `retrieve_data` (e.g., per-request, or once at
startup into its own cache) and the library has no opinion about it.
