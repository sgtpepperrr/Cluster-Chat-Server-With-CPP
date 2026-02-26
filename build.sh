#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BUILD_TYPE="Debug"
CLEAN=0
JOBS=""

usage() {
    cat <<'EOF'
Usage: ./build.sh [options]

Options:
  debug            Build with Debug type (default)
  release          Build with Release type
  clean            Remove build directory before building
  -j N, --jobs N   Number of parallel build jobs
  -h, --help       Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        release)
            BUILD_TYPE="Release"
            shift
            ;;
        clean)
            CLEAN=1
            shift
            ;;
        -j|--jobs)
            if [[ $# -lt 2 ]]; then
                echo "error: $1 requires a value" >&2
                exit 1
            fi
            JOBS="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown option '$1'" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ -z "${JOBS}" ]]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS="$(nproc)"
    else
        JOBS=4
    fi
fi

if [[ "${CLEAN}" -eq 1 ]]; then
    rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"

echo "[build] configure: type=${BUILD_TYPE}, jobs=${JOBS}"
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

echo "[build] compile"
cmake --build "${BUILD_DIR}" -- -j"${JOBS}"

echo "[build] done"
echo "  client: ${ROOT_DIR}/bin/ChatClient"
echo "  server: ${ROOT_DIR}/bin/ChatServer"
