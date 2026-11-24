#!/usr/bin/env python3
"""Kadena-compatible Stratum server for Assimilation Coin ($B0RG).

Speaks the chainweb-mining-client Stratum subset used by Antminer KA3,
Goldshell KD5/KD6/KD-Box and iBeLink BM-K1/K3:

    subscribe -> [null, nonce1, nonce2_size]
    authorize  -> true
    set_target -> 32-byte big-endian share target
    notify     -> [jobId, 286-byte header hex, cleanJobs]
    submit     -> [worker, jobId, nonce2]  (optional time or full header)

PROP (default port 3340) pays (100 - fee)% to the worker address and fee%
to --fee-address. SOLO (default port 4340) pays the worker address in full.

The node builds the block (SegWit commitment and MWEB HogEx included) via
the getminingwork RPC. This process only grinds-checks Blake2s and submits.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import b0rg_pow

# How often a worker should find a share, once vardiff has a sample.
SHARE_SECONDS = 15.0


class RpcError(RuntimeError):
    pass


class NodeRpc:
    def __init__(self, url: str, user: str, password: str, timeout: float = 30.0):
        self.url = url
        self.user = user
        self.password = password
        self.timeout = timeout
        self._id = 0
        self._lock = threading.Lock()

    def call(self, method: str, params: list):
        with self._lock:
            self._id += 1
            payload = json.dumps({"jsonrpc": "1.0", "id": self._id, "method": method, "params": params}).encode()
            req = urllib.request.Request(self.url, data=payload, headers={"Content-Type": "application/json"})
            if self.user is not None:
                token = f"{self.user}:{self.password}".encode()
                import base64
                req.add_header("Authorization", "Basic " + base64.b64encode(token).decode())
            try:
                with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                    body = json.loads(resp.read().decode())
            except urllib.error.HTTPError as exc:
                detail = exc.read().decode(errors="replace")
                raise RpcError(f"{method} HTTP {exc.code}: {detail}") from exc
            except urllib.error.URLError as exc:
                raise RpcError(f"{method} failed: {exc}") from exc
        if body.get("error"):
            raise RpcError(f"{method}: {body['error']}")
        return body.get("result")


class Job:
    def __init__(self, job_id: str, work: bytes, block_hex: str, network_target: int, height: int, ntime: int):
        self.job_id = job_id
        self.work = work
        self.block_hex = block_hex
        self.network_target = network_target
        self.height = height
        self.ntime = ntime
        self.created = time.time()


class WorkerStats:
    def __init__(self):
        self.accepted = 0
        self.rejected = 0
        self.blocks = 0
        self.last_share = 0.0
        self.share_target = 0
        self.hashrate = 0.0
        self.online = False
        self.address = ""
        self.mode = ""


class Session:
    def __init__(self, conn: socket.socket, addr, mode: str, nonce1: int):
        self.conn = conn
        self.addr = addr
        self.mode = mode
        self.nonce1 = nonce1
        self.nonce1_hex = f"{nonce1:04x}"
        self.agent = ""
        self.username = ""
        self.worker = ""
        self.address = ""
        self.subscribed = False
        self.authorized = False
        self.share_target = 1
        self.last_share_at = 0.0
        self.stats = WorkerStats()
        self.lock = threading.Lock()
        self.buf = b""

    def send(self, obj: dict) -> None:
        line = (json.dumps(obj, separators=(",", ":")) + "\n").encode()
        with self.lock:
            self.conn.sendall(line)

    def key(self) -> str:
        if self.worker:
            return f"{self.address}.{self.worker}"
        return self.address or self.nonce1_hex


class Pool:
    def __init__(self, args):
        self.args = args
        self.rpc = NodeRpc(args.rpc_url, args.rpc_user, args.rpc_password)
        self.jobs: dict[str, Job] = {}
        self.jobs_lock = threading.Lock()
        self.current: Job | None = None
        self.job_counter = 0
        self.sessions: list[Session] = []
        self.sessions_lock = threading.Lock()
        self.nonce1_next = 1
        self.blocks_found = 0
        self.shares_ok = 0
        self.shares_bad = 0
        self.start_time = time.time()
        self.base_share_target = (1 << (256 - args.share_bits)) - 1

    def alloc_nonce1(self) -> int:
        with self.sessions_lock:
            n = self.nonce1_next
            self.nonce1_next = (self.nonce1_next + 1) & 0xFFFF
            if self.nonce1_next == 0:
                self.nonce1_next = 1
            return n

    def refresh_job(self) -> None:
        # One template for the poller. Per-session coinbase is rebuilt when a
        # session is notified so each worker is paid directly.
        with self.sessions_lock:
            sessions = [s for s in self.sessions if s.subscribed and s.authorized and s.address]
        for session in sessions:
            self._push_job(session, clean=True)

    def _push_job(self, session: Session, clean: bool) -> None:
        try:
            if session.mode == "prop" and self.args.fee_percent > 0:
                result = self.rpc.call("getminingwork", [session.address, self.args.fee_percent, self.args.fee_address])
            else:
                result = self.rpc.call("getminingwork", [session.address, 0, ""])
        except RpcError as exc:
            print(f"getminingwork failed for {session.key()}: {exc}", flush=True)
            return
        work = bytes.fromhex(result["workhex"])
        if len(work) != b0rg_pow.WORK_SIZE:
            print(f"node returned {len(work)}-byte work, expected {b0rg_pow.WORK_SIZE}", flush=True)
            return
        network_target = b0rg_pow.target_from_be_hex(result["target"])
        with self.jobs_lock:
            self.job_counter += 1
            job_id = f"{self.job_counter:x}"
            job = Job(job_id, work, result["blockhex"], network_target, int(result["height"]), int(result["time"]))
            self.jobs[job_id] = job
            self.current = job
            # Keep a small history so a share for the previous template still resolves.
            if len(self.jobs) > 32:
                for old in sorted(self.jobs)[: len(self.jobs) - 32]:
                    # job ids are hex; sort lexicographically is not numeric. Drop oldest by counter embedded? 
                    pass
                # Drop by creation time instead.
                ordered = sorted(self.jobs.values(), key=lambda j: j.created)
                for stale in ordered[:-32]:
                    self.jobs.pop(stale.job_id, None)
        share_target = max(network_target, min(session.share_target or self.base_share_target, (1 << 256) - 1))
        # Never harder than the network, so a valid block is always submitted.
        if share_target < network_target:
            share_target = network_target
        session.share_target = share_target
        session.stats.share_target = share_target
        session.send({"id": None, "method": "mining.set_target", "params": [b0rg_pow.target_to_be_hex(share_target)]})
        session.send({"id": None, "method": "mining.notify", "params": [job_id, work.hex(), bool(clean)]})

    def poll_loop(self) -> None:
        last_height = -1
        while True:
            try:
                tip = self.rpc.call("getblockcount", [])
                height = int(tip)
                with self.sessions_lock:
                    active = any(s.subscribed and s.authorized for s in self.sessions)
                if active and (height != last_height or self.current is None):
                    last_height = height
                    self.refresh_job()
            except RpcError as exc:
                print(f"node poll: {exc}", flush=True)
            time.sleep(self.args.poll)

    def handle_submit(self, session: Session, msg_id, params: list) -> None:
        if len(params) < 3:
            session.send({"id": msg_id, "result": False, "error": [21, "bad submit", None]})
            self.shares_bad += 1
            session.stats.rejected += 1
            return
        worker_field = str(params[0])
        job_id = str(params[1])
        try:
            nonce2 = int(str(params[2]), 16)
        except ValueError:
            session.send({"id": msg_id, "result": False, "error": [21, "bad nonce2", None]})
            self.shares_bad += 1
            session.stats.rejected += 1
            return
        if nonce2 >= 1 << (8 * b0rg_pow.NONCE2_SIZE):
            session.send({"id": msg_id, "result": False, "error": [21, "nonce2 too wide", None]})
            self.shares_bad += 1
            session.stats.rejected += 1
            return
        with self.jobs_lock:
            job = self.jobs.get(job_id)
        if job is None:
            session.send({"id": msg_id, "result": False, "error": [21, "stale job", None]})
            self.shares_bad += 1
            session.stats.rejected += 1
            return

        usec_override = None
        work = bytearray(job.work)
        nonce = b0rg_pow.compose_nonce(session.nonce1, nonce2)
        if len(params) >= 4 and params[3] is not None:
            extra = str(params[3]).strip().lower()
            if len(extra) == 16:
                usec_override = int(extra, 16)
            elif len(extra) == b0rg_pow.WORK_SIZE * 2:
                submitted = bytes.fromhex(extra)
                # Only creation time and nonce may move. Everything else is the job.
                for i, (a, b) in enumerate(zip(submitted, job.work)):
                    if a == b:
                        continue
                    if 8 <= i < 16 or 278 <= i < 286:
                        continue
                    session.send({"id": msg_id, "result": False, "error": [21, "header mutated", None]})
                    self.shares_bad += 1
                    session.stats.rejected += 1
                    return
                usec_override = b0rg_pow.read_usec(submitted)
                header_nonce = b0rg_pow.read_nonce(submitted)
                if header_nonce != nonce:
                    session.send({"id": msg_id, "result": False, "error": [21, "nonce mismatch", None]})
                    self.shares_bad += 1
                    session.stats.rejected += 1
                    return
            elif extra:
                session.send({"id": msg_id, "result": False, "error": [21, "bad extra", None]})
                self.shares_bad += 1
                session.stats.rejected += 1
                return

        b0rg_pow.write_nonce(work, nonce)
        ntime = None
        ntime_pad = None
        if usec_override is not None:
            ntime, ntime_pad = b0rg_pow.split_usec(usec_override)
            b0rg_pow.write_usec(work, usec_override)

        digest = b0rg_pow.blake2s256(bytes(work))
        share_target = session.share_target or self.base_share_target
        # A hash that solves the block is a share even if vardiff moved.
        accept_target = max(share_target, job.network_target)
        if not b0rg_pow.hash_meets(digest, accept_target):
            session.send({"id": msg_id, "result": False, "error": [23, "low difficulty", None]})
            self.shares_bad += 1
            session.stats.rejected += 1
            return

        now = time.time()
        elapsed = now - session.last_share_at if session.last_share_at else SHARE_SECONDS
        session.last_share_at = now
        if elapsed > 0 and share_target > 0:
            # Bitcoin-style: hashes/sec ≈ difficulty * 2^32 / seconds per share.
            session.stats.hashrate = (b0rg_pow.DIFF1 / share_target) * (2**32) / max(elapsed, 0.05)
        session.stats.accepted += 1
        session.stats.last_share = now
        session.stats.online = True
        session.stats.address = session.address
        session.stats.mode = session.mode
        self.shares_ok += 1

        # Vardiff toward SHARE_SECONDS. Never harder than the network target.
        if elapsed > 0:
            scaled = int(share_target * (elapsed / SHARE_SECONDS))
            if scaled < 1:
                scaled = 1
            if scaled < job.network_target:
                scaled = job.network_target
            if scaled > (1 << 256) - 1:
                scaled = (1 << 256) - 1
            # Ignore tiny moves so ASICs are not constantly retargeted.
            if scaled > share_target * 1.25 or scaled < share_target * 0.8:
                session.share_target = scaled
                session.send({"id": None, "method": "mining.set_target", "params": [b0rg_pow.target_to_be_hex(scaled)]})

        solved = b0rg_pow.hash_meets(digest, job.network_target)
        if solved:
            try:
                block_hex = b0rg_pow.patch_block_hex(job.block_hex, nonce, ntime, ntime_pad)
                result = self.rpc.call("submitblock", [block_hex])
            except RpcError as exc:
                print(f"submitblock error: {exc}", flush=True)
                session.send({"id": msg_id, "result": False, "error": [20, "submitblock failed", None]})
                self.shares_bad += 1
                return
            if result not in (None, "", "duplicate", "duplicate-inconclusive"):
                print(f"block rejected: {result}", flush=True)
                session.send({"id": msg_id, "result": False, "error": [23, str(result), None]})
                self.shares_bad += 1
                session.stats.rejected += 1
                session.stats.accepted -= 1
                return
            self.blocks_found += 1
            session.stats.blocks += 1
            print(f"block {job.height} by {session.key()} nonce={nonce}", flush=True)
            try:
                self.refresh_job()
            except RpcError as exc:
                print(f"refresh after block failed: {exc}", flush=True)
        session.send({"id": msg_id, "result": True, "error": None})
        _ = worker_field

    def serve_client(self, conn: socket.socket, addr, mode: str) -> None:
        session = Session(conn, addr, mode, self.alloc_nonce1())
        with self.sessions_lock:
            self.sessions.append(session)
        conn.settimeout(300)
        try:
            while True:
                try:
                    chunk = conn.recv(65536)
                except socket.timeout:
                    continue
                if not chunk:
                    break
                session.buf += chunk
                while b"\n" in session.buf:
                    line, session.buf = session.buf.split(b"\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        msg = json.loads(line.decode())
                    except json.JSONDecodeError:
                        continue
                    self.dispatch(session, msg)
        except (ConnectionError, OSError):
            pass
        finally:
            session.stats.online = False
            with self.sessions_lock:
                if session in self.sessions:
                    self.sessions.remove(session)
            try:
                conn.close()
            except OSError:
                pass

    def dispatch(self, session: Session, msg: dict) -> None:
        method = msg.get("method")
        msg_id = msg.get("id")
        params = msg.get("params") or []
        if method == "mining.subscribe":
            agent = "unknown"
            if params:
                agent = str(params[0])
            session.agent = agent
            session.subscribed = True
            session.send({"id": msg_id, "result": [None, session.nonce1_hex, b0rg_pow.NONCE2_SIZE], "error": None})
            if session.authorized:
                self._push_job(session, clean=True)
            return
        if method == "mining.authorize":
            username = str(params[0]) if params else ""
            if "." in username:
                address, worker = username.split(".", 1)
            else:
                address, worker = username, ""
            if not address:
                session.send({"id": msg_id, "result": False, "error": [1, "address required", None]})
                return
            session.username = username
            session.address = address
            session.worker = worker
            session.authorized = True
            session.stats.address = address
            session.stats.mode = session.mode
            session.stats.online = True
            session.send({"id": msg_id, "result": True, "error": None})
            if session.subscribed:
                self._push_job(session, clean=True)
            return
        if method == "mining.submit":
            if not session.authorized:
                session.send({"id": msg_id, "result": False, "error": [24, "unauthorized", None]})
                return
            self.handle_submit(session, msg_id, params)
            return
        if method == "mining.extranonce.subscribe":
            session.send({"id": msg_id, "result": True, "error": None})
            return
        if msg_id is not None:
            session.send({"id": msg_id, "result": None, "error": [-1, f"unknown method {method}", None]})

    def listen(self, port: int, mode: str) -> None:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((self.args.bind, port))
        sock.listen(64)
        print(f"{mode} stratum on {self.args.bind}:{port}", flush=True)
        while True:
            conn, addr = sock.accept()
            threading.Thread(target=self.serve_client, args=(conn, addr, mode), daemon=True).start()

    def snapshot(self) -> dict:
        with self.sessions_lock:
            workers = {}
            online = 0
            for s in self.sessions:
                if not s.authorized:
                    continue
                st = s.stats
                if st.online:
                    online += 1
                workers[s.key()] = {
                    "address": s.address,
                    "worker": s.worker,
                    "mode": s.mode,
                    "agent": s.agent,
                    "online": st.online,
                    "accepted": st.accepted,
                    "rejected": st.rejected,
                    "blocks": st.blocks,
                    "hashrate": st.hashrate,
                    "share_difficulty": b0rg_pow.difficulty_of(s.share_target) if s.share_target else 0,
                    "last_share": st.last_share,
                }
        net_diff = 0.0
        height = 0
        if self.current is not None:
            net_diff = b0rg_pow.difficulty_of(self.current.network_target)
            height = self.current.height
        return {
            "coin": "B0RG",
            "ticker": "B0RG",
            "name": "Assimilation Coin",
            "algorithm": "blake2s-256",
            "protocol": "kadena-stratum-v1",
            "block_time": 30,
            "fee_percent": self.args.fee_percent,
            "fee_address": self.args.fee_address,
            "height": height,
            "network_difficulty": net_diff,
            "workers_online": online,
            "blocks_found": self.blocks_found,
            "shares": {"accepted": self.shares_ok, "rejected": self.shares_bad},
            "workers": workers,
            "ports": {"prop": self.args.prop_port, "solo": self.args.solo_port},
            "updated": time.time(),
            "uptime": time.time() - self.start_time,
        }

    def write_stats(self) -> None:
        snap = self.snapshot()
        path = self.args.stats_file
        tmp = path + ".tmp"
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(snap, fh, indent=2)
            fh.write("\n")
        os.replace(tmp, path)

    def stats_loop(self) -> None:
        while True:
            try:
                self.write_stats()
            except OSError as exc:
                print(f"stats write failed: {exc}", flush=True)
            time.sleep(5)


def load_cookie(path: str) -> tuple[str, str]:
    with open(path, "r", encoding="utf-8") as fh:
        user, password = fh.read().strip().split(":", 1)
    return user, password


def main() -> None:
    parser = argparse.ArgumentParser(description="B0RG Kadena-compatible stratum")
    parser.add_argument("--rpc-url", default="http://127.0.0.1:19432")
    parser.add_argument("--rpc-user", default="")
    parser.add_argument("--rpc-password", default="")
    parser.add_argument("--rpc-cookie", default="", help="b0rgd .cookie file; used when --rpc-user is empty")
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--prop-port", type=int, default=3340)
    parser.add_argument("--solo-port", type=int, default=4340)
    parser.add_argument("--fee-percent", type=int, default=1)
    parser.add_argument("--fee-address", default="")
    parser.add_argument("--share-bits", type=int, default=40, help="Initial share target difficulty in leading bits. Regtest overrides this when the network target is easier.")
    parser.add_argument("--poll", type=float, default=1.0)
    parser.add_argument("--stats-file", default="stratum/stats.json")
    parser.add_argument("--stats-port", type=int, default=19340)
    parser.add_argument("--prop-only", action="store_true")
    parser.add_argument("--solo-only", action="store_true")
    args = parser.parse_args()
    if not args.rpc_user:
        if not args.rpc_cookie:
            parser.error("pass --rpc-user/--rpc-password or --rpc-cookie")
        args.rpc_user, args.rpc_password = load_cookie(args.rpc_cookie)
    if args.fee_percent < 0 or args.fee_percent > 99:
        parser.error("--fee-percent must be 0..99")
    if not args.solo_only and args.fee_percent > 0 and not args.fee_address:
        parser.error("PROP mode with a fee requires --fee-address")

    pool = Pool(args)

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):  # noqa: N802
            body = json.dumps(pool.snapshot()).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, fmt, *log_args):
            return

    httpd = ThreadingHTTPServer((args.bind, args.stats_port), Handler)
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    print(f"stats http://{args.bind}:{args.stats_port}/ and {args.stats_file}", flush=True)
    threading.Thread(target=pool.stats_loop, daemon=True).start()
    threading.Thread(target=pool.poll_loop, daemon=True).start()
    threads = []
    if not args.solo_only:
        threads.append(threading.Thread(target=pool.listen, args=(args.prop_port, "prop"), daemon=True))
    if not args.prop_only:
        threads.append(threading.Thread(target=pool.listen, args=(args.solo_port, "solo"), daemon=True))
    for t in threads:
        t.start()
    print("stratum ready", flush=True)
    while True:
        time.sleep(3600)


if __name__ == "__main__":
    main()
