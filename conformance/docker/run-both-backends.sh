#!/bin/sh
# Shared entrypoint for every language's conformance image. Runs the
# test command (passed as CMD/args) once against Postgres and once
# against MySQL, inside this single container — one container per
# language, not one per (language, backend) pair.
#
# Expects MULTILANG_PG_HOST/_PORT and MULTILANG_MYSQL_HOST/_PORT plus the
# shared MULTILANG_DB_USER/_PASSWORD/_NAME to already be set by the
# `docker run` invocation.

status=0

export MULTILANG_DB_BACKEND=postgres
export MULTILANG_DB_HOST="$MULTILANG_PG_HOST"
export MULTILANG_DB_PORT="${MULTILANG_PG_PORT:-5432}"
echo "=== postgres ==="
"$@" || status=1

export MULTILANG_DB_BACKEND=mysql
export MULTILANG_DB_HOST="$MULTILANG_MYSQL_HOST"
export MULTILANG_DB_PORT="${MULTILANG_MYSQL_PORT:-3306}"
echo ""
echo "=== mysql ==="
"$@" || status=1

exit "$status"
