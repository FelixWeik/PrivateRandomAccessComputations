/* Adapted from preprocessing/bitutils.h from
 * https://git-crysp.uwaterloo.ca/avadapal/duoram by Adithya Vadapalli,
 * itself adapted from code by Ryan Henry */

#ifndef __BITUTILS_HPP__
#define __BITUTILS_HPP__

#include <array>
#include <cstdint>
#include <x86intrin.h>  // SSE and AVX intrinsics
#include <vector>
#include <gmpxx.h>

#include "types.tcc"

template <size_t N>
class GMPMasks {
public:
    static const mpz_class bool_mask[2];
    static const std::vector<mpz_class> lsb_mask;
    static const std::vector<mpz_class> lsb_mask_inv;
    static const mpz_class if_mask[2];

private:
    static mpz_class create_mask(size_t bit_count) {
        mpz_class mask;
        mpz_init2(mask.get_mpz_t(), N);
        for (size_t i = 0; i < bit_count; ++i) {
            mpz_setbit(mask.get_mpz_t(), i);
        }
        return mask;
    }

    static mpz_class create_inverse_mask(size_t bit_count) {
        mpz_class mask;
        mpz_init2(mask.get_mpz_t(), N);
        for (size_t i = 0; i < N; ++i) {
            mpz_setbit(mask.get_mpz_t(), i);
        }
        for (size_t i = 0; i < bit_count; ++i) {
            mpz_clrbit(mask.get_mpz_t(), i);
        }
        return mask;
    }
};

template <size_t N>
const mpz_class GMPMasks<N>::bool_mask[2] = {
    create_mask(1),                        // 0b00...0001
    create_mask(64)                        // 0b00...0001 << 64
};

template <size_t N>
const std::vector<mpz_class> GMPMasks<N>::lsb_mask = [] {
    std::vector<mpz_class> masks(4);
    masks[0] = create_mask(0);             // 0b00...0000
    masks[1] = create_mask(1);             // 0b00...0001
    masks[2] = create_mask(2);             // 0b00...0010
    masks[3] = create_mask(3);             // 0b00...0011
    return masks;
}();

template <size_t N>
const std::vector<mpz_class> GMPMasks<N>::lsb_mask_inv = [] {
    std::vector<mpz_class> masks(4);
    masks[0] = create_inverse_mask(0);     // 0b11...1111
    masks[1] = create_inverse_mask(1);     // 0b11...1110
    masks[2] = create_inverse_mask(2);     // 0b11...1101
    masks[3] = create_inverse_mask(3);     // 0b11...1100
    return masks;
}();

template <size_t N>
const mpz_class GMPMasks<N>::if_mask[2] = {
    create_mask(0),                        // 0b00...0000
    create_mask(N)                                 // 0b11...1111
};

template <unsigned int N>
mpz_class xor_if(const mpz_class& block1, const mpz_class& block2, bool flag) {
    if (flag) {
        return block1 ^ block2;
    }
    return 0;
}

template <unsigned int N, size_t LWIDTH>
inline std::array<mpz_class, LWIDTH> xor_if(
    const std::array<mpz_class, LWIDTH>& block1,
    const std::array<mpz_class, LWIDTH>& block2,
    bool flag
) {
    const mpz_class& mask = GMPMasks<N>::if_mask[flag ? 1 : 0];
    std::array<mpz_class, LWIDTH> res;
    for (size_t j = 0; j < LWIDTH; ++j) {
        res[j] = xor_if(block1[j], block2[j], mask);
    }
    return res;
}


// populates out with a random value of size nbits
inline void random_mpz(mpz_class &out, size_t nbits = VALUE_BITS, int seed = 42) {
    gmp_randstate_t state;
    gmp_randinit_default(state);
    gmp_randseed_ui(state, seed);
    mpz_urandomb(out.get_mpz_t(), state, nbits);
    gmp_randclear(state);
}

inline mpz_class random_mpz(size_t nbits = VALUE_BITS, int seed = 42) {
    mpz_class res;
    gmp_randstate_t state;
    gmp_randinit_default(state);
    gmp_randseed_ui(state, seed);
    mpz_urandomb(res.get_mpz_t(), state, nbits);
    gmp_randclear(state);
    return res;
}

inline __m128i random_m128i() {
    uint64_t lo, hi;
    asm volatile ("rdrand %0" : "=r" (lo));
    asm volatile ("rdrand %0" : "=r" (hi));
    return _mm_set_epi64x(hi, lo);
}

inline __m128i mpz_to_m128i(const mpz_class& value) {
    size_t count;
    size_t num_bytes = mpz_sizeinbase(value.get_mpz_t(), 10);
    std::vector<unsigned char> buffer(num_bytes);
    mpz_export(buffer.data(), &count, 1, 1, 0, 0, value.get_mpz_t());

    std::vector<__m128i> result;
    size_t num_chunks = (buffer.size() + 15) / 16;

    result.resize(num_chunks, _mm_setzero_si128());

    for (size_t i = 0; i < num_chunks; i++) {
        unsigned char tmp[16] = {0};
        size_t chunk_size = std::min<size_t>(16, buffer.size() - i * 16);
        memcpy(tmp, buffer.data() + i * 16, chunk_size);
        result[i] = _mm_loadu_si128(reinterpret_cast<__m128i *>(tmp));
    }

    return result[0];
}

inline std::vector<__m128i> mpz_to_m128i_vec(const mpz_class &value) {
    size_t count;
    size_t num_bytes = mpz_sizeinbase(value.get_mpz_t(), 10);
    std::vector<unsigned char> buffer(num_bytes);
    mpz_export(buffer.data(), &count, 1, 1, 0, 0, value.get_mpz_t());

    std::vector<__m128i> result;
    size_t num_chunks = (buffer.size() + 15) / 16;

    result.resize(num_chunks, _mm_setzero_si128());

    for (size_t i = 0; i < num_chunks; i++) {
        unsigned char tmp[16] = {0};
        size_t chunk_size = std::min<size_t>(16, buffer.size() - i * 16);
        memcpy(tmp, buffer.data() + i * 16, chunk_size);
        result[i] = _mm_loadu_si128(reinterpret_cast<__m128i *>(tmp));
    }

    return result;
}

inline mpz_class m128i_to_mpz_class(__m128i vec) {
    uint64_t values[2];
    _mm_storeu_si128(reinterpret_cast<__m128i*>(values), vec);
    return mpz_class(values[0]) << 64 | mpz_class(values[1]);
}


inline uint8_t get_lsb(const mpz_class& block) {
    return mpz_tstbit(block.get_mpz_t(), 0) != 0;
}

inline mpz_class clear_lsb(const mpz_class& block, uint8_t bits = 0b01) {
    mpz_class result;
    mpz_t mask;
    mpz_init_set_ui(mask, bits);
    mpz_and(result.get_mpz_t(), block.get_mpz_t(), mask);
    mpz_clear(mask);
    return result;
}

inline mpz_class set_lsb(const mpz_class& block, bool val = true) {
    mpz_class result = block;
    clear_lsb(result, 0b01);
    mpz_class mask = val ? 0b01 : 0b00;
    result |= mask;
    return result;
}


inline uint8_t parity(const mpz_class& block) {
    mpz_t temp;
    mpz_init(temp);
    mpz_set(temp, block.get_mpz_t());
    size_t popcount = mpz_popcount(temp);
    mpz_clear(temp);
    return popcount & 1;
}

template <unsigned int BIT_SIZE>
inline uint8_t parity_above(const mpz_class& block, uint8_t position) {
    uint64_t low = mpz_get_ui(block.get_mpz_t());
    uint64_t high = 0;

    if (BIT_SIZE > 64) {
        mpz_class high_bits = block >> 64;
        high = mpz_get_ui(high_bits.get_mpz_t());
    }
    uint64_t mask = uint64_t(1) << position;
    mask |= (mask - 1);
    mask = ~mask;

    if (position >= 64) {
        return (__builtin_popcountll(high & mask) & 1);
    } else {
        return ((__builtin_popcountll(high) + __builtin_popcountll(low & mask)) & 1);
    }
}


// Return the parity of the number of the number of bits set in block
// strictly below the given position
template <unsigned int BIT_SIZE>
inline uint8_t parity_below(const mpz_class& block, uint8_t position) {
    uint64_t low = mpz_get_ui(block.get_mpz_t());
    uint64_t high = 0;

    if (BIT_SIZE > 64) {
        mpz_class high_bits = block >> 64;
        high = mpz_get_ui(high_bits.get_mpz_t());
    }
    uint64_t mask = uint64_t(1) << position;
    mask = ~mask;

    if (position >= 64) {
        return ((__builtin_popcountll(low) + __builtin_popcountll(high & mask)) & 1);
    } else {
        return (__builtin_popcountll(low & mask) & 1);
    }
}

// Return the bit at the given position in block
inline uint8_t bit_at(const mpz_class& block, uint8_t position) {
    uint64_t value = mpz_get_ui(block.get_mpz_t());
    if (position >= 64) {
        uint64_t high = mpz_get_ui(block.get_mpz_t() + 1);
        return !!(high & (uint64_t(1) << (position - 64)));
    } else {
        return !!(value & (uint64_t(1) << position));
    }
}

static const __m128i bool128_mask[2] = {
    _mm_set_epi64x(0,1),                                        // 0b00...0001
    _mm_set_epi64x(1,0)                                         // 0b00...0001 << 64
};

static const __m128i lsb128_mask[4] = {
    _mm_setzero_si128(),                                        // 0b00...0000
    _mm_set_epi64x(0,1),                                        // 0b00...0001
    _mm_set_epi64x(0,2),                                        // 0b00...0010
    _mm_set_epi64x(0,3)                                         // 0b00...0011
};

static const __m128i lsb128_mask_inv[4] = {
    _mm_set1_epi8(-1),                                          // 0b11...1111
    _mm_set_epi64x(-1,-2),                                      // 0b11...1110
    _mm_set_epi64x(-1,-3),                                      // 0b11...1101
    _mm_set_epi64x(-1,-4)                                       // 0b11...1100
};

static const __m128i if128_mask[2] = {
    _mm_setzero_si128(),                                        // 0b00...0000
    _mm_set1_epi8(-1)                                           // 0b11...1111
};

inline __m128i xor_if(const __m128i & block1, const __m128i & block2, __m128i flag)
{
    return _mm_xor_si128(block1, _mm_and_si128(block2, flag));
}

inline mpz_class xor_if(const mpz_class & block1, const mpz_class & block2, bool flag){
    mpz_class temp_block2;
    mpz_class flag_mpz = flag ? 1 : 0;
    temp_block2 = block2 & flag_mpz;

    mpz_class res;
    res = block1 & temp_block2;
    return res;
}

inline __m128i xor_if(const __m128i & block1, const __m128i & block2, bool flag)
{
    return _mm_xor_si128(block1, _mm_and_si128(block2, if128_mask[flag ? 1 : 0]));
}

template <size_t LWIDTH>
inline std::array<__m128i,LWIDTH> xor_if(
    const std::array<__m128i,LWIDTH> & block1,
    const std::array<__m128i,LWIDTH> & block2, bool flag)
{
    std::array<__m128i,LWIDTH> res;
    for (size_t j=0;j<LWIDTH;++j) {
        res[j] = xor_if(block1[j], block2[j], flag);
    }
    return res;
}

template <size_t LWIDTH>
inline std::array<DPFnode, LWIDTH> xor_if(
    const std::array<DPFnode, LWIDTH> &block1,
    const std::array<DPFnode, LWIDTH> &block2, bool flag)
{
    std::array<DPFnode,LWIDTH> res;
    for (size_t j=0;j<LWIDTH;++j) {
        res[j] = xor_if(block1[j], block2[j], flag);
    }
    return res;
}

inline uint8_t get_lsb(const __m128i & block, uint8_t bits = 0b01)
{
    __m128i vcmp = _mm_xor_si128(_mm_and_si128(block, lsb128_mask[bits]), lsb128_mask[bits]);
    return static_cast<uint8_t>(_mm_testz_si128(vcmp, vcmp));
}

template <size_t LWIDTH>
inline uint8_t get_lsb(const std::array<__m128i,LWIDTH> & block)
{
    return get_lsb(block[0]);
}

template <size_t LWIDTH>
inline uint8_t get_lsb(const std::array<DPFnode,LWIDTH> & block) {
    return get_lsb(block[0]);
}

inline __m128i clear_lsb(const __m128i & block, uint8_t bits = 0b01)
{
    return _mm_and_si128(block, lsb128_mask_inv[bits]);
}

inline __m128i set_lsb(const __m128i & block, const bool val = true)
{
    return _mm_or_si128(clear_lsb(block, 0b01), lsb128_mask[val ? 0b01 : 0b00]);
}

// The following can probably be improved by someone who knows the SIMD
// instruction sets better than I do.

// Return the parity of the number of bits set in block; that is, 1 if
// there are an odd number of bits set in block; 0 if even
inline uint8_t parity(const __m128i & block)
{
    uint64_t low = uint64_t(_mm_cvtsi128_si64x(block));
    uint64_t high = uint64_t(_mm_cvtsi128_si64x(_mm_srli_si128(block,8)));
    return ((__builtin_popcountll(low) ^ __builtin_popcountll(high)) & 1);
}

// Return the parity of the number of the number of bits set in block
// strictly above the given position
inline uint8_t parity_above(const __m128i &block, uint8_t position)
{
    uint64_t high = uint64_t(_mm_cvtsi128_si64x(_mm_srli_si128(block,8)));
    if (position >= 64) {
        uint64_t mask = (uint64_t(1)<<(position-64));
        mask |= (mask-1);
        mask = ~mask;
        return (__builtin_popcountll(high & mask) & 1);
    } else {
        uint64_t low = uint64_t(_mm_cvtsi128_si64x(block));
        uint64_t mask = (uint64_t(1)<<position);
        mask |= (mask-1);
        mask = ~mask;
        return ((__builtin_popcountll(high) +
            __builtin_popcountll(low & mask)) & 1);
    }
}

// Return the parity of the number of the number of bits set in block
// strictly below the given position
inline uint8_t parity_below(const __m128i &block, uint8_t position)
{
    uint64_t low = uint64_t(_mm_cvtsi128_si64x(block));
    if (position >= 64) {
        uint64_t high = uint64_t(_mm_cvtsi128_si64x(_mm_srli_si128(block,8)));
        uint64_t mask = (uint64_t(1)<<(position-64))-1;
        return ((__builtin_popcountll(low) +
            __builtin_popcountll(high & mask)) & 1);
    } else {
        uint64_t mask = (uint64_t(1)<<position)-1;
        return (__builtin_popcountll(low & mask) & 1);
    }
}

// Return the bit at the given position in block
inline uint8_t bit_at(const __m128i &block, uint8_t position)
{
    if (position >= 64) {
        uint64_t high = uint64_t(_mm_cvtsi128_si64x(_mm_srli_si128(block,8)));
        return !!(high & (uint64_t(1)<<(position-64)));
    } else {
        uint64_t low = uint64_t(_mm_cvtsi128_si64x(block));
        return !!(low & (uint64_t(1)<<position));
    }
}

inline mpz_class deserialize_from_binary(const char* serialized, size_t len) {
    mpz_class value;
    mpz_import(value.get_mpz_t(), len, 1, sizeof(char), 0, 0, serialized);
    return value;
}

inline void serialize_to_binary(const mpz_class& value, size_t& len, std::vector<char> &buf) {
    len = mpz_sizeinbase(value.get_mpz_t(), 10);
    buf.resize(len);
    mpz_export(buf.data(), &len, 1, sizeof(char), 0, 0, value.get_mpz_t());
}

inline void serialize_halftriple(const std::tuple<mpz_class, mpz_class>& halftriple, size_t& len, std::vector<char> &buf) {
    const auto& [first, second] = halftriple;

    std::vector<char> buf0, buf1;
    size_t len_first = 0, len_second = 0;
    serialize_to_binary(first, len_first, buf0);
    serialize_to_binary(second, len_second, buf1);
    std::vector<char> vec(sizeof(size_t));
    std::memcpy(vec.data(), &len_first, sizeof(size_t));
    buf.insert(buf.end(),vec.begin(),vec.end());

    buf.insert(buf.end(),buf0.begin(),buf0.end());
    std::vector<char> vec1(sizeof(size_t));
    std::memcpy(vec1.data(), &len_second, sizeof(size_t));

    buf.insert(buf.end(),vec1.begin(),vec1.end());
    buf.insert(buf.end(),buf1.begin(),buf1.end());

    len = len_first + len_second;
}

inline std::tuple<mpz_class, mpz_class> deserialize_halftriple(const char* serialized, size_t len) {
    // Längen der beiden Werte extrahieren
    size_t len_first = 0, len_second = 0;
    std::memcpy(&len_first, serialized, sizeof(size_t));
    std::memcpy(&len_second, serialized + sizeof(size_t) + len_first, sizeof(size_t));

    // Daten für `first` und `second` extrahieren
    const char* data_first = serialized + sizeof(size_t);
    const char* data_second = serialized + sizeof(size_t) * 2 + len_first;

    mpz_class first = deserialize_from_binary(data_first, len_first);
    mpz_class second = deserialize_from_binary(data_second, len_second);

    return std::make_tuple(first, second);
}


#endif
