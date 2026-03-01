#!/usr/bin/env python3
"""
Benchmark tool for Cluster Chat Server.

No external dependencies — uses only the Python 3.12 standard library.

Prerequisites
-------------
1. Bench users seeded via  scripts/generate_bench_sql.sh
2. ChatServer node(s) running (and optionally Nginx)
3. Bench user states reset to 'offline' before each run

Discover bench data
-------------------
  mysql -u cppuser -p -e "SELECT MIN(id), MAX(id), COUNT(*) FROM chat.user WHERE name LIKE 'bench_%'"
  mysql -u cppuser -p -e "SELECT id FROM chat.allgroup WHERE groupname = 'benchgroup'"

Reset before each run
---------------------
  mysql -u cppuser -p -e "UPDATE chat.user SET state='offline' WHERE name LIKE 'bench_%'"
  mysql -u cppuser -p -e "DELETE FROM chat.offlinemessage WHERE userid IN \
        (SELECT id FROM chat.user WHERE name LIKE 'bench_%')"

Examples
--------
  python3 bench/run.py login --port 8000 --start-id 100 --count 100
  python3 bench/run.py chat  --port 8000 --start-id 100 --count 100 --messages 200
  python3 bench/run.py group --port 8000 --start-id 100 --group-id 5 --messages 200
  python3 bench/run.py all   --port 8000 --start-id 100 --count 200 --group-id 5
  python3 bench/run.py all   --port 8000 --start-id 100 --count 200 --group-id 5 --run-dir bench/runs/baseline-2026-03-01
"""

from __future__ import annotations

import argparse
import asyncio
import json
import math
import os
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Any

# --------------------------------------------------------------------------- #
# Protocol constants  (must match include/public.hpp  EnMsgType)
# --------------------------------------------------------------------------- #
LOGIN_MSG = 1
LOGIN_MSG_ACK = 2
LOGOUT_MSG = 3
ONE_CHAT_MSG = 6
GROUP_CHAT_MSG = 10


# --------------------------------------------------------------------------- #
# TCP / JSON connection
# --------------------------------------------------------------------------- #
class ChatConnection:
    """Async TCP connection that speaks the chat-server protocol.

    Client -> Server:  JSON string + 0x00 terminator
    Server -> Client:  bare JSON string (no delimiter)

    We use ``json.JSONDecoder.raw_decode`` on the receive side to find
    message boundaries in the byte stream.
    """

    def __init__(self, host: str, port: int) -> None:
        self.host = host
        self.port = port
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._buf = b""
        self._dec = json.JSONDecoder()

    async def connect(self) -> None:
        self._reader, self._writer = await asyncio.open_connection(
            self.host, self.port,
        )

    async def send(self, msg: dict) -> None:
        payload = json.dumps(msg, ensure_ascii=False).encode("utf-8") + b"\x00"
        self._writer.write(payload)  # type: ignore[union-attr]
        await self._writer.drain()  # type: ignore[union-attr]

    async def recv(self, timeout: float = 15.0) -> dict:
        deadline = time.monotonic() + timeout
        while True:
            # try to parse one JSON object from the buffer
            text = self._buf.decode("utf-8", errors="replace")
            idx = 0
            while idx < len(text) and text[idx] in " \t\r\n\x00":
                idx += 1
            if idx < len(text):
                try:
                    obj, end = self._dec.raw_decode(text, idx)
                    self._buf = self._buf[len(text[:end].encode("utf-8")):]
                    return obj
                except json.JSONDecodeError:
                    pass  # incomplete — read more bytes

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("recv timeout")
            try:
                chunk = await asyncio.wait_for(
                    self._reader.read(4096),  # type: ignore[union-attr]
                    timeout=remaining,
                )
            except asyncio.TimeoutError:
                raise TimeoutError("recv timeout")
            if not chunk:
                raise ConnectionError("closed by peer")
            self._buf += chunk

    async def close(self) -> None:
        if self._writer is not None:
            try:
                self._writer.close()
                await self._writer.wait_closed()
            except Exception:
                pass
            self._writer = None


# --------------------------------------------------------------------------- #
# Result container
# --------------------------------------------------------------------------- #
def _pct(sorted_vals: list[float], p: float) -> float:
    """Interpolated percentile on a *sorted* list."""
    if not sorted_vals:
        return 0.0
    k = (len(sorted_vals) - 1) * p / 100.0
    f, c = math.floor(k), math.ceil(k)
    if f == c:
        return sorted_vals[int(k)]
    return sorted_vals[f] * (c - k) + sorted_vals[c] * (k - f)


@dataclass
class BenchResult:
    scenario: str
    concurrency: int
    total_requests: int
    duration_s: float
    latencies_ms: list[float] = field(default_factory=list)
    successes: int = 0
    errors: int = 0
    disconnects: int = 0
    extra: dict[str, Any] = field(default_factory=dict)

    @property
    def throughput(self) -> float:
        return self.total_requests / self.duration_s if self.duration_s > 0 else 0.0

    @property
    def error_rate_pct(self) -> float:
        return self.errors / self.total_requests * 100 if self.total_requests else 0.0

    def p(self, pct: float) -> float:
        return _pct(sorted(self.latencies_ms), pct)

    def print_report(self) -> None:
        s = sorted(self.latencies_ms)
        w = 52
        print()
        print("=" * w)
        print(f"  {self.scenario}")
        print("=" * w)
        print(f"  Concurrency:      {self.concurrency}")
        print(f"  Total requests:   {self.total_requests}")
        print(f"  Duration:         {self.duration_s:.2f} s")
        print(f"  Throughput:       {self.throughput:.1f} req/s")
        if s:
            print(f"  Latency min:      {s[0]:.2f} ms")
            print(f"  Latency P50:      {_pct(s, 50):.2f} ms")
            print(f"  Latency P95:      {_pct(s, 95):.2f} ms")
            print(f"  Latency P99:      {_pct(s, 99):.2f} ms")
            print(f"  Latency max:      {s[-1]:.2f} ms")
        print(f"  Successes:        {self.successes}")
        print(f"  Errors:           {self.errors}")
        print(f"  Disconnects:      {self.disconnects}")
        print(f"  Error rate:       {self.error_rate_pct:.1f}%")
        if "fanout" in self.extra:
            fo = self.extra["fanout"]
            nr = fo["receivers"]
            print(f"  --- Fan-out ({nr} receivers) ---")
            print(f"  Complete msgs:    {fo['complete']}/{self.total_requests}")
            if fo["latencies"]:
                fs = sorted(fo["latencies"])
                print(f"  Fan-out P50:      {_pct(fs, 50):.2f} ms")
                print(f"  Fan-out P95:      {_pct(fs, 95):.2f} ms")
                print(f"  Fan-out P99:      {_pct(fs, 99):.2f} ms")
        print("=" * w)


# --------------------------------------------------------------------------- #
# Scenario 1 — Login throughput
# --------------------------------------------------------------------------- #
async def _login_one(host: str, port: int, uid: int, pwd: str) -> dict:
    conn = ChatConnection(host, port)
    try:
        await conn.connect()
        t0 = time.monotonic()
        await conn.send({"msgId": LOGIN_MSG, "id": uid, "password": pwd})
        resp = await conn.recv()
        t1 = time.monotonic()
        ok = resp.get("errno") == 0
        if ok:
            await conn.send({"msgId": LOGOUT_MSG, "id": uid})
            await asyncio.sleep(0.02)
        return {"lat": (t1 - t0) * 1000, "ok": ok, "err": None}
    except Exception as exc:
        return {"lat": None, "ok": False, "err": str(exc)}
    finally:
        await conn.close()


async def bench_login(host: str, port: int, uids: list[int], pwd: str) -> BenchResult:
    n = len(uids)
    print(f"[login] {n} concurrent logins ...")
    t0 = time.monotonic()
    results = await asyncio.gather(*[_login_one(host, port, u, pwd) for u in uids])
    dur = time.monotonic() - t0

    res = BenchResult("Login Throughput", n, n, dur)
    for r in results:
        if r["ok"]:
            res.successes += 1
            res.latencies_ms.append(r["lat"])
        else:
            res.errors += 1
            if r["err"] and "connect" in (r["err"] or "").lower():
                res.disconnects += 1
            if r["lat"] is not None:
                res.latencies_ms.append(r["lat"])
    return res


# --------------------------------------------------------------------------- #
# Scenario 2 — One-to-one chat throughput / latency
# --------------------------------------------------------------------------- #
async def _chat_recv(
    conn: ChatConnection, expected: int, lats: list[float],
) -> None:
    got = 0
    while got < expected:
        try:
            msg = await conn.recv(timeout=30)
        except (TimeoutError, ConnectionError):
            break
        if msg.get("msgId") == ONE_CHAT_MSG:
            payload: str = msg.get("msg", "")
            if payload.startswith("bench|"):
                t_send = float(payload.split("|")[2])
                lats.append((time.monotonic() - t_send) * 1000)
            got += 1


async def _chat_pair(
    host: str, port: int,
    s_id: int, r_id: int, pwd: str,
    num_msgs: int, interval: float,
) -> dict:
    cs = ChatConnection(host, port)
    cr = ChatConnection(host, port)
    lats: list[float] = []
    try:
        await cs.connect()
        await cr.connect()

        # login receiver first so it is ready to receive
        await cr.send({"msgId": LOGIN_MSG, "id": r_id, "password": pwd})
        rr = await cr.recv()
        if rr.get("errno") != 0:
            return {"lats": [], "errs": num_msgs}

        await cs.send({"msgId": LOGIN_MSG, "id": s_id, "password": pwd})
        rs = await cs.recv()
        if rs.get("errno") != 0:
            return {"lats": [], "errs": num_msgs}

        await asyncio.sleep(0.05)

        recv_task = asyncio.create_task(_chat_recv(cr, num_msgs, lats))

        for seq in range(num_msgs):
            t = time.monotonic()
            await cs.send({
                "msgId": ONE_CHAT_MSG,
                "id": s_id, "name": f"b{s_id}",
                "to": r_id,
                "msg": f"bench|{seq}|{t}",
                "time": "2025-01-01 00:00:00",
            })
            if interval > 0:
                await asyncio.sleep(interval)

        # wait for the receiver to collect all messages
        try:
            await asyncio.wait_for(asyncio.shield(recv_task), timeout=30)
        except asyncio.TimeoutError:
            pass
        recv_task.cancel()
        try:
            await recv_task
        except asyncio.CancelledError:
            pass

        # logout
        await cs.send({"msgId": LOGOUT_MSG, "id": s_id})
        await cr.send({"msgId": LOGOUT_MSG, "id": r_id})
        await asyncio.sleep(0.02)
    except Exception:
        pass
    finally:
        await cs.close()
        await cr.close()

    return {"lats": lats, "errs": num_msgs - len(lats)}


async def bench_one_chat(
    host: str, port: int, uids: list[int], pwd: str,
    msgs: int, interval: float,
) -> BenchResult:
    pairs = len(uids) // 2
    print(f"[chat] {pairs} pairs x {msgs} msgs (interval {interval*1000:.0f} ms) ...")

    t0 = time.monotonic()
    tasks = [
        _chat_pair(host, port, uids[i * 2], uids[i * 2 + 1], pwd, msgs, interval)
        for i in range(pairs)
    ]
    results = await asyncio.gather(*tasks)
    dur = time.monotonic() - t0

    total = pairs * msgs
    all_lats: list[float] = []
    errs = 0
    for r in results:
        all_lats.extend(r["lats"])
        errs += r["errs"]

    return BenchResult(
        "One-to-One Chat", pairs, total, dur,
        all_lats, len(all_lats), errs,
    )


# --------------------------------------------------------------------------- #
# Scenario 3 — Group chat  (1 sender -> N receivers)
# --------------------------------------------------------------------------- #
async def _grp_recv(
    conn: ChatConnection, expected: int, lats_map: dict[int, list[float]],
) -> None:
    got = 0
    while got < expected:
        try:
            msg = await conn.recv(timeout=30)
        except (TimeoutError, ConnectionError):
            break
        if msg.get("msgId") == GROUP_CHAT_MSG:
            payload: str = msg.get("msg", "")
            if payload.startswith("bench|"):
                parts = payload.split("|")
                seq = int(parts[1])
                t_send = float(parts[2])
                lats_map.setdefault(seq, []).append(
                    (time.monotonic() - t_send) * 1000
                )
            got += 1


async def bench_group_chat(
    host: str, port: int, uids: list[int], pwd: str,
    group_id: int, msgs: int, interval: float,
) -> BenchResult:
    sender_id = uids[0]
    rcv_ids = uids[1:]
    nr = len(rcv_ids)
    print(f"[group] 1 sender -> {nr} receivers, {msgs} msgs, group {group_id} ...")

    cs = ChatConnection(host, port)
    crs = [ChatConnection(host, port) for _ in rcv_ids]
    lats_map: dict[int, list[float]] = {}
    dur = 0.0

    try:
        # connect all
        await cs.connect()
        for c in crs:
            await c.connect()

        # login receivers
        for c, uid in zip(crs, rcv_ids):
            await c.send({"msgId": LOGIN_MSG, "id": uid, "password": pwd})
            r = await c.recv()
            if r.get("errno") != 0:
                print(f"  warn: user {uid} login errno={r.get('errno')}")

        # login sender
        await cs.send({"msgId": LOGIN_MSG, "id": sender_id, "password": pwd})
        r = await cs.recv()
        if r.get("errno") != 0:
            print(f"  sender {sender_id} login failed")
            return BenchResult(f"Group Chat (1->{nr})", nr + 1, msgs, 0)

        await asyncio.sleep(0.15)

        # start receivers
        recv_tasks = [
            asyncio.create_task(_grp_recv(c, msgs, lats_map)) for c in crs
        ]

        # send messages
        t0 = time.monotonic()
        for seq in range(msgs):
            t = time.monotonic()
            await cs.send({
                "msgId": GROUP_CHAT_MSG,
                "id": sender_id, "name": f"b{sender_id}",
                "groupid": group_id,
                "msg": f"bench|{seq}|{t}",
                "time": "2025-01-01 00:00:00",
            })
            if interval > 0:
                await asyncio.sleep(interval)

        # wait for delivery
        await asyncio.sleep(min(10, msgs * 0.05))
        for t2 in recv_tasks:
            t2.cancel()
        await asyncio.gather(*recv_tasks, return_exceptions=True)
        dur = time.monotonic() - t0

        # logout
        await cs.send({"msgId": LOGOUT_MSG, "id": sender_id})
        for c, uid in zip(crs, rcv_ids):
            await c.send({"msgId": LOGOUT_MSG, "id": uid})
        await asyncio.sleep(0.05)
    except Exception as exc:
        print(f"  error: {exc}")
    finally:
        await cs.close()
        for c in crs:
            await c.close()

    # compute stats
    all_lats: list[float] = []
    fanout_lats: list[float] = []
    complete = 0
    for seq in range(msgs):
        if seq in lats_map:
            ls = lats_map[seq]
            all_lats.extend(ls)
            if len(ls) == nr:
                fanout_lats.append(max(ls))
                complete += 1

    total_expected = msgs * nr
    res = BenchResult(
        f"Group Chat (1->{nr})", nr + 1, msgs, dur,
        all_lats, len(all_lats), total_expected - len(all_lats),
    )
    res.extra["fanout"] = {
        "receivers": nr,
        "complete": complete,
        "latencies": fanout_lats,
    }
    return res


# --------------------------------------------------------------------------- #
# Summary table
# --------------------------------------------------------------------------- #
def print_config(args: argparse.Namespace) -> None:
    print()
    print("=" * 52)
    print("  Benchmark Configuration")
    print("=" * 52)
    print(f"  Target:           {args.host}:{args.port}")
    print(f"  User IDs:         {args.start_id} .. "
          f"{args.start_id + args.count - 1}  ({args.count} users)")
    print(f"  Messages/sender:  {args.messages}")
    print(f"  Send interval:    {args.interval * 1000:.1f} ms")
    if args.group_id:
        print(f"  Group ID:         {args.group_id}  "
              f"({args.group_members} members)")
    print("=" * 52)


def print_summary(results: list[BenchResult]) -> None:
    print()
    print("## Baseline Summary")
    print()
    print("| Scenario | Concurrency | Requests | Throughput |"
          "  P50  |  P95  |  P99  | Errors |")
    print("|----------|-------------|----------|------------|"
          "-------|-------|-------|--------|")
    for r in results:
        print(
            f"| {r.scenario:<22s} "
            f"| {r.concurrency:>11} "
            f"| {r.total_requests:>8} "
            f"| {r.throughput:>8.1f}/s "
            f"| {r.p(50):>5.1f} "
            f"| {r.p(95):>5.1f} "
            f"| {r.p(99):>5.1f} "
            f"| {r.error_rate_pct:>5.1f}% |"
        )
    print()


# --------------------------------------------------------------------------- #
# Markdown report
# --------------------------------------------------------------------------- #
def _run_cmd_capture(cmd: list[str]) -> str:
    try:
        cp = subprocess.run(
            cmd,
            check=False,
            capture_output=True,
            text=True,
        )
        out = (cp.stdout or cp.stderr or "").strip()
        return out if out else "N/A"
    except Exception:
        return "N/A"


def _git_branch() -> str:
    return _run_cmd_capture(["git", "rev-parse", "--abbrev-ref", "HEAD"])


def _git_commit() -> str:
    return _run_cmd_capture(["git", "rev-parse", "HEAD"])


def _results_table(results: list[BenchResult]) -> str:
    lines = [
        "| Scenario | Concurrency | Requests | Throughput | P50 | P95 | P99 | Error Rate |",
        "|----------|-------------|----------|------------|-----|-----|-----|------------|",
    ]
    for r in results:
        lines.append(
            f"| {r.scenario} | {r.concurrency} | {r.total_requests} | "
            f"{r.throughput:.1f}/s | {r.p(50):.1f} | {r.p(95):.1f} | "
            f"{r.p(99):.1f} | {r.error_rate_pct:.1f}% |",
        )
    return "\n".join(lines)


def write_markdown_report(
    args: argparse.Namespace,
    results: list[BenchResult],
    report_file: str,
    meta_file: str,
    raw_cmd: str,
) -> None:
    parent = os.path.dirname(report_file)
    if parent:
        os.makedirs(parent, exist_ok=True)
    ts_local = datetime.now().astimezone().strftime("%Y-%m-%d %H:%M:%S %Z")
    ts_utc = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
    user_end = args.start_id + args.count - 1
    group_text = (
        f"{args.group_id} ({args.group_members} members)"
        if args.group_id else "N/A"
    )
    content = f"""# Benchmark Report

## 1. Run Snapshot
- Generated at (local): {ts_local}
- Generated at (UTC): {ts_utc}
- Branch: `{_git_branch()}`
- Commit: `{_git_commit()}`
- Scenario: `{args.scenario}`
- Command: `{raw_cmd}`

## 2. Test Setup
- Target: `{args.host}:{args.port}`
- User IDs: `{args.start_id}..{user_end}` (`{args.count}` users)
- Messages per sender: `{args.messages}`
- Send interval: `{args.interval * 1000:.1f} ms`
- Group ID: `{group_text}`

## 3. Results
{_results_table(results)}

## 4. Environment Metadata
- Metadata file: `{meta_file}`
- After benchmark, collect metadata with:
  `./scripts/collect_baseline_meta.sh {meta_file}`

## 5. Incidents (fill manually)
- Crash/abort timestamps:
- Error logs (`free(): invalid pointer`, `Segmentation fault`, `Broken pipe`):
- How often it happened:
- Impact on benchmark result:

## 6. Conclusion (fill manually)
- Current bottlenecks:
- Current stability risks:
- Priority fixes before next benchmark:
"""
    with open(report_file, "w", encoding="utf-8") as f:
        f.write(content)
    print(f"[report] wrote {report_file}")


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #
def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="Benchmark tool for Cluster Chat Server",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    p.add_argument(
        "scenario", choices=["login", "chat", "group", "all"],
        help="login | chat | group | all",
    )
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=8000)
    p.add_argument("--start-id", type=int, required=True,
                   help="first bench user ID")
    p.add_argument("--count", type=int, default=200,
                   help="number of bench users (default 200)")
    p.add_argument("--password", default="bench123")
    p.add_argument("--messages", type=int, default=100,
                   help="messages per sender for chat/group (default 100)")
    p.add_argument("--group-id", type=int, default=0,
                   help="group ID for group / all scenario")
    p.add_argument("--group-members", type=int, default=21,
                   help="number of users in the bench group (default 21)")
    p.add_argument("--interval", type=float, default=0.001,
                   help="seconds between sends per sender (default 0.001)")
    p.add_argument("--run-dir", default="",
                   help="directory for generated report/meta paths")
    p.add_argument("--report-file", default="",
                   help="write markdown report to this file")
    p.add_argument("--meta-file", default="",
                   help="metadata file path shown in report")
    return p


def main() -> None:
    args = build_parser().parse_args()
    uids = list(range(args.start_id, args.start_id + args.count))

    if args.scenario in ("group", "all") and args.group_id <= 0:
        print("error: --group-id is required for group / all scenario",
              file=sys.stderr)
        sys.exit(1)

    print_config(args)

    results: list[BenchResult] = []
    pause = 3  # seconds between scenarios in 'all' mode

    # --- login ---
    if args.scenario in ("login", "all"):
        r = asyncio.run(bench_login(args.host, args.port, uids, args.password))
        r.print_report()
        results.append(r)
        if args.scenario == "all":
            print(f"[all] pausing {pause}s ...")
            time.sleep(pause)

    # --- chat ---
    if args.scenario in ("chat", "all"):
        r = asyncio.run(bench_one_chat(
            args.host, args.port, uids, args.password,
            args.messages, args.interval,
        ))
        r.print_report()
        results.append(r)
        if args.scenario == "all":
            print(f"[all] pausing {pause}s ...")
            time.sleep(pause)

    # --- group ---
    if args.scenario in ("group", "all"):
        gm = min(args.group_members, args.count)
        guids = uids[:gm]
        r = asyncio.run(bench_group_chat(
            args.host, args.port, guids, args.password,
            args.group_id, args.messages, args.interval,
        ))
        r.print_report()
        results.append(r)

    # --- summary ---
    if len(results) > 1:
        print_summary(results)

    raw_cmd = "python3 " + " ".join(shlex.quote(a) for a in sys.argv)
    report_file = args.report_file
    meta_file = args.meta_file
    if args.run_dir:
        os.makedirs(args.run_dir, exist_ok=True)
        if not report_file:
            report_file = os.path.join(args.run_dir, "report.md")
        if not meta_file:
            meta_file = os.path.join(args.run_dir, "meta.md")
    if report_file:
        if not meta_file:
            meta_file = "bench/runs/<run-id>/meta.md"
        write_markdown_report(args, results, report_file, meta_file, raw_cmd)


if __name__ == "__main__":
    main()
