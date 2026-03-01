# Benchmark Workflow (Simple)

This is the only flow you need. No manual copy/paste for results.

## Directory Rule
Each test run must use its own directory:

`bench/runs/<run-id>/`

Example:
`bench/runs/baseline-2026-03-01/`

Keep all files for that run inside this directory (`report.md`, `meta.md`, `meta.artifacts/`).

## Step-by-Step

1. Create a run directory.
```bash
./scripts/new_bench_run.sh baseline-2026-03-01
```

2. Run benchmark (auto writes `report.md`).
```bash
python3 bench/run.py all --port 8000 --start-id 1 --count 200 --group-id 1 --messages 200 --run-dir bench/runs/baseline-2026-03-01
```

3. Collect environment metadata (automatic).
```bash
./scripts/collect_baseline_meta.sh bench/runs/baseline-2026-03-01/meta.md
```
`meta.md` only stores environment/config snapshot.

4. Record incidents in `report.md` (required, manual).
- `free(): invalid pointer`
- `Segmentation fault (core dumped)`
- `Broken pipe`

5. Final check: this run directory must contain:
- `report.md`
- `meta.md`
- `meta.artifacts/`

6. For the next optimization test, use a new run id:
```bash
./scripts/new_bench_run.sh reliability-v1-2026-03-05
```
