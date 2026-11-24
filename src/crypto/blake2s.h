// Copyright (c) 2026 The Assimilation Coin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_BLAKE2S_H
#define BITCOIN_CRYPTO_BLAKE2S_H

#include <stddef.h>
#include <stdint.h>

/** Unkeyed BLAKE2s-256 (RFC 7693, 32-byte digest). Matches Python hashlib.blake2s(digest_size=32). */
void CBlake2s256(const unsigned char* data, size_t len, unsigned char out[32]);

#endif // BITCOIN_CRYPTO_BLAKE2S_H
