#ifndef __PRG_HPP__
#define __PRG_HPP__

#include <array>

#include "bitutils.hpp"
#include "aes.hpp"
#include "types.hpp"

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

// Compute one of the children of node seed; whichchild=0 for
// the left child, 1 for the right child
// cost: 1 AES encryption
static inline void prg(__m128i &out, __m128i seed, bool whichchild,
    size_t &aes_ops)
{
    __m128i in = set_lsb(seed, whichchild);
    __m128i mid;
    AES_ECB_encrypt(mid, set_lsb(seed, whichchild), prgkey.k, aes_ops);
    out = _mm_xor_si128(mid, in);
}

static void prg(DPFnode &out, const DPFnode& seed, bool whichchild, size_t &aes_ops) {
    auto vec = split_dpfnode_to_m128i<LABEL_BLOCKS>(seed);
    std::array<__m128i, LABEL_BLOCKS> res_vec{};

    for (size_t i = 0; i < LABEL_BLOCKS; i++) {
        __m128i elmt = vec[i];
        __m128i outi;
        prg(outi, elmt, whichchild, aes_ops);
        res_vec[i] = outi;
    }

    out = combine_m128i_to_dpfnode<LABEL_BLOCKS>(res_vec);
}

// Compute both children of node seed
static void prgboth(DPFnode &left, DPFnode &right, const DPFnode &seed, size_t &aes_ops) {
    auto seed_vec = split_dpfnode_to_m128i<LABEL_BLOCKS>(seed);
    std::array<__m128i, LABEL_BLOCKS> left_vec{}, right_vec{};

    for (size_t i = 0; i < LABEL_BLOCKS; i++) {
        __m128i inl = set_lsb(seed_vec[i], 0);
        __m128i inr = set_lsb(seed_vec[i], 1);
        __m128i midl, midr;
        AES_ECB_encrypt(midl, inl, prgkey.k, aes_ops);
        AES_ECB_encrypt(midr, inr, prgkey.k, aes_ops);
        left_vec[i] = _mm_xor_si128(midl, inl);
        right_vec[i] = _mm_xor_si128(midr, inr);
    }

    left = combine_m128i_to_dpfnode<LABEL_BLOCKS>(left_vec);
    right = combine_m128i_to_dpfnode<LABEL_BLOCKS>(right_vec);
}

// Compute one of the leaf children of node seed; whichchild=0 for
// the left child, 1 for the right child
template <size_t LWIDTH>
static void prg(std::array<DPFnode, LWIDTH> &out, const DPFnode &seed, bool whichchild, size_t &aes_ops) {
    auto seed_vec = split_dpfnode_to_m128i<LWIDTH>(seed);

    std::vector<std::array<__m128i, LWIDTH>> small_leafs;

    for (__m128i &seed_elmt : seed_vec) {
        std::array<__m128i, LWIDTH> small_leaf{};
        std::array<__m128i, LWIDTH> res;

        __m128i in = set_lsb(seed_elmt, whichchild);
        __m128i mid0, mid1, mid2;
        AES_ECB_encrypt(mid0, set_lsb(seed_elmt, whichchild), leafprgkeys[0].k, aes_ops);
        if (LWIDTH > 1) {
            AES_ECB_encrypt(mid1, set_lsb(seed_elmt, whichchild), leafprgkeys[1].k, aes_ops);
        }
        if (LWIDTH > 2) {
            AES_ECB_encrypt(mid2, set_lsb(seed_elmt, whichchild), leafprgkeys[2].k, aes_ops);
        }
        res[0] = _mm_xor_si128(mid0, in);
        if (LWIDTH > 1) {
            res[1] = _mm_xor_si128(mid1, in);
        }
        if (LWIDTH > 2) {
            res[2] = _mm_xor_si128(mid2, in);
        }

        small_leafs.push_back(res);
    }

    std::vector<std::vector<__m128i>> extracted_elements(LWIDTH);

    for (size_t i = 0; i < LWIDTH; i++) {
        for (const auto& arr : small_leafs) {
            extracted_elements[i].push_back(arr[i]);
        }
    }

    std::array<DPFnode, LWIDTH> res;
    for (size_t i = 0; i < LWIDTH; i++) {
        std::array<__m128i, LWIDTH> elmts{};
        for (size_t j = 0; j < LWIDTH; j++) {
            elmts[j] = extracted_elements[i][j];
        }
        res[i] = combine_m128i_to_dpfnode<LWIDTH>(elmts);
    }
}

// Compute one of the leaf children of node seed; whichchild=0 for
// the left child, 1 for the right child
template <size_t LWIDTH>
static inline void prg(std::array<__m128i,LWIDTH> &out,
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
    out[0] = _mm_xor_si128(mid0, in);
    if (LWIDTH > 1) {
        out[1] = _mm_xor_si128(mid1, in);
    }
    if (LWIDTH > 2) {
        out[2] = _mm_xor_si128(mid2, in);
    }
}

template <size_t LWIDTH>
static void prgboth(std::array<DPFnode,LWIDTH> &left,
    std::array<DPFnode,LWIDTH> &right, const DPFnode& seed, size_t &aes_ops) {
    auto seed_vec = split_dpfnode_to_m128i<LWIDTH>(seed);

    std::vector<std::array<__m128i, LWIDTH>> left_res{}, right_res{};

    for (__m128i &seed_elmt : seed_vec) {
        std::array<__m128i, LWIDTH> left_elmt, right_elmt;
        prgboth(left_elmt, right_elmt, seed_elmt, aes_ops);
        left_res.push_back(left_elmt);
        right_res.push_back(right_elmt);
    }

    std::vector<std::vector<__m128i>> extracted_elements_left(LWIDTH), extracted_elements_right(LWIDTH);

    for (size_t i = 0; i < LWIDTH; ++i) {
        for (const auto& arr : left_res) {
            extracted_elements_left[i].push_back(arr[i]);
        }
        for (const auto& arr : right_res) {
            extracted_elements_right[i].push_back(arr[i]);
        }
    }

    std::array<DPFnode,LWIDTH> left_leaf, right_leaf;
    for (size_t i = 0; i < LWIDTH; i++) {
        std::array<__m128i, LWIDTH> elmts_left{}, elmts_right{};
        for (size_t j = 0; j < LWIDTH; j++) {
            elmts_left[j] = extracted_elements_left[i][j];
            elmts_right[j] = extracted_elements_right[i][j];
        }
        left_leaf[i] = combine_m128i_to_dpfnode<LWIDTH>(elmts_left);
        right_leaf[i] = combine_m128i_to_dpfnode<LWIDTH>(elmts_right);
    }
    right = right_leaf;
    left = left_leaf;
}

// Compute both of the leaf children of node seed
template <size_t LWIDTH>
static inline void prgboth(std::array<__m128i,LWIDTH> &left,
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
    left[0] = _mm_xor_si128(midl0, inl);
    right[0] = _mm_xor_si128(midr0, inr);
    if (LWIDTH > 1) {
        left[1] = _mm_xor_si128(midl1, inl);
        right[1] = _mm_xor_si128(midr1, inr);
    }
    if (LWIDTH > 2) {
        left[2] = _mm_xor_si128(midl2, inl);
        right[2] = _mm_xor_si128(midr2, inr);
    }
}

#endif
