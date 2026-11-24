// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/block.h>

#include <arith_uint256.h>
#include <crypto/blake2s.h>
#include <crypto/common.h>
#include <hash.h>
#include <tinyformat.h>
#include <util/strencodings.h>

#include <cstring>

/**
 * Kadena Chainweb work header (286 bytes). ASICs hash exactly these bytes
 * once with unkeyed BLAKE2s-256 and compare the digest, as a little-endian
 * 256-bit integer, to the target.
 *
 * Offset  Size  Field
 * 0       8     flags (reserved, zero)
 * 8       8     creation time, Unix microseconds, little-endian
 * 16      32    parent hash (block hashPrevBlock, internal LE byte order)
 * 48      110   adjacent parents (uint16 length = 0, then zeros; no side chains)
 * 158     32    PoW target expanded from nBits, little-endian
 * 190     32    payload hash (block hashMerkleRoot, internal LE byte order)
 * 222     4     chain id (0)
 * 226     32    chainwork weight (0; not part of B0RG consensus state)
 * 258     8     height (0; height is not committed inside the Bitcoin-style header)
 * 266     4     version (nVersion as uint32 LE)
 * 270     8     epoch start (0)
 * 278     8     nonce (nNonce), little-endian. This is the field KA3 / KD / BM-K grind.
 */
void BuildKadenaWork(const CBlockHeader& hdr, unsigned char out[KADENA_WORK_SIZE])
{
    std::memset(out, 0, 286);

    uint64_t usec = (uint64_t)hdr.nTime * 1000000ull + (uint64_t)hdr.nTimePad;
    WriteLE64(out + 8, usec);
    std::memcpy(out + 16, hdr.hashPrevBlock.begin(), 32);

    arith_uint256 target;
    target.SetCompact(hdr.nBits);
    uint256 target_le = ArithToUint256(target);
    std::memcpy(out + 158, target_le.begin(), 32);

    std::memcpy(out + 190, hdr.hashMerkleRoot.begin(), 32);
    WriteLE32(out + 266, (uint32_t)hdr.nVersion);
    WriteLE64(out + 278, hdr.nNonce);
}

uint256 CBlockHeader::GetHash() const
{
    return SerializeHash(*this);
}

uint256 CBlockHeader::GetPoWHash() const
{
    unsigned char work[KADENA_WORK_SIZE];
    BuildKadenaWork(*this, work);
    uint256 out;
    CBlake2s256(work, KADENA_WORK_SIZE, out.begin());
    return out;
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=0x%08x, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%llu, nTimePad=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, (unsigned long long)nNonce, nTimePad,
        vtx.size());
    for (const auto& tx : vtx) {
        s << "  " << tx->ToString() << "\n";
    }
    return s.str();
}

CTransactionRef CBlock::GetHogEx() const noexcept
{
    if (vtx.size() >= 2 && vtx.back()->IsHogEx()) {
        assert(!vtx.back()->vout.empty());
        return vtx.back();
    }

    return nullptr;
}