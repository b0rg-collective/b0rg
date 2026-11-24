// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/block.h>
#include <uint256.h>

#include <algorithm>
#include <vector>

namespace {
/** Zawy LWMA-1 window. Difficulty is adjusted every block once this many exist. */
static const int64_t B0RG_LWMA_WINDOW = 60;
/** Solvetime clamp, in multiples of the target spacing (Zawy's 6*T). */
static const int64_t B0RG_LWMA_SOLVETIME_CLAMP = 6;
/** Hardest the next target may be, relative to the previous block (16x). */
static const int B0RG_LWMA_MAX_HARDER_BITS = 4;
/** Easiest the next target may be, relative to the previous block (8x). */
static const int B0RG_LWMA_MAX_EASIER_BITS = 3;

/**
 * Zawy LWMA-1 (linear weighted moving average).
 * https://github.com/zawy12/difficulty-algorithms/issues/3
 *
 * next_target = (average of the last N targets) * (weighted solvetimes) / k
 * k = N * (N + 1) * T / 2
 * Oldest solvetime has weight 1, newest has weight N.
 * Solvetimes are clamped to [1, 6*T] so one stamped gap cannot wreck the
 * window, and the result is clamped to [prev/16, prev*8] and to powLimit so a
 * KA3 arriving or leaving cannot move difficulty by more than those bounds in
 * a single block. Before N blocks exist, powLimit (the easy bootstrap target)
 * is used.
 */
unsigned int LwmaNextWorkRequired(const CBlockIndex* pindexLast, const Consensus::Params& params)
{
    const int64_t T = params.nPowTargetSpacing;
    const int64_t N = B0RG_LWMA_WINDOW;
    const int64_t k = N * (N + 1) * T / 2;
    const unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    if (pindexLast->nHeight < N || pindexLast->pprev == nullptr) {
        return nProofOfWorkLimit;
    }

    std::vector<int64_t> solvetimes(N);
    arith_uint256 sum_target;
    const CBlockIndex* block = pindexLast;
    for (int i = N - 1; i >= 0; --i) {
        if (block == nullptr || block->pprev == nullptr) {
            return nProofOfWorkLimit;
        }
        solvetimes[i] = block->GetBlockTime() - block->pprev->GetBlockTime();
        arith_uint256 target;
        target.SetCompact(block->nBits);
        sum_target += target;
        block = block->pprev;
    }

    int64_t weighted = 0;
    const int64_t max_solvetime = B0RG_LWMA_SOLVETIME_CLAMP * T;
    for (int i = 0; i < N; ++i) {
        int64_t solvetime = solvetimes[i];
        if (solvetime < 1) solvetime = 1;
        if (solvetime > max_solvetime) solvetime = max_solvetime;
        weighted += solvetime * (i + 1);
    }
    if (weighted < 1) weighted = 1;

    arith_uint256 next = sum_target / N;
    next *= weighted;
    next /= k;

    arith_uint256 prev;
    prev.SetCompact(pindexLast->nBits);
    arith_uint256 min_target = prev >> B0RG_LWMA_MAX_HARDER_BITS;
    if (min_target == 0) min_target = 1;
    arith_uint256 max_target = prev << B0RG_LWMA_MAX_EASIER_BITS;
    if (next < min_target) next = min_target;
    if (next > max_target) next = max_target;

    const arith_uint256 limit = UintToArith256(params.powLimit);
    if (next > limit) next = limit;
    if (next == 0) next = 1;
    return next.GetCompact();
}
} // namespace

unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader *pblock, const Consensus::Params& params)
{
    assert(pindexLast != nullptr);
    const unsigned int nProofOfWorkLimit = UintToArith256(params.powLimit).GetCompact();

    // Testnet only: a block whose timestamp is more than two spacings after
    // its parent may use the minimum difficulty. Mainnet does not, because a
    // miner can set that timestamp without waiting.
    if (params.fPowAllowMinDifficultyBlocks && pblock != nullptr) {
        if (pblock->GetBlockTime() > pindexLast->GetBlockTime() + params.nPowTargetSpacing * 2)
            return nProofOfWorkLimit;
    }

    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    return LwmaNextWorkRequired(pindexLast, params);
}

unsigned int CalculateNextWorkRequired(const CBlockIndex* pindexLast, int64_t nFirstBlockTime, const Consensus::Params& params)
{
    if (params.fPowNoRetargeting)
        return pindexLast->nBits;

    // Limit adjustment step
    int64_t nActualTimespan = pindexLast->GetBlockTime() - nFirstBlockTime;
    if (nActualTimespan < params.nPowTargetTimespan/4)
        nActualTimespan = params.nPowTargetTimespan/4;
    if (nActualTimespan > params.nPowTargetTimespan*4)
        nActualTimespan = params.nPowTargetTimespan*4;

    // Retarget
    arith_uint256 bnNew;
    arith_uint256 bnOld;
    bnNew.SetCompact(pindexLast->nBits);
    bnOld = bnNew;
    // Litecoin: intermediate uint256 can overflow by 1 bit
    const arith_uint256 bnPowLimit = UintToArith256(params.powLimit);
    bool fShift = bnNew.bits() > bnPowLimit.bits() - 1;
    if (fShift)
        bnNew >>= 1;
    bnNew *= nActualTimespan;
    bnNew /= params.nPowTargetTimespan;
    if (fShift)
        bnNew <<= 1;

    if (bnNew > bnPowLimit)
        bnNew = bnPowLimit;

    return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const Consensus::Params& params)
{
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

    // Check range
    if (fNegative || bnTarget == 0 || fOverflow || bnTarget > UintToArith256(params.powLimit))
        return false;

    // Check proof of work matches claimed amount
    if (UintToArith256(hash) > bnTarget)
        return false;

    return true;
}
