#ifndef __PRG_HPP__
#define __PRG_HPP__

#include <array>

#include "bitutils.hpp"
#include "aes.hpp"

static const struct PRGkey {
    AESkey k;
    PRGkey(__m128i key) {
        AES_128_Key_Expansion(k, key);
    }
}
// Digits of e
prgkey(
    _mm_set_epi64x(2718281828459045235ULL, 3602874713526624977ULL)
),
// Digits of pi
leafprgkeys[3] = {
    _mm_set_epi64x(3141592653589793238ULL, 4626433832795028841ULL),
    _mm_set_epi64x(9716939937510582097ULL, 4944592307816406286ULL),
    _mm_set_epi64x(2089986280348253421ULL, 1706798214808651328ULL)
};

#if VALUE_BITS == 64

// Compute one of the children of node seed; whichchild=0 for
// the left child, 1 for the right child
// cost: 1 AES encryption
static void prg(__m128i &out, __m128i seed, bool whichchild,
    size_t &aes_ops)
{
    __m128i in = set_lsb(seed, whichchild);
    __m128i mid;
    AES_ECB_encrypt(mid, set_lsb(seed, whichchild), prgkey.k, aes_ops);
    out = mid ^ in;
}

// Compute both children of node seed
static void prgboth(__m128i &left, __m128i &right, __m128i seed,
    size_t &aes_ops)
{
    __m128i inl = set_lsb(seed, 0);
    __m128i inr = set_lsb(seed, 1);
    __m128i midl, midr;
    AES_ECB_encrypt(midl, inl, prgkey.k, aes_ops);  // ciphertext (midl) is result of encryption
    AES_ECB_encrypt(midr, inr, prgkey.k, aes_ops);
    left = midl ^ inl;  // XORs the ciphertext with the plaintext (OTP?)
    right = midr ^ inr;
}

// Compute one of the leaf children of node seed; whichchild=0 for
// the left child, 1 for the right child
template <size_t LWIDTH>
static void prg(std::array<__m128i,LWIDTH> &out,
    __m128i seed, bool whichchild, size_t &aes_ops)
{
    __m128i in = set_lsb(seed, whichchild);
    __m128i mid0, mid1, mid2;
    AES_ECB_encrypt(mid0, set_lsb(seed, whichchild), leafprgkeys[0].k, aes_ops);
    if (LWIDTH > 1) {
        AES_ECB_encrypt(mid1, set_lsb(seed, whichchild), leafprgkeys[1].k, aes_ops);
    }
    if (LWIDTH > 2) {
        AES_ECB_encrypt(mid2, set_lsb(seed, whichchild), leafprgkeys[2].k, aes_ops);
    }
    out[0] = mid0 ^ in;
    if (LWIDTH > 1) {
        out[1] = mid1 ^ in;
    }
    if (LWIDTH > 2) {
        out[2] = mid2 ^ in;
    }
}

// Compute both of the leaf children of node seed
template <size_t LWIDTH>
static void prgboth(std::array<__m128i,LWIDTH> &left,
    std::array<__m128i,LWIDTH> &right, __m128i seed, size_t &aes_ops)
{
    __m128i inl = set_lsb(seed, 0);
    __m128i inr = set_lsb(seed, 1);
    __m128i midl0, midl1, midl2;
    __m128i midr0, midr1, midr2;
    AES_ECB_encrypt(midl0, inl, leafprgkeys[0].k, aes_ops);
    AES_ECB_encrypt(midr0, inr, leafprgkeys[0].k, aes_ops);
    if (LWIDTH > 1) {
        AES_ECB_encrypt(midl1, inl, leafprgkeys[1].k, aes_ops);
        AES_ECB_encrypt(midr1, inr, leafprgkeys[1].k, aes_ops);
    }
    if (LWIDTH > 2) {
        AES_ECB_encrypt(midl2, inl, leafprgkeys[2].k, aes_ops);
        AES_ECB_encrypt(midr2, inr, leafprgkeys[2].k, aes_ops);
    }
    left[0] = midl0 ^ inl;
    right[0] = midr0 ^ inr;
    if (LWIDTH > 1) {
        left[1] = midl1 ^ inl;
        right[1] = midr1 ^ inr;
    }
    if (LWIDTH > 2) {
        left[2] = midl2 ^ inl;
        right[2] = midr2 ^ inr;
    }
}

#elif VALUE_BITS == 128

// Compute one of the children of node seed; whichchild=0 for
// the left child, 1 for the right child
// cost: 1 AES encryption
static void prg(__m256i &out, __m256i seed, bool whichchild,
    size_t &aes_ops)
{
    __m256i in = set_lsb(seed, whichchild);
    __m256i mid;
    __m128i mid_lower, mid_upper, in_lower, in_upper;

    in_lower = _mm256_castsi256_si128(in);
    in_upper = _mm256_extracti128_si256(in, 1);

    AES_ECB_encrypt(mid_lower, in_lower, prgkey.k, aes_ops);
    AES_ECB_encrypt(mid_upper, in_upper, prgkey.k, aes_ops);

    mid = _mm256_set_m128i(mid_upper, in_lower);
    out = mid ^ in;
}

// Compute both children of node seed
static void prgboth(__m256i &left, __m256i &right, __m256i seed,
    size_t &aes_ops)
{
    __m256i inl = set_lsb(seed, 0);
    __m256i inr = set_lsb(seed, 1);
    __m128i midl_upper, midl_lower, midr_upper, midr_lower;  // encryption results
    __m128i inl_upper, inl_lower, inr_upper, inr_lower;

    inl_upper = _mm256_extracti128_si256(inl, 1);
    inl_lower = _mm256_extracti128_si256(inl, 0);
    inr_upper = _mm256_extracti128_si256(inr, 1);
    inr_lower = _mm256_extracti128_si256(inr, 0);

    AES_ECB_encrypt(midl_upper, inl_upper, prgkey.k, aes_ops);
    AES_ECB_encrypt(midl_lower, inl_lower, prgkey.k, aes_ops);
    AES_ECB_encrypt(midr_upper, inr_upper, prgkey.k, aes_ops);
    AES_ECB_encrypt(midr_lower, inr_lower, prgkey.k, aes_ops);

    __m256i midl = _mm256_set_m128i(midl_upper, midl_lower);
    __m256i midr = _mm256_set_m128i(midr_upper, midr_lower);

    left = midl ^ inl;  // XORs the ciphertext with the plaintext (OTP?)
    right = midr ^ inr;
}

// Compute one of the leaf children of node seed; whichchild=0 for
// the left child, 1 for the right child
template <size_t LWIDTH>
static void prg(std::array<__m256i,LWIDTH> &out,
    __m256i seed, bool whichchild, size_t &aes_ops)
{
    __m256i in = set_lsb(seed, whichchild);
    __m256i mid0, mid1, mid2;
    __m128i mid0_upper, mid0_lower, mid1_upper, mid1_lower, mid2_upper, mid2_lower, in_upper, in_lower;
    in_upper = _mm256_extracti128_si256(in, 1);
    in_lower = _mm256_extracti128_si256(in, 0);

    mid0_upper = _mm256_extracti128_si256(mid0, 1);
    mid0_lower = _mm256_extracti128_si256(mid0, 0);

    mid1_upper = _mm256_extracti128_si256(mid1, 1);
    mid1_lower = _mm256_extracti128_si256(mid1, 0);

    mid2_upper = _mm256_extracti128_si256(mid2, 1);
    mid2_lower = _mm256_extracti128_si256(mid2, 0);

    AES_ECB_encrypt(mid0_upper, in_upper, leafprgkeys[0].k, aes_ops);
    AES_ECB_encrypt(mid0_lower, in_lower, leafprgkeys[0].k, aes_ops);
    mid0 = _mm256_set_m128i(mid0_upper, mid0_lower);
    out[0] = mid0 ^ in;
    if (LWIDTH > 1) {
        AES_ECB_encrypt(mid1_upper, in_upper, leafprgkeys[1].k, aes_ops);
        AES_ECB_encrypt(mid1_lower, in_lower, leafprgkeys[1].k, aes_ops);
        mid1 = _mm256_set_m128i(mid1_upper, mid1_lower);
        out[1] = mid1 ^ in;
    }
    if (LWIDTH > 2) {
        AES_ECB_encrypt(mid2_upper, in_upper, leafprgkeys[2].k, aes_ops);
        AES_ECB_encrypt(mid2_lower, in_lower, leafprgkeys[2].k, aes_ops);
        mid2 = _mm256_set_m128i(mid2_upper, mid2_lower);
        out[2] = mid2 ^ in;
    }
}

// Compute both of the leaf children of node seed
template <size_t LWIDTH>
static void prgboth(std::array<__m256i,LWIDTH> &left,
    std::array<__m256i,LWIDTH> &right, __m256i seed, size_t &aes_ops)
{
    prg<LWIDTH>(left, set_lsb(seed, 0), 0, aes_ops);  // automatically safes to left / right
    prg<LWIDTH>(right, set_lsb(seed, 1), 1, aes_ops);
}

# endif
#endif