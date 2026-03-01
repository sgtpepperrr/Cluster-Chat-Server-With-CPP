#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
env_file="${repo_root}/.env"
schema_file="${repo_root}/docs/rebuild_schema.sql"

if [[ ! -f "${env_file}" ]]; then
    echo "Error: .env not found at ${env_file}" >&2
    exit 1
fi

if [[ ! -f "${schema_file}" ]]; then
    echo "Error: schema file not found at ${schema_file}" >&2
    exit 1
fi

set -a
# shellcheck disable=SC1090
source "${env_file}"
set +a

: "${CHAT_DB_HOST:?CHAT_DB_HOST is required in .env}"
: "${CHAT_DB_PORT:?CHAT_DB_PORT is required in .env}"
: "${CHAT_DB_USER:?CHAT_DB_USER is required in .env}"
: "${CHAT_DB_PASSWORD:?CHAT_DB_PASSWORD is required in .env}"
: "${CHAT_DB_CHARSET:=utf8mb4}"

if [[ "${REALLY_RESET_CHAT_DB:-}" != "YES" ]]; then
    cat <<EOF
Refusing to run destructive schema rebuild.

This operation will DROP and recreate chat tables:
  user, friend, allgroup, groupuser, offlinemessage

Run again with:
  REALLY_RESET_CHAT_DB=YES ./scripts/rebuild_chat_schema.sh
EOF
    exit 1
fi

MYSQL_PWD="${CHAT_DB_PASSWORD}" mysql \
    --host="${CHAT_DB_HOST}" \
    --port="${CHAT_DB_PORT}" \
    --user="${CHAT_DB_USER}" \
    --default-character-set="${CHAT_DB_CHARSET}" \
    < "${schema_file}"

echo "Schema rebuild complete: ${schema_file}"
