#!/usr/bin/env python3
"""Regtest end-to-end: stratum mines blocks, then an MWEB peg-in / send / peg-out."""

from __future__ import annotations

import json
import os
import secrets
import shutil
import socket
import subprocess
import sys
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def rpc(url, user, password, method, params):
    import base64
    body = json.dumps({"jsonrpc": "1.0", "id": "it", "method": method, "params": params}).encode()
    req = urllib.request.Request(url, data=body, headers={
        "Content-Type": "application/json",
        "Authorization": "Basic " + base64.b64encode(f"{user}:{password}".encode()).decode(),
    })
    with urllib.request.urlopen(req, timeout=60) as resp:
        payload = json.loads(resp.read().decode())
    if payload.get("error"):
        raise RuntimeError(f"{method} {params}: {payload['error']}")
    return payload["result"]


def wait_port(host, port, timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with socket.create_connection((host, port), timeout=1):
                return
        except OSError:
            time.sleep(0.2)
    raise TimeoutError(f"{host}:{port} did not open")


def main() -> int:
    bin_dir = os.environ.get("B0RG_BIN", os.path.join(ROOT, "src"))
    b0rgd = os.path.join(bin_dir, "b0rgd")
    cli = os.path.join(bin_dir, "b0rg-cli")
    datadir = os.environ.get("B0RG_DATADIR", "/tmp/b0rg-regtest")
    if os.path.exists(datadir):
        shutil.rmtree(datadir)
    os.makedirs(datadir)
    user, password = "b0rg", secrets.token_hex(16)
    url = "http://127.0.0.1:19432"
    log = open("/tmp/b0rg-regtest.log", "w")
    node = None
    pool = None
    node = subprocess.Popen([
        b0rgd, "-regtest", f"-datadir={datadir}",
        f"-rpcuser={user}", f"-rpcpassword={password}",
        "-rpcbind=127.0.0.1", "-rpcallowip=127.0.0.1",
        "-listen=0", "-fallbackfee=0.0002",
        "-printtoconsole",
    ], stdout=log, stderr=subprocess.STDOUT)
    try:
        deadline = time.time() + 40
        while time.time() < deadline:
            try:
                rpc(url, user, password, "getblockcount", [])
                break
            except Exception:
                if node.poll() is not None:
                    raise RuntimeError("b0rgd exited early; see /tmp/b0rg-regtest.log")
                time.sleep(0.3)
        else:
            raise TimeoutError("rpc did not come up")

        rpc(url, user, password, "createwallet", ["test"])
        addr = rpc(url, user, password, "getnewaddress", ["miner", "bech32"])
        print("miner address", addr, flush=True)

        pool_log = open("/tmp/b0rg-stratum.log", "w")
        pool = subprocess.Popen([
            sys.executable, os.path.join(ROOT, "stratum", "b0rg_stratum.py"),
            "--rpc-url", url, "--rpc-user", user, "--rpc-password", password,
            "--solo-only", "--solo-port", "4340", "--bind", "127.0.0.1",
            "--share-bits", "8", "--poll", "0.25",
            "--stats-file", "/tmp/b0rg-stats.json", "--stats-port", "19340",
            "--fee-percent", "0",
        ], stdout=pool_log, stderr=subprocess.STDOUT, cwd=ROOT)
        wait_port("127.0.0.1", 4340, 20)

        miner = subprocess.run([
            sys.executable, os.path.join(ROOT, "stratum", "test_miner.py"),
            "--host", "127.0.0.1", "--port", "4340",
            "--user", addr + ".cpu",
            "--blocks", "101",
            "--max-hashes", "100000",
        ], cwd=ROOT, timeout=300)
        if miner.returncode != 0:
            raise RuntimeError(f"test miner exit {miner.returncode}")

        height = rpc(url, user, password, "getblockcount", [])
        print("height", height, flush=True)
        if height < 101:
            raise RuntimeError(f"expected at least 101 blocks, got {height}")
        bal = rpc(url, user, password, "getbalances", [])
        trusted = float(bal["mine"]["trusted"])
        immature = float(bal["mine"]["immature"])
        print("balances", bal["mine"], flush=True)
        if trusted <= 0:
            raise RuntimeError("coinbase was not credited to the miner address")

        mweb = rpc(url, user, password, "getnewaddress", ["peg", "mweb"])
        mweb2 = rpc(url, user, password, "getnewaddress", ["peer", "mweb"])
        print("mweb", mweb, flush=True)
        print("mweb2", mweb2, flush=True)
        if "mweb" not in mweb:
            raise RuntimeError(f"expected an mweb address, got {mweb}")
        peg_in = rpc(url, user, password, "sendtoaddress", [mweb, 10])
        print("peg-in tx", peg_in, flush=True)
        rpc(url, user, password, "generatetoaddress", [1, addr])
        info = rpc(url, user, password, "getaddressinfo", [mweb])
        print("mweb info", {k: info.get(k) for k in ("address", "ismine", "iswitness")}, flush=True)
        received = float(rpc(url, user, password, "getreceivedbyaddress", [mweb, 0]))
        print("mweb received", received, flush=True)
        if received < 10:
            # Some wallet builds report MWEB via listunspent instead.
            unspent = rpc(url, user, password, "listunspent", [0, 9999999, [mweb]])
            print("mweb unspent", unspent, flush=True)
            if not unspent:
                raise RuntimeError("peg-in did not credit the MWEB address")
        peg_send = rpc(url, user, password, "sendtoaddress", [mweb2, 1])
        print("mweb->mweb tx", peg_send, flush=True)
        rpc(url, user, password, "generatetoaddress", [1, addr])
        received2 = float(rpc(url, user, password, "getreceivedbyaddress", [mweb2, 0]))
        print("mweb2 received", received2, flush=True)
        transparent = rpc(url, user, password, "getnewaddress", ["out", "bech32"])
        before = float(rpc(url, user, password, "getreceivedbyaddress", [transparent, 0]))
        peg_out = rpc(url, user, password, "sendtoaddress", [transparent, "0.5"])
        print("peg-out tx", peg_out, flush=True)
        rpc(url, user, password, "generatetoaddress", [1, addr])
        after = float(rpc(url, user, password, "getreceivedbyaddress", [transparent, 0]))
        print("transparent before/after", before, after, flush=True)
        if after < before + 0.5:
            raise RuntimeError("peg-out did not credit the transparent address")
        final = rpc(url, user, password, "getbalances", [])
        print("final", final["mine"], flush=True)
        print("INTEGRATION_OK", flush=True)
        print("trusted_after_stratum", trusted, "immature", immature, flush=True)
        return 0
    finally:
        try:
            if pool is not None:
                pool.terminate()
        except Exception:
            pass
        try:
            rpc(url, user, password, "stop", [])
        except Exception:
            node.terminate()
        try:
            node.wait(timeout=20)
        except Exception:
            node.kill()


if __name__ == "__main__":
    sys.exit(main())
