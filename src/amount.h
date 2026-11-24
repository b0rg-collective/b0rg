// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_AMOUNT_H
#define BITCOIN_AMOUNT_H

#include <stdint.h>

/** Amount in satoshis (Can be negative) */
typedef int64_t CAmount;

static const CAmount COIN = 100000000;

/** Base block subsidy before the halving shift (1,750 drones). */
static const CAmount B0RG_BASE_SUBSIDY = 1750 * COIN;
/**
 * Added to block 1 so the integer-halving series sums to exactly
 * 7,000,000,009.00000000 B0RG. See GetBlockSubsidy().
 * 2,000,000 * sum_k (1750*COIN >> k) = 6,999,999,999.72000000 B0RG.
 */
static const CAmount B0RG_BLOCK1_REMAINDER = 928000000;
/** Exact maximum spendable supply, in drone-satoshis. */
static const CAmount B0RG_MAX_SUPPLY = 7000000009LL * COIN;

/** No amount larger than this (in satoshi) is valid.
 *
 * Consensus-critical ceiling. Equal to the exact spendable emission of
 * Assimilation Coin (7,000,000,009 B0RG).
 */
static const CAmount MAX_MONEY = B0RG_MAX_SUPPLY;
inline bool MoneyRange(const CAmount& nValue) { return (nValue >= 0 && nValue <= MAX_MONEY); }

#endif //  BITCOIN_AMOUNT_H
