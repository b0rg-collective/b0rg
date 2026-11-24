// Copyright (c) 2026 The Assimilation Coin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <amount.h>
#include <arith_uint256.h>
#include <chain.h>
#include <chainparams.h>
#include <crypto/blake2s.h>
#include <pow.h>
#include <primitives/block.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/strencodings.h>
#include <validation.h>

#include <boost/test/unit_test.hpp>

#include <cstring>
#include <string>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(b0rg_tests, BasicTestingSetup)

static const char* BLAKE2S_ABC =
    "508c5e8c327c14e2e1a72ba34eeb452f37458b209ed63a294d999b4c86675982";

// Header: version 0x20000000, prev = 00..1f, merkle = 20..3f,
// nTime = 1795478400, nTimePad = 123456, nBits = 0x1e0ffff0,
// nonce = 0x0123456789abcdef. Digest is the raw BLAKE2s byte order
// (Python hashlib.blake2s(..., digest_size=32).hexdigest()).
static const char* POW_WORK_HEX =
    "000000000000000040c23d64fa600600000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000f0ff0f0000202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000200000000000000000efcdab8967452301";
static const char* POW_HASH_HEX =
    "cf537bd0bb53c25054fb9dec1873d3227589351b266a326766c2584fa2ed30f4";

BOOST_AUTO_TEST_CASE(blake2s_rfc7693_abc)
{
    unsigned char out[32];
    const unsigned char msg[3] = {'a', 'b', 'c'};
    CBlake2s256(msg, 3, out);
    BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>(out, 32)), BLAKE2S_ABC);
}

BOOST_AUTO_TEST_CASE(kadena_work_matches_python_blake2s)
{
    CBlockHeader hdr;
    hdr.nVersion = 0x20000000;
    for (int i = 0; i < 32; ++i) {
        hdr.hashPrevBlock.begin()[i] = (unsigned char)i;
        hdr.hashMerkleRoot.begin()[i] = (unsigned char)(i + 32);
    }
    hdr.nTime = 1795478400;
    hdr.nTimePad = 123456;
    hdr.nBits = 0x1e0ffff0;
    hdr.nNonce = 0x0123456789abcdefULL;

    unsigned char work[KADENA_WORK_SIZE];
    BuildKadenaWork(hdr, work);
    BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>(work, KADENA_WORK_SIZE)), POW_WORK_HEX);

    const uint256 pow = hdr.GetPoWHash();
    BOOST_CHECK_EQUAL(HexStr(Span<const uint8_t>(pow.begin(), 32)), POW_HASH_HEX);

    // Nonce sits at bytes 278..285 and the target occupies 158..189.
    BOOST_CHECK_EQUAL(work[278], 0xef);
    BOOST_CHECK_EQUAL(work[285], 0x01);
    BOOST_CHECK_EQUAL(work[158], 0x00);
}

BOOST_AUTO_TEST_CASE(header_serialization_is_88_bytes)
{
    CBlockHeader hdr;
    hdr.nVersion = 1;
    hdr.nTime = 1795478400;
    hdr.nBits = 0x1e0ffff0;
    hdr.nNonce = 0x0123456789abcdefULL;
    hdr.nTimePad = 7;
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << hdr;
    BOOST_CHECK_EQUAL(ss.size(), B0RG_HEADER_SIZE);
    const unsigned char* p = (const unsigned char*)&ss[0];
    uint64_t nonce = 0;
    std::memcpy(&nonce, p + B0RG_NONCE_OFFSET, 8);
    BOOST_CHECK_EQUAL(nonce, hdr.nNonce);
    uint32_t pad = 0;
    std::memcpy(&pad, p + B0RG_TIMEPAD_OFFSET, 4);
    BOOST_CHECK_EQUAL(pad, hdr.nTimePad);
}

BOOST_AUTO_TEST_CASE(emission_sums_to_exact_supply)
{
    const auto chainparams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const auto& consensus = chainparams->GetConsensus();
    BOOST_CHECK_EQUAL(consensus.nSubsidyHalvingInterval, 2000000);
    BOOST_CHECK_EQUAL(consensus.nPowTargetSpacing, 30);

    CAmount total = 0;
    int height = 1;
    int blocks = 0;
    while (true) {
        const int halvings = (height - 1) / consensus.nSubsidyHalvingInterval;
        if (halvings >= 64) break;
        CAmount subsidy = GetBlockSubsidy(height, consensus);
        if (subsidy == 0) break;
        total += subsidy;
        ++blocks;
        ++height;
        if (blocks > 80000000) break;
    }
    BOOST_CHECK_EQUAL(blocks, 76000000);
    BOOST_CHECK_EQUAL(total, B0RG_MAX_SUPPLY);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(0, consensus), CAmount{0});
    BOOST_CHECK_EQUAL(GetBlockSubsidy(1, consensus), B0RG_BASE_SUBSIDY + B0RG_BLOCK1_REMAINDER);
}

static CBlockIndex MakeTip(std::vector<CBlockIndex>& blocks, int64_t spacing, unsigned int bits, int64_t t0)
{
    blocks.resize(61);
    for (int i = 0; i < 61; ++i) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = t0 + i * spacing;
        blocks[i].nBits = bits;
    }
    return blocks.back();
}

BOOST_AUTO_TEST_CASE(lwma_steady_blocks_hold_difficulty)
{
    const auto chainparams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const auto& consensus = chainparams->GetConsensus();
    std::vector<CBlockIndex> blocks;
    const unsigned int bits = 0x1e0ffff0;
    CBlockIndex tip = MakeTip(blocks, consensus.nPowTargetSpacing, bits, 1700000000);
    CBlockHeader next;
    next.nTime = tip.nTime + consensus.nPowTargetSpacing;
    const unsigned int got = GetNextWorkRequired(&blocks.back(), &next, consensus);
    arith_uint256 expect;
    expect.SetCompact(bits);
    arith_uint256 got_target;
    got_target.SetCompact(got);
    BOOST_CHECK(got_target == expect);
}

BOOST_AUTO_TEST_CASE(lwma_fast_blocks_are_clamped)
{
    const auto chainparams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const auto& consensus = chainparams->GetConsensus();
    std::vector<CBlockIndex> blocks;
    const unsigned int bits = 0x1c0ffff0;
    // 1-second spacing. Natural LWMA wants about T/1 = 30x harder; the clamp is 16x.
    CBlockIndex tip = MakeTip(blocks, 1, bits, 1700000000);
    CBlockHeader next;
    next.nTime = tip.nTime + 1;
    const unsigned int got = GetNextWorkRequired(&blocks.back(), &next, consensus);
    arith_uint256 prev;
    prev.SetCompact(bits);
    arith_uint256 expect = prev >> 4;
    arith_uint256 expect_rt;
    expect_rt.SetCompact(expect.GetCompact());
    arith_uint256 got_target;
    got_target.SetCompact(got);
    BOOST_CHECK(got_target == expect_rt);
    BOOST_CHECK(got_target < prev);
}

BOOST_AUTO_TEST_CASE(lwma_slow_blocks_drop_difficulty_within_bound)
{
    const auto chainparams = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    const auto& consensus = chainparams->GetConsensus();
    std::vector<CBlockIndex> blocks;
    const unsigned int bits = 0x1c0ffff0;
    // Every solvetime is far above 6*T, so each one clamps to 6*T and the
    // window asks for a 6x easier target. That is inside the 8x easy clamp
    // and still harder than powLimit.
    CBlockIndex tip = MakeTip(blocks, consensus.nPowTargetSpacing * 100, bits, 1700000000);
    CBlockHeader next;
    next.nTime = tip.nTime + consensus.nPowTargetSpacing * 100;
    const unsigned int got = GetNextWorkRequired(&blocks.back(), &next, consensus);
    arith_uint256 prev;
    prev.SetCompact(bits);
    arith_uint256 expect = prev * 6;
    arith_uint256 expect_rt;
    expect_rt.SetCompact(expect.GetCompact());
    arith_uint256 got_target;
    got_target.SetCompact(got);
    BOOST_CHECK(got_target == expect_rt);
    BOOST_CHECK(got_target > prev);
    BOOST_CHECK(got_target <= UintToArith256(consensus.powLimit));
}

BOOST_AUTO_TEST_CASE(lwma_bootstrap_and_regtest)
{
    const auto main = CreateChainParams(*m_node.args, CBaseChainParams::MAIN);
    CBlockIndex early;
    early.nHeight = 10;
    early.nTime = 1700000000;
    early.nBits = 0x1e0ffff0;
    CBlockHeader next;
    next.nTime = early.nTime + 30;
    const unsigned int limit = UintToArith256(main->GetConsensus().powLimit).GetCompact();
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&early, &next, main->GetConsensus()), limit);

    const auto regtest = CreateChainParams(*m_node.args, CBaseChainParams::REGTEST);
    std::vector<CBlockIndex> blocks;
    CBlockIndex tip = MakeTip(blocks, 1, 0x207fffff, 1700000000);
    next.nTime = tip.nTime + 1;
    BOOST_CHECK_EQUAL(GetNextWorkRequired(&blocks.back(), &next, regtest->GetConsensus()), 0x207fffffu);
}

BOOST_AUTO_TEST_SUITE_END()
