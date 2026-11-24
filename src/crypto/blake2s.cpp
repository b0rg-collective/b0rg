// Copyright (c) 2026 The Assimilation Coin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <crypto/blake2s.h>

#include <openssl/evp.h>

#include <cassert>
#include <stdexcept>

void CBlake2s256(const unsigned char* data, size_t len, unsigned char out[32])
{
    unsigned int outlen = 32;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    assert(ctx != nullptr);
    if (EVP_DigestInit_ex(ctx, EVP_blake2s256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data, len) != 1 ||
        EVP_DigestFinal_ex(ctx, out, &outlen) != 1 ||
        outlen != 32) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("BLAKE2s-256 failed");
    }
    EVP_MD_CTX_free(ctx);
}
