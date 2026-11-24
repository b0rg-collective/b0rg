"""Kadena-compatible 286-byte Blake2s-256 work header for Assimilation Coin.

The layout matches Chainweb's degree-3 block header (the 286 bytes ASICs hash;
the trailing 32-byte block hash is not part of the preimage):

    0    8   flags (0)
    8    8   creation time, Unix microseconds, little-endian
    16   32  parent hash, little-endian
    48   110 adjacent parents (uint16 length 0, then zeros)
    158  32  expanded nBits target, little-endian
    190  32  payload / merkle hash, little-endian
    222  4   chain id (0)
    226  32  weight (0)
    258  8   height (0; not committed in the B0RG header)
    266  4   nVersion, uint32 little-endian
    270  8   epoch (0)
    278  8   nonce, uint64 little-endian

Stratum (chainweb-mining-client / f2pool / poolflare, the dialect KA3,
Goldshell KD-series and iBeLink BM-K speak):

    mining.subscribe result: [null, nonce1_hex, nonce2_size]
    mining.set_target params: ["<32-byte target, big-endian hex>"]
    mining.notify params: [jobId, "<286-byte header hex>", cleanJobs]
    mining.submit params: [username.worker, jobId, nonce2_hex]

nonce1 is the most-significant bytes of the 8-byte nonce (pool).
nonce2 is the least-significant bytes (miner). On the wire both are
big-endian hex. They are composed as

    nonce = (nonce2 << (8 * len(nonce1))) | nonce1

and written little-endian at offset 278. With a 2-byte nonce1 the miner
grinds the last 6 bytes of the header.

Stock firmware submits only nonce2 and does not roll creation time.
Bytes 8..15 stay as the pool sent them. An optional 4th submit parameter
of 16 hex characters is a big-endian microsecond timestamp the miner
wrote into those bytes; a 572-hex 4th parameter is the full header, and
only the time and nonce slots may differ from the job.
"""

from __future__ import annotations

import hashlib
import struct

WORK_SIZE = 286
TIME_OFFSET = 8
TARGET_OFFSET = 158
NONCE_OFFSET = 278
NONCE1_SIZE = 2
NONCE2_SIZE = 6

# Bitcoin "difficulty 1" target. Pool difficulty is DIFF1 / target.
DIFF1 = 0xFFFF * (1 << (8 * (0x1D - 3)))


def compact_to_target(nbits: int) -> int:
    """Bitcoin/Litecoin compact-size expansion (arith_uint256::SetCompact)."""
    nsize = nbits >> 24
    nword = nbits & 0x007FFFFF
    if nsize <= 3:
        nword >>= 8 * (3 - nsize)
    else:
        nword <<= 8 * (nsize - 3)
    negative = (nbits & 0x00800000) != 0
    overflow = nword != 0 and ((nsize > 34) or (nword > 0xFF and nsize > 33) or (nword > 0xFFFF and nsize > 32))
    if negative or overflow or nword == 0:
        return 0
    return nword


def target_to_le(target: int) -> bytes:
    if target < 0 or target >= 1 << 256:
        raise ValueError("target out of range")
    return int(target).to_bytes(32, "little")


def target_to_be_hex(target: int) -> str:
    return int(target).to_bytes(32, "big").hex()


def target_from_be_hex(hex_str: str) -> int:
    raw = bytes.fromhex(hex_str.strip())
    if len(raw) != 32:
        raise ValueError("target must be 32 bytes")
    return int.from_bytes(raw, "big")


def build_work(
    version: int,
    prev_le: bytes,
    merkle_le: bytes,
    ntime: int,
    ntime_pad: int,
    nbits: int,
    nonce: int,
) -> bytes:
    if len(prev_le) != 32 or len(merkle_le) != 32:
        raise ValueError("prev and merkle must be 32 bytes")
    if not 0 <= ntime_pad < 1_000_000:
        raise ValueError("ntime_pad out of range")
    buf = bytearray(WORK_SIZE)
    usec = int(ntime) * 1_000_000 + int(ntime_pad)
    struct.pack_into("<Q", buf, TIME_OFFSET, usec)
    buf[16:48] = prev_le
    buf[TARGET_OFFSET:TARGET_OFFSET + 32] = target_to_le(compact_to_target(nbits))
    buf[190:222] = merkle_le
    struct.pack_into("<I", buf, 266, version & 0xFFFFFFFF)
    struct.pack_into("<Q", buf, NONCE_OFFSET, int(nonce) & ((1 << 64) - 1))
    return bytes(buf)


def blake2s256(data: bytes) -> bytes:
    return hashlib.blake2s(data, digest_size=32).digest()


def hash_meets(digest: bytes, target: int) -> bool:
    return int.from_bytes(digest, "little") <= target


def compose_nonce(nonce1: int, nonce2: int, nonce1_size: int = NONCE1_SIZE) -> int:
    return ((int(nonce2) << (8 * nonce1_size)) | int(nonce1)) & ((1 << 64) - 1)


def split_usec(usec: int) -> tuple[int, int]:
    usec = int(usec)
    if usec < 0:
        raise ValueError("negative time")
    return usec // 1_000_000, usec % 1_000_000


def read_usec(work: bytes) -> int:
    return struct.unpack_from("<Q", work, TIME_OFFSET)[0]


def read_nonce(work: bytes) -> int:
    return struct.unpack_from("<Q", work, NONCE_OFFSET)[0]


def write_nonce(work: bytearray, nonce: int) -> None:
    struct.pack_into("<Q", work, NONCE_OFFSET, int(nonce) & ((1 << 64) - 1))


def write_usec(work: bytearray, usec: int) -> None:
    struct.pack_into("<Q", work, TIME_OFFSET, int(usec))


def difficulty_of(target: int) -> float:
    if target <= 0:
        return 0.0
    return DIFF1 / target


def patch_block_hex(block_hex: str, nonce: int, ntime: int | None = None, ntime_pad: int | None = None) -> str:
    raw = bytearray.fromhex(block_hex)
    if len(raw) < 88:
        raise ValueError("block hex is shorter than a header")
    struct.pack_into("<Q", raw, 76, int(nonce) & ((1 << 64) - 1))
    if ntime is not None:
        struct.pack_into("<I", raw, 68, int(ntime) & 0xFFFFFFFF)
    if ntime_pad is not None:
        struct.pack_into("<I", raw, 84, int(ntime_pad) & 0xFFFFFFFF)
    return raw.hex()
