# Assimilation Coin (B0RG)

<p align="center">
  <img src="share/pixmaps/b0rg-logo.jpg" alt="B0RG" width="140">
</p>

**White paper.** Software version 1.0.0. This document describes the consensus rules and the mining interface implemented in this repository. It is not an offer to sell a token, and it promises no listing, liquidity, or return.

Website: [https://shed-p00l.com](https://shed-p00l.com)

The node is a fork of [Litecoin Core v0.21.4](https://github.com/litecoin-project/litecoin). Litecoin Core is itself a fork of Bitcoin Core. Copyright notices for those projects are kept in [COPYING](COPYING). Consensus numbers below are the constants and checks in `src/`, and the emission total is the one locked by `src/test/b0rg_tests.cpp`.

## Abstract

Assimilation Coin (ticker **B0RG**, a zero, not the letter O) is a single-chain proof-of-work network. The work function is unkeyed BLAKE2s-256 over a 286-byte header laid out the way Kadena Chainweb miners already hash, so Blake2s ASICs built for Kadena can point at this chain without a new algorithm. The block header stored on the chain is an 88-byte Bitcoin-style header with a 64-bit nonce. Block identity remains SHA256d of that 88-byte header. Only proof of work uses BLAKE2s.

Blocks are targeted at 30 seconds. Spendable supply is exactly **7,000,000,009** B0RG. Genesis pays nothing and is not spendable. There is no premine. The subsidy starts at 1,750 B0RG per block and halves, by an integer right shift, every 2,000,000 blocks. Block 1 pays 1,759.28 B0RG so the series lands on that supply exactly. Coinbase outputs mature after 100 blocks. Difficulty is Zawy LWMA-1 over a 60-block window, first applied at height 61.

Transfers are transparent by default. Mimblewimble Extension Blocks (MWEB), SegWit, and Taproot are active from the first block after genesis. MWEB is opt-in: coins move into the extension block only when a wallet sends to an MWEB address, and they move back when a wallet pegs out.

One B0RG is called one **drone** in the desktop wallet. The chain and the RPC still count amounts in drone-satoshis, of which there are 100,000,000 in a drone.

## 1. Motivation

Kadena's proof of work is BLAKE2s-256 of a fixed 286-byte header. Machines such as the Antminer KA3, Goldshell KD5, KD6, and KD-Box, and the iBeLink BM-K1 and BM-K3, already speak that header through Stratum. When that hashrate is idle it has nowhere to go that uses the same firmware. B0RG gives those machines a chain whose work header is the one they already grind. This is one chain, not a braided Chainweb: unused Chainweb fields (adjacent parents, chain id, weight, height, epoch) are zero. The ASIC still hashes 286 bytes and writes an 8-byte nonce at bytes 278 through 285.

The implementation stays on the Litecoin Core v0.21.4 codebase so the JSON-RPC, the wallet, the script interpreter, SegWit, Taproot, and MWEB are the interfaces operators of that software already run. That is a compatibility choice. It is not a claim that any exchange has listed B0RG, and this paper does not ask for a listing.

## 2. Units and supply

| | |
|---|---|
| Ticker | B0RG |
| Wallet name for 1 B0RG | 1 drone |
| Base unit | 1 zap = 1 satoshi = 10⁻⁸ B0RG |
| `COIN` | 100,000,000 zaps |
| Wallet display units | DRONES, mDRONES, µDRONES, zaps |
| Maximum spendable supply | 7,000,000,009.00000000 B0RG |
| Premine | 0 |
| Genesis subsidy | 0 |

`MAX_MONEY` equals that maximum supply. An amount outside `0 .. MAX_MONEY` is not valid money. The names DRONES, mDRONES, µDRONES, and zaps are Qt display strings. They do not change consensus, address bytes, or RPC field names.

`GetBlockSubsidy` in `src/validation.cpp`:

- Height 0 returns 0.
- For height `h >= 1`, the era is `(h - 1) / 2,000,000`.
- The subsidy is `1,750 * COIN` shifted right by that many bits.
- If the shift count is 64 or more, or the shifted value is 0, the subsidy is 0.
- Height 1 adds `928,000,000` zaps (9.28 B0RG) on top of the base subsidy, so block 1 pays **1,759.28** B0RG.

Heights 1 through 2,000,000 are era 0 (1,750 B0RG, except block 1). Height 2,000,001 is the first block of era 1, at half the base subsidy in integer zaps. The unit test `emission_sums_to_exact_supply` sums every paying block and requires:

- exactly 76,000,000 paying blocks, which are heights 1 through 76,000,000
- a total of 7,000,000,009 B0RG
- height 76,000,000, era 37, pays 1 zap
- height 0 pays 0

The code comment in `src/amount.h` records why the block-1 remainder exists: `2,000,000` blocks of each term `(1,750 * COIN >> k)` sum to 6,999,999,999.72 B0RG, and 9.28 B0RG closes the gap.

## 3. Proof of work

Two hashes, two jobs.

**Block hash.** `GetHash()` is SHA256d of the serialized 88-byte header. That hash identifies the block. It is not the proof of work.

**Proof-of-work hash.** `GetPoWHash()` fills a 286-byte buffer (`BuildKadenaWork`) and hashes it once with unkeyed BLAKE2s-256 (RFC 7693, 32-byte digest, no key). The implementation is `CBlake2s256` and matches Python `hashlib.blake2s(digest_size=32)`. The 32-byte digest is read as a little-endian integer and must be less than or equal to the target expanded from `nBits`. The target must be in range and must not be easier than `powLimit`.

### 3.1 Header stored on the chain (88 bytes)

| Offset | Field |
|---|---|
| 0 | `nVersion`, int32 |
| 4 | `hashPrevBlock`, 32 bytes |
| 36 | `hashMerkleRoot`, 32 bytes |
| 68 | `nTime`, uint32 Unix seconds |
| 72 | `nBits`, uint32 compact target |
| 76 | `nNonce`, **uint64** little-endian |
| 84 | `nTimePad`, uint32 |

`nTimePad` is the microseconds inside that second. A value of 1,000,000 or more is an invalid header (`bad-timepad`). The microsecond timestamp used in the work buffer is `nTime * 1,000,000 + nTimePad`.

### 3.2 Work buffer hashed by the ASIC (286 bytes)

| Offset | Size | Contents |
|---|---|---|
| 0 | 8 | flags, zero |
| 8 | 8 | creation time, Unix microseconds, little-endian |
| 16 | 32 | `hashPrevBlock`, internal little-endian bytes |
| 48 | 110 | adjacent parents: a uint16 length of 0, then zeros |
| 158 | 32 | PoW target expanded from `nBits`, little-endian |
| 190 | 32 | `hashMerkleRoot`, internal little-endian bytes |
| 222 | 4 | chain id 0 |
| 226 | 32 | weight 0 |
| 258 | 8 | height 0 (height is not committed here) |
| 266 | 4 | `nVersion`, uint32 little-endian |
| 270 | 8 | epoch 0 |
| 278 | 8 | `nNonce`, uint64 little-endian |

Bytes 158 through 189 are the network target and are inside the hashed bytes. Bytes 278 through 285 are the nonce the miner grinds. Stock KA3, Goldshell, and iBeLink firmware does not roll the time field. A block that leaves bytes 8 through 15 as the pool sent them is valid. `nTimePad` exists so a miner that does update the 8-byte microsecond field can still be represented in the 88-byte header.

Mainnet `powLimit` is `00000fff…f` (compact `0x1e0ffff0`). Regtest `powLimit` is `7fff…f` (compact `0x207fffff`), and regtest does not retarget.

### 3.3 Genesis

The genesis coinbase script carries this byte string (the dash is U+2014):

> Nov 24 2026 — Mint your drone army. Assimilate cyberspace. Resistance is futile.

The genesis output value is 0. The genesis transaction is not connected into the UTXO set. Mainnet genesis:

| | |
|---|---|
| Time | 1795478400 (2026-11-24 00:00:00 UTC) |
| `nBits` | `0x1e0ffff0` |
| Nonce | 1,104,784 |
| `nTimePad` | 0 |
| Merkle root | `dfc5b2693cd26d23c70b7d711658cd86c11b5b166664c66ff69f0acac611d06b` |
| Block hash | `7e3e2d4ad8c78b9f2cdb53657a48d9474049a10393fbfe2678df819c07657799` |

Testnet and regtest use the same merkle root, the same message, and a zero reward. Testnet time is 1795478401, nonce 1,635,351, bits `0x1e0ffff0`, hash `9bff2bdbde6b9e17bdc4f50af740076eb71921f481949597ceaa32dc96c5e386`. Regtest time is 1795478402, nonce 2, bits `0x207fffff`, hash `4a9e01c53b9b548d4ebdf77ed0b7b887667ae2da2bc52c3255234df7ae040a29`.

There are no DNS seeds and no fixed peer list in the binary. A node syncs from peers the operator adds.

## 4. Difficulty

`GetNextWorkRequired` uses Zawy LWMA-1 (`src/pow.cpp`), not Bitcoin's 2016-block retarget. The old retarget function is still in the file and is not called by consensus.

Constants: window `N = 60`, spacing `T = 30` seconds, `k = N * (N + 1) * T / 2`. While the tip height is below 60, the next target is `powLimit`. The first block whose difficulty is the LWMA result is **height 61**.

For a tip at height 60 or more, the algorithm:

1. Takes the last 60 blocks. Solvetime is the difference of `nTime` values, not the microsecond pad.
2. Clamps each solvetime to the closed range `[1, 6*T]` seconds, which is `[1, 180]`.
3. Weights the oldest clamped solvetime by 1 and the newest by 60.
4. Sets the next target to `(average of those 60 targets) * (weighted solvetimes) / k`.
5. Clamps that target to `[previous >> 4, previous << 3]`: at most 16 times harder, at most 8 times easier than the previous block, and never easier than `powLimit`. A zero target is rejected.

Regtest sets `fPowNoRetargeting` and returns the previous block's `nBits`. Testnet, and only testnet, allows a block whose timestamp is more than two spacings after its parent to use `powLimit`. Mainnet does not, because a miner can set that timestamp without waiting.

## 5. Other consensus parameters

| Rule | Mainnet value |
|---|---|
| Target spacing | 30 seconds |
| LWMA window | 60 blocks, from height 61 |
| Halving interval | 2,000,000 blocks |
| Base subsidy | 1,750 B0RG |
| Block 1 subsidy | 1,759.28 B0RG |
| Coinbase maturity | 100 blocks |
| BIP16 height | 0 |
| BIP34, BIP65, BIP66, CSV, SegWit | height 1 |
| Taproot | always active |
| MWEB | always active for a block that has a parent |
| BIP9 miner window / threshold | 60 / 45 (deployment bits; MWEB and Taproot do not wait for signaling) |
| Message start | `b0 46 0b 26` |
| P2P port | 19333 |
| RPC port | 19332 |

Coinbase maturity is the consensus constant `COINBASE_MATURITY` (100). At a 30-second target that is 50 minutes. A coinbase output cannot be spent before then. The same 100-block rule is Litecoin's; B0RG did not lower it.

SegWit is required from height 1. Taproot's version bit is marked always active. MWEB's deployment is marked always active, with one deliberate exception: `IsMWEBEnabled` is false when there is no previous block, so the HogEx in the first block after genesis does not spend a parent extension output. Later blocks do.

## 6. MWEB

MWEB is optional for the person spending coins. A transparent P2PKH or bech32 output is public in the usual way. Sending to an address with human-readable part `borgmweb` pegs the coins into the extension block. A later send from that balance to another MWEB address stays in the extension block. A send from an MWEB balance to a transparent `borg1…` or `B…` address pegs out, and that transparent output is public again.

The node builds the SegWit commitment and the HogEx when it assembles a block template. Miners and the bundled Stratum server do not rebuild those transactions themselves. They call `getminingwork`, which returns a full block, and `submitblock` to hand a solved block back.

This paper does not add a new privacy construction. The extension-block rules are the MWEB implementation shipped in the Litecoin Core version this tree forks.

## 7. Addresses and networks

Mainnet:

| Use | Encoding |
|---|---|
| P2PKH | base58 version 25, addresses start with `B` |
| P2SH | base58 version 30, addresses start with `D` |
| Secret key (WIF) | version 153 |
| HD public / secret | `04 B0 B2 1E` / `04 B0 AD E4` |
| Native SegWit | bech32 human-readable part `borg` |
| MWEB | human-readable part `borgmweb` |

Testnet and regtest share base58 versions: P2PKH 120, P2SH 125, secret 248, HD public `04 B0 35 87`, HD secret `04 B0 35 83`. Human-readable parts are `tborg` / `tborgmweb` on testnet and `rborg` / `rborgmweb` on regtest.

| Network | P2P | RPC | Message start |
|---|---|---|---|
| main | 19333 | 19332 | `b0 46 0b 26` |
| test | 29333 | 29332 | `b0 46 0b 54` |
| regtest | 19433 | 19432 | `b0 46 0b 52` |

Default data directory: `~/.b0rg` on Unix, `~/Library/Application Support/B0RG` on macOS, `%APPDATA%\B0RG` on Windows. The config file name is `b0rg.conf`.

## 8. Mining and Stratum

`stratum/b0rg_stratum.py` is Python 3, standard library only, plus `hashlib.blake2s`. It polls `getminingwork` and submits with `submitblock`. The wire protocol is the Kadena Stratum subset used by chainweb-mining-client and by the factory firmware on the machines named above.

| | |
|---|---|
| Subscribe result | `[null, "<nonce1, 2 bytes hex>", 6]` |
| Authorize | username is the payout address, or `address.worker`. The password is ignored. |
| `mining.set_target` | 32-byte big-endian **share** target |
| `mining.notify` | job id, 572 hex characters (the 286-byte header), clean-jobs flag |
| `mining.submit` | `address.worker`, job id, nonce2 hex |

`nonce1` is the high 2 bytes of the uint64 nonce. `nonce2` is the low 6 bytes the ASIC increments. On the wire both are big-endian hex. In the header they are written little-endian at bytes 278 through 285. The share target is never harder than the network target, so a header that meets the network target is submitted as a block. Vardiff aims for about one share every 15 seconds, starting from `--share-bits 40` (about 2⁴⁰ work) when the network target is harder than that. Regtest's network target is already easier and replaces that starting point.

An optional fourth submit parameter may be 16 hex characters (big-endian microseconds) or the full 572-hex header. In a full header, only bytes 8–15 and 278–285 may differ from the job. Any other changed byte is rejected.

Default listening ports of this server:

| Port | Mode |
|---|---|
| 3340 | PROP. Default fee is 1 percent (`--fee-percent` default). The worker address receives 99 percent and `--fee-address` receives 1 percent. |
| 4340 | SOLO. The worker address receives 100 percent. |
| 19340 | JSON stats for a dashboard. The same object is written to `stratum/stats.json`. |

The public pool for this project is:

| Mode | URL |
|---|---|
| PROP, 1% fee | `stratum+tcp://pool.shed-p00l.com:3340` |
| SOLO | `stratum+tcp://pool.shed-p00l.com:4340` |

Set the miner username to a B0RG address you control (`B…`, `D…`, or `borg1…`). A worker name may be appended as `address.worker`. The password can be `x`. Algorithm in the firmware is Blake2s / Kadena. Blake2b and Kadena-C32 are different algorithms and will not produce valid work for this chain.

A node that should accept the pool's blocks, or that is mining locally, needs a peer. There are no seeds in the binary. For the public pool's node:

```
addnode=pool.shed-p00l.com
```

The default P2P port is 19333, so the port can be omitted.

## 9. What this repository builds

Version 1.0.0. Programs: `b0rgd`, `b0rg-cli`, `b0rg-tx`, `b0rg-wallet`, and the Qt wallet `b0rg-qt`. Build steps are in the README. The Qt window title is B0RG. The application icon is the cube graphic in `share/pixmaps/b0rg-logo.jpg`.

## 10. Roadmap

Phase 1, which is this repository, is the full node, the Qt desktop wallet, and the Python Stratum server.

Phase 2 is a mobile wallet. It is not in this repository, and this paper does not specify its custody model, its sync method, or a ship date.

## 11. Risks and disclaimer

This is experimental software. It is a young chain with no DNS seeds, a public bootstrap difficulty equal to `powLimit` for the first 60 blocks after genesis, and a proof-of-work function dominated by whatever Blake2s hardware points at it. LWMA limits how far one block can move the target (16× harder, 8× easier). It does not stop a large miner from producing many blocks in a row, and it does not stop a reorg.

The authors and the publisher of [shed-p00l.com](https://shed-p00l.com) do not promise that B0RG will have buyers, that an exchange will list it, that the pool will stay online, or that mining will return more value than the electricity it uses. Drones are the unit of account of this chain. They are not a claim on the authors, on the pool, or on any asset. Nothing in this paper is investment advice, a solicitation, or an offer of a security.

MWEB hides a transfer only while the coins stay in the extension block, and only against parties who do not hold the viewing keys the wallet keeps. Peg-ins, peg-outs, and transparent outputs are public. A bug in the fork, in MWEB, or in a wallet can lose coins. Run a node you built yourself if you are going to hold a balance, and keep the wallet file and its passphrase offline from the pool host.

Review the diff against Litecoin Core v0.21.4 before you depend on it. The MIT license in [COPYING](COPYING) applies to the software, including this paper: it is provided as is, without warranty.
