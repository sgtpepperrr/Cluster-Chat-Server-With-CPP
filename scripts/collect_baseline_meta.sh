#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat <<'EOF'
Usage:
  ./scripts/collect_baseline_meta.sh [output]

Arguments:
  output   Optional output path.
           - If output is a directory, write: <output>/meta.md
           - If output is a file path, write exactly that file

Examples:
  ./scripts/collect_baseline_meta.sh
  ./scripts/collect_baseline_meta.sh bench/runs/baseline-2026-03-01/meta.md
  ./scripts/collect_baseline_meta.sh bench/runs/baseline-2026-03-01
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

default_out="${ROOT_DIR}/bench/runs/baseline-$(date +%F)/meta.md"
target="${1:-$default_out}"

if [[ -d "$target" ]]; then
    out_file="${target%/}/meta.md"
else
    if [[ "${target##*/}" == *.* ]]; then
        out_file="$target"
    else
        mkdir -p "$target"
        out_file="${target%/}/meta.md"
    fi
fi

mkdir -p "$(dirname "$out_file")"

if [[ "$out_file" == *.md ]]; then
    artifacts_dir="${out_file%.md}.artifacts"
else
    artifacts_dir="${out_file}.artifacts"
fi
mkdir -p "$artifacts_dir"

run_cmd() {
    local outfile="$1"
    shift
    bash -lc "$*" >"$outfile" 2>&1 || true
}

run_cmd "$artifacts_dir/os-release.txt" "cat /etc/os-release"
run_cmd "$artifacts_dir/uname.txt" "uname -a"
run_cmd "$artifacts_dir/lscpu.txt" "lscpu"
run_cmd "$artifacts_dir/memory.txt" "free -h"
run_cmd "$artifacts_dir/disk.txt" "df -h"
run_cmd "$artifacts_dir/ulimit.txt" "ulimit -n"
run_cmd "$artifacts_dir/sysctl-net.txt" \
    "sysctl net.core.somaxconn net.ipv4.tcp_tw_reuse net.ipv4.ip_local_port_range net.ipv4.tcp_max_syn_backlog"

run_cmd "$artifacts_dir/nginx-version.txt" "nginx -v"
run_cmd "$artifacts_dir/redis-version.txt" "redis-server --version"
run_cmd "$artifacts_dir/mysql-version.txt" "mysql --version"
run_cmd "$artifacts_dir/gxx-version.txt" "g++ --version"
run_cmd "$artifacts_dir/cmake-version.txt" "cmake --version"

if [[ -f /etc/nginx/nginx.conf ]]; then
    cp /etc/nginx/nginx.conf "$artifacts_dir/nginx.conf"
else
    echo "/etc/nginx/nginx.conf not found" >"$artifacts_dir/nginx.conf"
fi
run_cmd "$artifacts_dir/nginx-T.txt" "nginx -T"

run_cmd "$artifacts_dir/git-status.txt" "git -C \"$ROOT_DIR\" status --short"
run_cmd "$artifacts_dir/git-log-5.txt" "git -C \"$ROOT_DIR\" log --oneline -n 5"

git_commit="$(git -C "$ROOT_DIR" rev-parse HEAD 2>/dev/null || echo "N/A")"
git_branch="$(git -C "$ROOT_DIR" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "N/A")"
dirty_count="$(git -C "$ROOT_DIR" status --porcelain 2>/dev/null | wc -l | tr -d ' ')"
if [[ "${dirty_count:-0}" == "0" ]]; then
    git_dirty="clean"
else
    git_dirty="dirty (${dirty_count} files)"
fi

ts_local="$(date '+%Y-%m-%d %H:%M:%S %Z')"
ts_utc="$(date -u '+%Y-%m-%d %H:%M:%S UTC')"

cat >"$out_file" <<EOF
# Baseline Metadata Snapshot

- Collected at (local): ${ts_local}
- Collected at (UTC): ${ts_utc}
- Repository: ${ROOT_DIR}
- Git branch: ${git_branch}
- Git commit: ${git_commit}
- Worktree: ${git_dirty}

## Environment

### Machine & OS
- See: \`$(basename "$artifacts_dir")/os-release.txt\`
- See: \`$(basename "$artifacts_dir")/uname.txt\`
- See: \`$(basename "$artifacts_dir")/lscpu.txt\`
- See: \`$(basename "$artifacts_dir")/memory.txt\`
- See: \`$(basename "$artifacts_dir")/disk.txt\`

### Limits & Kernel Networking
- See: \`$(basename "$artifacts_dir")/ulimit.txt\`
- See: \`$(basename "$artifacts_dir")/sysctl-net.txt\`

### Dependency Versions
- See: \`$(basename "$artifacts_dir")/nginx-version.txt\`
- See: \`$(basename "$artifacts_dir")/redis-version.txt\`
- See: \`$(basename "$artifacts_dir")/mysql-version.txt\`
- See: \`$(basename "$artifacts_dir")/gxx-version.txt\`
- See: \`$(basename "$artifacts_dir")/cmake-version.txt\`

### Nginx Config Snapshot
- See: \`$(basename "$artifacts_dir")/nginx.conf\`
- See: \`$(basename "$artifacts_dir")/nginx-T.txt\`

## Scope
- This file is environment/config metadata only.
- Put benchmark results, incidents, and conclusions in \`report.md\` of the same run directory.
EOF

echo "[ok] wrote: $out_file"
echo "[ok] artifacts: $artifacts_dir"
