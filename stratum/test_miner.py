#!/usr/bin/env python3
"""CPU test miner that speaks Kadena Stratum v1 to b0rg_stratum.py.

It does not need a physical KA3. It subscribes, authorizes, grinds nonce2
over the 286-byte header with hashlib.blake2s, and submits shares.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import b0rg_pow


def recv_lines(sock: socket.socket, buf: bytearray):
    while b"\n" not in buf:
        chunk = sock.recv(65536)
        if not chunk:
            raise ConnectionError("stratum closed")
        buf += chunk
    while b"\n" in buf:
        line, _, rest = buf.partition(b"\n")
        buf[:] = rest
        line = line.strip()
        if line:
            yield json.loads(line.decode())


def main() -> int:
    parser = argparse.ArgumentParser(description="B0RG Kadena-Stratum CPU test miner")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=4340)
    parser.add_argument("--user", required=True, help="address or address.worker")
    parser.add_argument("--password", default="x")
    parser.add_argument("--agent", default="b0rg-test-miner/1.0")
    parser.add_argument("--blocks", type=int, default=1, help="Stop after this many accepted shares that the pool calls blocks, or after --shares shares")
    parser.add_argument("--shares", type=int, default=0, help="Stop after this many accepted shares (0 = use --blocks)")
    parser.add_argument("--max-hashes", type=int, default=5_000_000)
    parser.add_argument("--roll-time", action="store_true", help="Send a 4th submit param with the original creation time, to exercise the optional time path")
    args = parser.parse_args()

    sock = socket.create_connection((args.host, args.port), timeout=30)
    sock.settimeout(10)
    buf = bytearray()

    def send(obj):
        sock.sendall((json.dumps(obj) + "\n").encode())

    send({"id": 1, "method": "mining.subscribe", "params": [args.agent, None]})
    send({"id": 2, "method": "mining.authorize", "params": [args.user, args.password]})

    nonce1 = None
    nonce2_size = b0rg_pow.NONCE2_SIZE
    target = None
    job_id = None
    work = None
    accepted = 0
    submit_id = 100
    hashes = 0
    deadline = time.time() + 180

    while time.time() < deadline:
        progressed = False
        try:
            for msg in recv_lines(sock, buf):
                progressed = True
                if msg.get("id") == 1 and msg.get("result"):
                    nonce1 = int(msg["result"][1], 16)
                    nonce2_size = int(msg["result"][2])
                    print(f"subscribed nonce1={msg['result'][1]} nonce2_size={nonce2_size}", flush=True)
                elif msg.get("id") == 2:
                    print(f"authorize {msg.get('result')}", flush=True)
                elif msg.get("method") == "mining.set_target":
                    target = b0rg_pow.target_from_be_hex(msg["params"][0])
                    print(f"target set difficulty={b0rg_pow.difficulty_of(target):.6g}", flush=True)
                elif msg.get("method") == "mining.notify":
                    job_id = str(msg["params"][0])
                    work = bytearray.fromhex(msg["params"][1])
                    print(f"job {job_id} header={len(work)} bytes", flush=True)
                elif msg.get("id", 0) >= 100:
                    if msg.get("result") is True:
                        accepted += 1
                        print(f"share accepted ({accepted})", flush=True)
                        goal = args.shares or args.blocks
                        if accepted >= goal:
                            print("done", flush=True)
                            return 0
                    else:
                        print(f"share rejected {msg}", flush=True)
        except socket.timeout:
            pass
        if not progressed and (nonce1 is None or target is None or work is None):
            continue
        if nonce1 is None or target is None or work is None or job_id is None:
            continue

        base = bytearray(work)
        usec = b0rg_pow.read_usec(base)
        found = False
        span = 1 << (8 * nonce2_size)
        for nonce2 in range(span):
            nonce = b0rg_pow.compose_nonce(nonce1, nonce2, 8 - nonce2_size)
            trial = bytearray(base)
            b0rg_pow.write_nonce(trial, nonce)
            digest = b0rg_pow.blake2s256(bytes(trial))
            hashes += 1
            if b0rg_pow.hash_meets(digest, target):
                params = [args.user, job_id, f"{nonce2:0{nonce2_size * 2}x}"]
                if args.roll_time:
                    params.append(f"{usec:016x}")
                submit_id += 1
                send({"id": submit_id, "method": "mining.submit", "params": params})
                print(f"submitted nonce2={params[2]} hashes={hashes}", flush=True)
                found = True
                # One share per job; wait for a fresh notify or the accept.
                work = None
                break
            if hashes >= args.max_hashes:
                print("hit --max-hashes without a share", flush=True)
                return 1
            if hashes % 200000 == 0:
                print(f"hashing {hashes}", flush=True)
        if not found:
            print("exhausted nonce2", flush=True)
            return 1
    print("timed out", flush=True)
    return 1


if __name__ == "__main__":
    sys.exit(main())
