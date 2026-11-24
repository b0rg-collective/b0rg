// Copyright (c) 2026 The Assimilation Coin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Unkeyed BLAKE2s-256, RFC 7693. No OpenSSL: the Windows depends tree
// ships OpenSSL 1.0.1, which has no EVP_blake2s256.

#include <crypto/blake2s.h>

#include <cstring>

static const uint32_t blake2s_IV[8] = {
    0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
    0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u
};

static const uint8_t blake2s_sigma[10][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
    { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
    { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
    { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
    { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
    { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
    { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
    { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
    { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
};

static inline uint32_t rotr32(uint32_t x, int n)
{
    return (x >> n) | (x << (32 - n));
}

static void G(uint32_t v[16], int a, int b, int c, int d, uint32_t x, uint32_t y)
{
    v[a] = v[a] + v[b] + x;
    v[d] = rotr32(v[d] ^ v[a], 16);
    v[c] = v[c] + v[d];
    v[b] = rotr32(v[b] ^ v[c], 12);
    v[a] = v[a] + v[b] + y;
    v[d] = rotr32(v[d] ^ v[a], 8);
    v[c] = v[c] + v[d];
    v[b] = rotr32(v[b] ^ v[c], 7);
}

static void compress(uint32_t h[8], const uint8_t block[64], uint64_t t, int last)
{
    uint32_t m[16];
    uint32_t v[16];
    for (int i = 0; i < 16; ++i) {
        m[i] = (uint32_t)block[i * 4]
             | ((uint32_t)block[i * 4 + 1] << 8)
             | ((uint32_t)block[i * 4 + 2] << 16)
             | ((uint32_t)block[i * 4 + 3] << 24);
    }
    for (int i = 0; i < 8; ++i) v[i] = h[i];
    for (int i = 0; i < 8; ++i) v[i + 8] = blake2s_IV[i];
    v[12] ^= (uint32_t)t;
    v[13] ^= (uint32_t)(t >> 32);
    if (last) v[14] = ~v[14];
    for (int r = 0; r < 10; ++r) {
        const uint8_t* s = blake2s_sigma[r];
        G(v, 0, 4, 8, 12, m[s[0]], m[s[1]]);
        G(v, 1, 5, 9, 13, m[s[2]], m[s[3]]);
        G(v, 2, 6, 10, 14, m[s[4]], m[s[5]]);
        G(v, 3, 7, 11, 15, m[s[6]], m[s[7]]);
        G(v, 0, 5, 10, 15, m[s[8]], m[s[9]]);
        G(v, 1, 6, 11, 12, m[s[10]], m[s[11]]);
        G(v, 2, 7, 8, 13, m[s[12]], m[s[13]]);
        G(v, 3, 4, 9, 14, m[s[14]], m[s[15]]);
    }
    for (int i = 0; i < 8; ++i) h[i] ^= v[i] ^ v[i + 8];
}

void CBlake2s256(const unsigned char* data, size_t len, unsigned char out[32])
{
    uint32_t h[8];
    for (int i = 0; i < 8; ++i) h[i] = blake2s_IV[i];
    // digest length 32, key length 0, fanout 1, depth 1
    h[0] ^= 0x01010020u;

    uint8_t block[64];
    uint64_t counter = 0;
    size_t offset = 0;
    while (len - offset > 64) {
        std::memcpy(block, data + offset, 64);
        counter += 64;
        compress(h, block, counter, 0);
        offset += 64;
    }
    std::memset(block, 0, 64);
    const size_t remain = len - offset;
    if (remain) std::memcpy(block, data + offset, remain);
    counter += remain;
    compress(h, block, counter, 1);

    for (int i = 0; i < 8; ++i) {
        out[i * 4]     = (unsigned char)(h[i]);
        out[i * 4 + 1] = (unsigned char)(h[i] >> 8);
        out[i * 4 + 2] = (unsigned char)(h[i] >> 16);
        out[i * 4 + 3] = (unsigned char)(h[i] >> 24);
    }
}
