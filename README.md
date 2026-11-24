# Assimilation Coin (`$B0RG`)

Phase 1 is a Litecoin Core 0.21.4 fork with opt-in MWEB privacy and a Kadena-compatible Blake2s-256 proof of work, plus a lightweight Stratum server so real Kadena ASICs can mine it.

One **drone** is one `B0RG`. Amounts have 8 decimal places (`1 B0RG = 100,000,000` drone-satoshis). The ticker uses a zero, not the letter O.

| | |
|---|---|
| Block time | 30 seconds |
| Emission | 1,750 B0RG/block, halving every 2,000,000 spendable blocks |
| Block 1 | 1,759.28 B0RG (the 1,750 plus the 9.28 remainder that makes the integer series exact) |
| Max supply | **7,000,000,009.00000000 B0RG**, all spendable. Genesis pays 0 and is unspendable |
| Coinbase maturity | 100 blocks (~50 minutes) |
| Difficulty | Zawy LWMA-1, 60-block window, from block 61. Solvetimes clamped to `[1, 6×30s]`. The next target cannot be more than 16× harder or 8× easier than the previous block, and cannot be easier than `powLimit` |
| Premine | 0 |

The first 60 mainnet blocks use the easy bootstrap target (`powLimit`, compact `0x1e0ffff0`). A KA3 will find those quickly; LWMA then locks onto real hashrate. That is the fair-launch bootstrap, not a premine.

## Build

Ubuntu 24.04 packages: `build-essential libtool autotools-dev automake pkg-config bsdmainutils libssl-dev libevent-dev libboost-system-dev libboost-filesystem-dev libboost-thread-dev libboost-test-dev libdb5.3++-dev libsqlite3-dev libfmt-dev`. Qt (`qtbase5-dev qttools5-dev`) is only needed for `b0rg-qt`.

```bash
./autogen.sh
./configure --with-incompatible-bdb --without-gui
make -j"$(nproc)"
```

Binaries land in `src/`: `b0rgd`, `b0rg-cli`, `b0rg-tx`, `b0rg-wallet`. Pass `--with-gui=qt5` to also build `src/qt/b0rg-qt`. The approved cube-miner logo is `share/pixmaps/b0rg-logo.jpg` and is wired as the Qt icon and splash.

## Run a node

Default data directory is `~/.b0rg`.

| Network | P2P | RPC | Bech32 | MWEB |
|---|---|---|---|---|
| main | 19333 | 19332 | `borg` | `borgmweb` |
| test | 29333 | 29332 | `tborg` | `tborgmweb` |
| regtest | 19433 | 19432 | `rborg` | `rborgmweb` |

Mainnet transparent addresses start with `B` (P2PKH, version 25) or `D` (P2SH, version 30). On Windows the data directory is `%APPDATA%\B0RG`.

The desktop wallet shows amounts as **DRONES** (1 DRONES = 1 B0RG), **mDRONES**, **µDRONES**, and **zaps** (1 zap = 1 satoshi). That labeling is display-only.

To sync a Windows wallet from the pool node, add one peer (the default P2P port is already 19333):

```
addnode=pool.shed-p00l.com
```

or, over Tailscale:

```
addnode=100.101.120.28
```

Put that in `%APPDATA%\B0RG\b0rg.conf`, or start with `b0rg-qt.exe -addnode=pool.shed-p00l.com`.

```bash
src/b0rgd -daemon
src/b0rg-cli createwallet "shed"
src/b0rg-cli -rpcwallet=shed getnewaddress
src/b0rg-cli -rpcwallet=shed getnewaddress "private" mweb
```

SegWit and MWEB are active from height 1 on every network. Taproot is active immediately.

### MWEB (opt-in privacy)

MWEB addresses look like `borgmweb1...` on mainnet. Sending to one pegs transparent drones in. Sending from an MWEB balance to another MWEB address stays inside the extension block. Sending from MWEB to a transparent `borg1...` / `B...` address pegs out.

```bash
WALLET="-rpcwallet=shed"
TRANSPARENT=$(src/b0rg-cli $WALLET getnewaddress "spend" bech32)
MWEB=$(src/b0rg-cli $WALLET getnewaddress "vault" mweb)
# After the transparent coinbase has 100 confirmations:
src/b0rg-cli $WALLET sendtoaddress "$MWEB" 10          # peg-in
src/b0rg-cli generatetoaddress 1 "$TRANSPARENT"       # or wait for a mined block
OTHER=$(src/b0rg-cli $WALLET getnewaddress "peer" mweb)
src/b0rg-cli $WALLET sendtoaddress "$OTHER" 1          # MWEB -> MWEB
src/b0rg-cli $WALLET sendtoaddress "$TRANSPARENT" 0.5  # peg-out
```

On a fresh regtest node, add `-fallbackfee=0.0002` so `sendtoaddress` does not wait for fee estimates.

## Proof of work

`GetHash()` is SHA256d of the 88-byte header (version, prev, merkle, time, bits, **uint64 nonce**, time-pad). `GetPoWHash()` rebuilds a **286-byte Kadena Chainweb work header** and hashes it once with unkeyed BLAKE2s-256 (RFC 7693, 32-byte digest). The digest is compared as a little-endian 256-bit integer against the target in `nBits`.

| Offset | Bytes | Contents |
|---|---|---|
| 0 | 8 | flags, zero |
| 8 | 8 | `nTime * 1000000 + nTimePad`, little-endian microseconds |
| 16 | 32 | `hashPrevBlock` |
| 48 | 110 | adjacent-parent record, length 0 (no side chains) |
| 158 | 32 | expanded `nBits` target, little-endian |
| 190 | 32 | `hashMerkleRoot` |
| 222 | 4 | chain id 0 |
| 226 | 32 | weight 0 |
| 258 | 8 | height 0 (height is not stored in the header) |
| 266 | 4 | `nVersion` |
| 270 | 8 | epoch 0 |
| 278 | 8 | `nNonce`, little-endian |

`nTimePad` is microseconds within the second, `0..999999`, so a miner that updates the 8-byte creation-time field can be represented exactly. Values `>= 1000000` are invalid.

Genesis message, in the coinbase scriptSig:

> Nov 24 2026 — Mint your drone army. Assimilate cyberspace. Resistance is futile.

## Stratum

`stratum/b0rg_stratum.py` is Python 3 standard library plus `hashlib.blake2s`. It polls `getminingwork` (the node assembles the SegWit commitment and the MWEB HogEx) and speaks Kadena Stratum v1:

```text
mining.subscribe  -> [null, "<2-byte nonce1 hex>", 6]
mining.authorize  -> true     (username is address or address.worker)
mining.set_target -> ["<32-byte big-endian share target>"]
mining.notify     -> [jobId, "<572 hex chars = 286-byte header>", true]
mining.submit     -> [address.worker, jobId, nonce2_hex]
```

`nonce1` is the high 2 bytes of the uint64 nonce, `nonce2` is the low 6 bytes the ASIC grinds. Both are big-endian hex on the wire and are written little-endian at bytes 278..285. This is the split used by `chainweb-mining-client`, f2pool and poolflare, which is what KA3 / KD / BM-K firmware implements.

The **network target lives inside the header at bytes 158..189** and is part of the hash. `mining.set_target` is a separate, easier-or-equal share threshold. Stock firmware compares against `mining.set_target` and does **not** roll creation time; a submit is only `[worker, jobId, nonce2]`. Leaving the time bytes unchanged is valid. If a miner does roll time and reports it, a 4th parameter of 16 hex characters (big-endian microseconds) or the full 572-hex header is accepted. Any other mutated header byte is rejected.

```bash
src/b0rgd -daemon -rpcuser=b0rg -rpcpassword=b0rg
python3 stratum/b0rg_stratum.py \
  --rpc-url http://127.0.0.1:19332 \
  --rpc-user b0rg --rpc-password b0rg \
  --fee-address 'B...' --fee-percent 1
```

| Port | Mode |
|---|---|
| 3340 | PROP. Worker address receives 99%, `--fee-address` receives 1% |
| 4340 | SOLO. Worker address receives 100% |
| 19340 | JSON stats (`/` or any path) for a ShedMiners dashboard. Also written to `stratum/stats.json` |

`--solo-only` / `--prop-only` listen on one port. `--share-bits 40` is the initial share target (about 2^40 work) until vardiff aims for one share every 15 seconds. The share target is never harder than the network target, so a block solution is always submitted. On regtest the network target is already trivial and overrides `--share-bits`.

CPU check without hardware:

```bash
python3 stratum/test_miner.py --port 4340 --user 'borg1....worker' --blocks 1
```

### ASIC settings

| | |
|---|---|
| Algorithm | Blake2s (Kadena). Not Blake2b, not Scrypt, not SHA-256 |
| URL | `stratum+tcp://YOUR_HOST:3340` (PROP) or `:4340` (SOLO) |
| Worker | your `B...` or `borg1...` address, optional `.workername` |
| Password | anything (`x` is fine; it is ignored) |
| Firmware | Antminer KA3, Goldshell KD6 / KD5 / KD-Box, iBeLink BM-K1 / BM-K3, factory Kadena firmware |

Point the miner at this pool the same way you would point it at a Kadena pool. Do not enable a "Blake2b" or "Kadena-C32" profile if the firmware offers one; those are different 32-byte-nonce algorithms.

## Tests

```bash
src/test/test_litecoin --run_test=b0rg_tests
```

`b0rg_tests` checks the exact 7,000,000,009 supply, that the 286-byte header matches `hashlib.blake2s(..., digest_size=32)`, and that LWMA holds, tightens (clamped to 16×), and loosens (6× on a fully slow window) the way the comments in `src/pow.cpp` describe.

`stratum/integration_test.py` starts a regtest node, mines 101 blocks through Stratum, then pegs in, sends MWEB to MWEB, and pegs out.
