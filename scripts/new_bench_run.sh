#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUNS_DIR="${ROOT_DIR}/bench/runs"

usage() {
    cat <<'EOF'
Usage:
  ./scripts/new_bench_run.sh <run-id>

Examples:
  ./scripts/new_bench_run.sh baseline-2026-03-01
  ./scripts/new_bench_run.sh reliability-v1-2026-03-05
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

if [[ $# -ne 1 ]]; then
    usage
    exit 1
fi

run_id="$1"
run_dir="${RUNS_DIR}/${run_id}"

if [[ -d "$run_dir" ]]; then
    echo "error: run directory already exists: $run_dir" >&2
    exit 1
fi

mkdir -p "$run_dir"

cat <<EOF
[ok] created run directory:
  ${run_dir}

Next:
1) Run benchmark (auto-generates report):
   python3 bench/run.py all --port 8000 --start-id 1 --count 200 --group-id 1 --messages 200 --run-dir ${run_dir}

2) Collect environment metadata:
   ./scripts/collect_baseline_meta.sh ${run_dir}/meta.md

3) Add incident logs into:
   ${run_dir}/report.md  (Section: Incidents)
EOF
