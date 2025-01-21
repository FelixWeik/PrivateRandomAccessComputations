#ifndef __OBLIVDS_TYPES_HPP__
#define __OBLIVDS_TYPES_HPP__

#include <tuple>
#include <vector>
#include <array>
#include <cstdint>
#include <iostream>
#include <ostream>
#include <x86intrin.h>  // SSE and AVX intrinsics
#include <bsd/stdlib.h> // arc4random_buf

#include "bitutils.hpp"

// The number of bits in an MPC secret-shared memory word

#ifndef VALUE_BITS
#define VALUE_BITS 64
#endif

#ifndef INPUT_BITS
#define INPUT_BITS 128
#endif

// how many entries does each input value get (for indices and big register types)
#ifndef INPUT_PARTITION
#define INPUT_PARTITION (INPUT_BITS/VALUE_BITS)
#endif

// Values in MPC secret-shared memory are of this type.
// This is the type of the underlying shared value, not the types of the
// shares themselves.

#if VALUE_BITS == 64
using value_t = uint64_t;
#elif VALUE_BITS == 32
using value_t = uint32_t;
#else
#error "Unsupported value of VALUE_BITS"
#endif

// Secret-shared bits are of this type.  Note that it is standards
// compliant to treat a bool as an unsigned integer type with values 0
// and 1.

using bit_t = bool;

// Counts of the number of bits in a value are of this type, which must
// be large enough to store the _value_ VALUE_BITS
using nbits_t = uint8_t;

// Convert a number of bits to the number of bytes required to store (or
// more to the point, send) them.
#define BITBYTES(nbits) (((nbits)+7)>>3)

// A mask of this many bits; the test is to prevent 1<<nbits from
// overflowing if nbits == VALUE_BITS
#define MASKBITS(nbits) (((nbits) < VALUE_BITS) ? (value_t(1)<<(nbits))-1 : ~0)

// The type of register holding an additive share of a value
struct RegAS {
    value_t ashare;

    RegAS() : ashare(0) {}

    inline value_t share() const { return ashare; }
    void set(value_t s) { ashare = s; }

    // Set each side's share to a random value nbits bits long
    void randomize(size_t nbits = VALUE_BITS) {
        value_t mask = MASKBITS(nbits);
        arc4random_buf(&ashare, sizeof(ashare));  // fills ashare pseudorandomly with sizeof(ashare) bits
        ashare &= mask;
    }

    void test(RegAS astest) const {
        std::cout << "==== TEST RegAS ====" << std::endl;
        std::cout << "test_share = " << astest.ashare << std::endl;
        bool res = astest.ashare == this->ashare;
        std::cout << "equal = " << res << std::endl;
        std::cout << "==== TEST RegAS ====" << std::endl;
    }

    RegAS &operator+=(const RegAS &rhs) {
        this->ashare += rhs.ashare;
        return *this;
    }

    RegAS operator+(const RegAS &rhs) const {
        RegAS res = *this;
        res += rhs;
        return res;
    }

    RegAS &operator-=(const RegAS &rhs) {
        this->ashare -= rhs.ashare;
        return *this;
    }

    RegAS operator-(const RegAS &rhs) const {
        RegAS res = *this;
        res -= rhs;
        return res;
    }

    RegAS operator-() const {
        RegAS res = *this;
        res.ashare = -res.ashare;
        return res;
    }

    RegAS &operator*=(value_t rhs) {
        this->ashare *= rhs;
        return *this;
    }

    RegAS operator*(value_t rhs) const {
        RegAS res = *this;
        res *= rhs;
        return res;
    }

    RegAS &operator<<=(nbits_t shift) {
        this->ashare <<= shift;
        return *this;
    }

    RegAS operator<<(nbits_t shift) const {
        RegAS res = *this;
        res <<= shift;
        return res;
    }

    // Multiply a scalar by a vector
    template <size_t N>
    std::array<RegAS,N> operator*(std::array<value_t,N> rhs) const {
        std::array<RegAS,N> res;
        for (size_t i=0;i<N;++i) {
            res[i] = *this;
            res[i] *= rhs[i];
        }
        return res;
    }

    RegAS &operator&=(value_t mask) {
        this->ashare &= mask;
        return *this;
    }

    RegAS operator&(value_t mask) const {
        RegAS res = *this;
        res &= mask;
        return res;
    }

    // Multiply by the local share of the argument, not multiplcation of
    // two shared values (two versions)
    RegAS &mulshareeq(const RegAS &rhs) {
        *this *= rhs.ashare;
        return *this;
    }
    inline RegAS mulshare(const RegAS &rhs) const {
        RegAS res = *this;
        res *= rhs.ashare;
        return res;
    }

    void dump() const {
        printf("%016lx\n", ashare);
    }
};

inline value_t combine(const RegAS &A, const RegAS &B,
        nbits_t nbits = VALUE_BITS) {
    value_t mask = ~0;
    if (nbits < VALUE_BITS) {
        mask = (value_t(1)<<nbits)-1;
    }
    return (A.ashare + B.ashare) & mask;
}

// The type of register holding a bit-share
struct RegBS {
    bit_t bshare;

    RegBS() : bshare(0) {}

    inline bit_t share() const { return bshare; }
    inline void set(bit_t s) { bshare = s; }

    // Set each side's share to a random bit
    inline void randomize() {
        unsigned char randb;
        arc4random_buf(&randb, sizeof(randb));
        bshare = randb & 1;
    }

    inline RegBS &operator^=(const RegBS &rhs) {
        this->bshare ^= rhs.bshare;
        return *this;
    }

    inline RegBS operator^(const RegBS &rhs) const {
        RegBS res = *this;
        res ^= rhs;
        return res;
    }

    inline RegBS &operator^=(const bit_t &rhs) {
        this->bshare ^= rhs;
        return *this;
    }

    inline RegBS operator^(const bit_t &rhs) const {
        RegBS res = *this;
        res ^= rhs;
        return res;
    }
};

// The type of register holding an XOR share of a value
struct RegXS {
    value_t xshare;

    RegXS() : xshare(0) {}
    RegXS(const RegBS &b) { xshare = b.bshare ? ~0 : 0; }

    inline value_t share() const { return xshare; }
    inline void set(value_t s) { xshare = s; }

    // Set each side's share to a random value nbits bits long
    void randomize(size_t nbits = VALUE_BITS) {
        value_t mask = MASKBITS(nbits);
        arc4random_buf(&xshare, sizeof(xshare));
        xshare &= mask;
    }

    void test(RegXS xreg) const {
        std::cout << "==== TEST RegXS ====" << std::endl;
        std::cout << "test_share = " << xreg.xshare << std::endl;
        bool res = xreg.xshare == this->xshare;
        std::cout << "equal = " << res << std::endl;
        std::cout << "==== TEST RegXS ====" << std::endl;
    }

    // For RegXS, + and * should be interpreted bitwise; that is, + is
    // really XOR and * is really AND.  - is also XOR (the same as +).

    // We also include actual XOR operators for convenience

    RegXS &operator+=(const RegXS &rhs) {
        this->xshare ^= rhs.xshare;
        return *this;
    }

    RegXS operator+(const RegXS &rhs) const {
        RegXS res = *this;
        res += rhs;
        return res;
    }

    RegXS &operator-=(const RegXS &rhs) {
        this->xshare ^= rhs.xshare;
        return *this;
    }

    RegXS operator-(const RegXS &rhs) const {
        RegXS res = *this;
        res -= rhs;
        return res;
    }

    RegXS operator-() const {
        RegXS res = *this;
        return res;
    }

    RegXS &operator*=(value_t rhs) {
        this->xshare &= rhs;
        return *this;
    }

    RegXS operator*(value_t rhs) const {
        RegXS res = *this;
        res *= rhs;
        return res;
    }

    // Multiply a scalar by a vector
    template <size_t N>
    std::array<RegXS,N> operator*(std::array<value_t,N> rhs) const {
        std::array<RegXS,N> res;
        for (size_t i=0;i<N;++i) {
            res[i] = *this;
            res[i] *= rhs[i];
        }
        return res;
    }

    RegXS &operator^=(const RegXS &rhs) {
        this->xshare ^= rhs.xshare;
        return *this;
    }

    RegXS operator^(const RegXS &rhs) const {
        RegXS res = *this;
        res ^= rhs;
        return res;
    }

    RegXS &operator&=(value_t mask) {
        this->xshare &= mask;
        return *this;
    }

    RegXS operator&(value_t mask) const {
        RegXS res = *this;
        res &= mask;
        return res;
    }

    // Bit shifting and bit-extraction

    RegXS &operator<<=(nbits_t shift) {
        this->xshare <<= shift;
        return *this;
    }

    RegXS operator<<(nbits_t shift) const {
        RegXS res = *this;
        res <<= shift;
        return res;
    }

    RegXS &operator>>=(nbits_t shift) {
        this->xshare >>= shift;
        return *this;
    }

    RegXS operator>>(nbits_t shift) const {
        RegXS res = *this;
        res >>= shift;
        return res;
    }

    RegBS bitat(nbits_t pos) const {
        RegBS bs;
        bs.set(!!(this->xshare & (value_t(1)<<pos)));
        return bs;
    }

    // Multiply by the local share of the argument, not multiplcation of
    // two shared values (two versions)
     RegXS &mulshareeq(const RegXS &rhs) {
        *this *= rhs.xshare;
        return *this;
    }

    [[nodiscard]] RegXS mulshare(const RegXS &rhs) const {
        RegXS res = *this;
        res *= rhs.xshare;
        return res;
    }

    void dump() const {
        printf("%016lx\n", xshare);
    }

    // Extract a bit share of bit bitnum of the XOR-shared register
    [[nodiscard]] RegBS bit(nbits_t bitnum) const {
        RegBS bs;
        bs.bshare = !!(xshare & (value_t(1)<<bitnum));
        return bs;
    }
};

inline value_t combine(const RegXS &A, const RegXS &B,
        const nbits_t nbits = VALUE_BITS) {
    value_t mask = ~0;
    if (nbits < VALUE_BITS) {
        mask = (value_t(1)<<nbits)-1;
    }
    return (A.xshare ^ B.xshare) & mask;
}

// Represents an INPUT_PARTITION multiple of RegAS
struct BigAS {
    RegAS ashares[INPUT_PARTITION];

    BigAS() {
        for (auto & i : ashares) {
            i.set(0);
        }
    }

    explicit BigAS(const RegAS input[]) {
        for (size_t i=0;i<INPUT_PARTITION;++i) {
            this->ashares[i] = input[i];
        }
    }

    explicit BigAS (const std::vector<RegAS> &in) {
        for (int i = 0; i < INPUT_PARTITION; ++i) {
            this->ashares[i] = in[i];
        }
    }

    void set(size_t pos, RegAS &val) {
        ashares[pos].set(val.ashare);
    }

    RegAS* share() {
        return ashares;
    }

    void randomize(const size_t nbits = VALUE_BITS) {
        for (RegAS& i : ashares) {
            i.randomize(nbits);
        }
    }

    void test(const BigAS astest) const {
        std::cout << "==== TEST BigAS ====" << std::endl;
        for (int i=0; i<INPUT_PARTITION; i++) {
            this -> ashares[i].test(astest.ashares[i]);
        }
        std::cout << "==== TEST BigAS ====" << std::endl;
    }

    RegAS &operator[](const size_t i) {
        return ashares[i];
    }

    BigAS &operator+=(const BigAS &rhs) {
        for (int i = 0; i < INPUT_PARTITION; i++) {
            this->ashares[i] += rhs.ashares[i];
        }
        return *this;
    }

    BigAS operator+(const BigAS &rhs) const {
        BigAS res = *this;
        res += rhs;
        return res;
    }

    BigAS &operator-=(const BigAS &rhs) {
        for (int i = 0; i < INPUT_PARTITION; i++) {
            this -> ashares[i] -= rhs.ashares[i];
        }
        return *this;
    }

    BigAS operator-(const BigAS &rhs) const {
        BigAS res = *this;
        res -= rhs;
        return res;
    }

    BigAS operator-() const {
        BigAS res = *this;
        for (auto & i : res.ashares) {
            i = -i;
        }
        return res;
    }

    BigAS &operator*=(const value_t &rhs) {
        value_t carry = 0;
        for (auto & i : this->ashares) {
            value_t product = i.ashare * rhs + carry;

            i.set(product & 0xFFFFFFFFFFFFFFFF);
            carry = product >> 64;
        }
        return *this;
    }

    BigAS operator*(value_t rhs) const {
        BigAS res = *this;
        res *= rhs;
        return res;
    }

    static BigAS mult(const BigAS &rhs, const BigAS &lhs) {
        BigAS res;
        std::array<value_t, 2 * INPUT_PARTITION> product_blocks = {0};

        for (size_t i = 0; i < INPUT_PARTITION; ++i) {
            for (size_t j = 0; j < INPUT_PARTITION; ++j) {
                product_blocks[i + j] +=
                    static_cast<value_t>(lhs.ashares[i].ashare) *
                    static_cast<value_t>(rhs.ashares[j].ashare);

                if (product_blocks[i + j] >= (1ULL << 64)) {
                    product_blocks[i + j + 1] += product_blocks[i + j] >> 64;
                    product_blocks[i + j] &= 0xFFFFFFFFFFFFFFFF;
                }
            }
        }

        for (size_t i = 0; i < INPUT_PARTITION; ++i) {
            res.ashares[i].set(product_blocks[i]);
        }

        return res;
    }

    BigAS &operator<<=(nbits_t shift) {
        if (shift == 0) return *this;
        constexpr size_t BITS_PER_BLOCK = 64;
        value_t carry = 0;

        for (auto & i : ashares) {
            const value_t current = i.ashare;
            i.ashare = (current << shift) | carry;
            carry = (current >> (BITS_PER_BLOCK - shift));
        }
        return *this;
    }

    BigAS operator<<(nbits_t shift) const {
        BigAS res = *this;
        res <<= shift;
        return res;
    }

    BigAS &operator>>=(nbits_t shift) {
        if (shift == 0) return *this;
        value_t carry = 0;
        for (size_t i = INPUT_PARTITION; i-- > 0;) {
            constexpr size_t BITS_PER_BLOCK = 64;
            value_t current = ashares[i].ashare;
            ashares[i].ashare = (current >> shift) | carry;
            carry = (current << (BITS_PER_BLOCK - shift));
        }
        return *this;
    }

    BigAS operator>>(nbits_t shift) const {
        BigAS res = *this;
        res >>= shift;
        return res;
    }

    BigAS &operator&=(value_t * mask) {
        for(int i = 0; i < INPUT_PARTITION; ++i) {
            this -> ashares[i] &= mask[i];
        }
        return *this;
    }

    BigAS operator&(value_t * mask) const {
        BigAS res = *this;
        res &= mask;
        return res;
    }

    BigAS &mulshareeq(const RegAS &rhs) {
        for (auto & i : ashares) {
            i *= rhs.ashare;
        }
        return *this;
    }

    [[nodiscard]] BigAS mulshare(const RegAS &rhs) const {
        BigAS res = *this;
        for (auto & i : res.ashares) {
            i *= rhs.ashare;
        }
        return res;
    }

    void dump() const {
        for (const RegAS &i: this -> ashares) {
            i.dump();
        }
    }
};

inline value_t* combine(const BigAS &A, const BigAS &B, nbits_t nbits = VALUE_BITS) {
    static value_t result[INPUT_PARTITION];
    value_t mask = ~0;

    if (nbits < VALUE_BITS) {
        mask = (static_cast<value_t>(1) << nbits) - 1;
    }

    for (size_t i = 0; i < INPUT_PARTITION; ++i) {
        result[i] = (A.ashares[i].ashare + B.ashares[i].ashare) & mask;
    }

    return result;
}


struct BigXS {
    RegXS xshares[INPUT_PARTITION];

    BigXS() {
        for (auto & i : xshares) {
            i.set(0);
        }
    }

    explicit BigXS(const RegXS* input) {
        for (size_t i=0;i<INPUT_PARTITION;i++) {
            this->xshares[i] = input[i];
        }
    }

    void randomize(size_t nbits = VALUE_BITS) {
        for (RegXS &i : this -> xshares) {
            i.randomize(nbits);
        }
    }

    RegXS &operator[](value_t index) {
        return xshares[index];
    }

    BigXS &set(value_t value, uint8_t pos = 0) { // datentyp von pos bei bedarf anpassen
        xshares[pos].set(value);
        return *this;
    }

    // + and * are executed bitwise => no bit-shifting necessary
    BigXS &operator+=(const BigXS &rhs) {
        for (size_t i = 0;i<INPUT_PARTITION;i++) {
            xshares[i] += rhs.xshares[i];
        }
        return *this;
    }

    BigXS operator+(BigXS &rhs) const {
        BigXS res = *this;
        for (size_t i = 0;i<INPUT_PARTITION;i++) {
            res[i] += rhs[i];
        }
        return res;
    }

    BigXS &operator-=(const BigXS &rhs) {
        for (size_t i = 0;i<INPUT_PARTITION;i++) {
            this -> xshares[i] -= rhs.xshares[i];
        }
        return *this;
    }

    BigXS operator-(BigXS &rhs) const {
        BigXS res = *this;
        for (size_t i = 0;i<INPUT_PARTITION;i++) {
            res[i] -= rhs[i];
        }
        return res;
    }

    BigXS operator-() const {
        BigXS res = *this;
        for (auto &i: res.xshares) {
            i = -i;
        }
        return res;
    }

    BigXS &operator*=(value_t rhs) {
        for (auto & xshare : this -> xshares) {
            xshare *= rhs;
        }
        return *this;
    }

    BigXS operator*(value_t *rhs) const {

        BigXS res = *this;
        for (size_t i = 0;i<INPUT_PARTITION;i++) {
            res[i] *= rhs[i];
        }
        return res;
    }

    BigXS &operator^=(const BigXS &rhs) {
        for (size_t i = 0;i<INPUT_PARTITION;i++) {
            this -> xshares[i] ^= rhs.xshares[i];
        }
        return *this;
    }

    BigXS operator^(BigXS &rhs) const {
        auto res = *this;
        for (size_t i = 0;i<INPUT_PARTITION;i++) {
            res[i] ^= rhs.xshares[i];
        }
        return res;
    }

    BigXS &operator&=(value_t * mask) {
        for (size_t i = 0;i<INPUT_PARTITION;i++) {
            this->xshares[i] &= mask[i];
        }
        return *this;
    }

    BigXS operator&(value_t * mask) const {
        BigXS res = *this;
        for(size_t i = 0;i<INPUT_PARTITION;i++) {
            res[i] &= mask[i];
        }
        return res;
    }

    BigXS &operator<<=(nbits_t shift) {
        for(auto &i: this -> xshares) {
            i <<= shift;
        }
        return *this;
    }

    BigXS operator<<(nbits_t shift) const {
        BigXS res = *this;
        for(auto &i: res.xshares) {
            i <<= shift;
        }
        return res;
    }

    BigXS &operator>>=(nbits_t shift) {
        for(auto &i: this -> xshares) {
            i >>= shift;
        }
        return *this;
    }

    BigXS operator>>(nbits_t shift) const {
        BigXS res = *this;
        for(auto &i: res.xshares) {
            i >>= shift;
        }
        return res;
    }

    [[nodiscard]] RegBS bitat(nbits_t pos0, nbits_t pos1) const {
        return xshares[pos0].bitat(pos1);
    }

    BigXS &mulshareeq(const BigXS &rhs) {
        for (size_t i = 0;i<INPUT_PARTITION;i++) {
            this -> xshares[i].mulshareeq(rhs.xshares[i]);
        }
        return *this;
    }

    [[nodiscard]] BigXS mulshare(const BigXS &rhs) const {
        BigXS res = *this;
        for (size_t i = 0; i < INPUT_PARTITION; i++) {
            res[i] = res[i].mulshare(rhs.xshares[i]);
        }
        return res;
    }

    void dump() const {
        std::cout << "==== BigXS ====" << std::endl;
        for (auto &i : xshares) {
            i.dump();
        }
        std::cout << "==== BigXS ====" << std::endl;
    }

    [[nodiscard]] RegBS bit(nbits_t pos, nbits_t bitnum) const {
        return xshares[pos].bit(bitnum);
    }
};

// Aims to represent a set of indices of size INPUT_PARTITION
struct IndexAS {
    BigAS indexChain;

    explicit IndexAS(RegAS start) {
        indexChain = BigAS();
        for (value_t i = 0;i<INPUT_PARTITION;i++) {
            RegAS cur{};
            cur.set(start.ashare + i);
            indexChain[i] = cur;
        }
    }

    // chain _must_ be of size INPUT_PARTITION
    explicit IndexAS(BigAS chain): indexChain(chain) {}

    explicit IndexAS() {
        for (size_t i = 0; i < INPUT_PARTITION; i++) {
            RegAS cur{};
            cur.randomize();
            indexChain[i] = cur;
        }
    }

    // can be used in MemRefInd constructor to execute
    // independent operations on all indices
    [[nodiscard]] std::vector<RegAS> getVector() const {
        std::vector<RegAS> res;
        for (RegAS index : indexChain.ashares) {
            res.push_back(index);
        }
        return res;
    }

    void dump() const {
        std::cout << "==== IndexAS ====" << std::endl;
        for (const RegAS i: indexChain.ashares) {
            i.dump();
        }
        std::cout << "==== IndexAS ====" << std::endl;
    }
};

struct IndexXS {
    BigXS indexChain;

    explicit IndexXS(RegXS start) {
        indexChain = BigXS();
        for (value_t i = 0;i<INPUT_PARTITION;i++) {
            RegXS cur{};
            cur.set(start.xshare + i);
            indexChain[i] = cur;
        }
    }

    // chain _must_ be of size INPUT_PARTITION
    explicit IndexXS(BigXS chain): indexChain(chain) {}

    // produces a random set of indizes
    explicit IndexXS() {
        for (size_t i = 0; i < INPUT_PARTITION; i++) {
            RegXS cur{};
            cur.randomize();
            indexChain[i] = cur;
        }
    }

    // can be used in MemRefInd constructor to execute
    // independent operations on all indices
    [[nodiscard]] std::vector<RegXS> getVector() const {
        std::vector<RegXS> res;
        for (RegXS index : indexChain.xshares) {
            res.emplace_back(index);
        }
        return res;
    }

    void dump() const {
        std::cout << "==== IndexXS ====" << std::endl;
        for (const RegXS i: indexChain.xshares) {
            i.dump();
        }
        std::cout << "==== IndexXS ====" << std::endl;
    }
};

// Enable templates to specialize on just the basic types RegAS and
// RegXS.  Technique from
// https://stackoverflow.com/questions/2430039/one-template-specialization-for-multiple-classes

template <bool B> struct prac_template_bool_type {};
using prac_template_true = prac_template_bool_type<true>;
using prac_template_false = prac_template_bool_type<false>;
template <typename T>
struct prac_basic_Reg_S : prac_template_false
{
    static const bool value = false;
};
template<>
struct prac_basic_Reg_S<RegAS>: prac_template_true
{
    static const bool value = true;
};
template<>
struct prac_basic_Reg_S<RegXS>: prac_template_true
{
    static const bool value = true;
};

// Some useful operations on tuples, vectors, and arrays of the above
// types

template <typename T>
std::tuple<T,T> operator+=(std::tuple<T,T> &A,
    const std::tuple<T,T> &B)
{
    std::get<0>(A) += std::get<0>(B);
    std::get<1>(A) += std::get<1>(B);
    return A;
}

template <typename T>
std::tuple<T,T> operator+=(const std::tuple<T&,T&> &A,
    const std::tuple<T,T> &B)
{
    std::get<0>(A) += std::get<0>(B);
    std::get<1>(A) += std::get<1>(B);
    return A;
}

template <typename T>
std::tuple<T,T> operator+(const std::tuple<T,T> &A,
    const std::tuple<T,T> &B)
{
    auto res = A;
    res += B;
    return res;
}

template <typename T>
std::tuple<T,T> operator-=(const std::tuple<T&,T&> &A,
    const std::tuple<T,T> &B)
{
    std::get<0>(A) -= std::get<0>(B);
    std::get<1>(A) -= std::get<1>(B);
    return A;
}

template <typename T>
std::tuple<T,T> operator-=(std::tuple<T,T> &A,
    const std::tuple<T,T> &B)
{
    std::get<0>(A) -= std::get<0>(B);
    std::get<1>(A) -= std::get<1>(B);
    return A;
}

template <typename T>
std::tuple<T,T> operator-(const std::tuple<T,T> &A,
    const std::tuple<T,T> &B)
{
    auto res = A;
    res -= B;
    return res;
}

template <typename T>
std::tuple<T,T> operator*=(const std::tuple<T&,T&> &A,
    const std::tuple<value_t,value_t> &B)
{
    std::get<0>(A) *= std::get<0>(B);
    std::get<1>(A) *= std::get<1>(B);
    return A;
}

template <typename T>
std::tuple<T,T> operator*=(std::tuple<T,T> &A,
    const std::tuple<value_t,value_t> &B)
{
    std::get<0>(A) *= std::get<0>(B);
    std::get<1>(A) *= std::get<1>(B);
    return A;
}

template <typename T>
std::tuple<T,T> operator*(const std::tuple<T,T> &A,
    const std::tuple<value_t,value_t> &B)
{
    auto res = A;
    res *= B;
    return res;
}

template <typename T, size_t N>
std::tuple<std::array<T,N>,std::array<T,N>> operator*(
    const std::tuple<T,T> &A,
    const std::tuple<std::array<value_t,N>,std::array<value_t,N>> &B)
{
    std::tuple<std::array<T,N>,std::array<T,N>> res;
    std::get<0>(res) = std::get<0>(A) * std::get<0>(B);
    std::get<1>(res) = std::get<1>(A) * std::get<1>(B);
    return res;
}

template <typename S, size_t N>
inline std::array<value_t,N> combine(const std::array<S,N> &A,
        const std::array<S,N> &B,
        nbits_t nbits = VALUE_BITS) {
    std::array<value_t,N> res;
    for (size_t i=0;i<N;++i) {
        res[i] = combine(A[i], B[i], nbits);
    }
    return res;
}

template <typename S, size_t N>
inline std::tuple<std::array<value_t,N>,std::array<value_t,N>>
    combine(const std::tuple<std::array<S,N>,std::array<S,N>> &A,
        const std::tuple<std::array<S,N>,std::array<S,N>> &B,
        nbits_t nbits = VALUE_BITS) {
    return std::make_tuple(
        combine(std::get<0>(A), std::get<0>(B), nbits),
        combine(std::get<1>(A), std::get<1>(B), nbits));
}

template <typename T>
std::tuple<T,T,T> operator+=(const std::tuple<T&,T&,T&> &A,
    const std::tuple<T,T,T> &B)
{
    std::get<0>(A) += std::get<0>(B);
    std::get<1>(A) += std::get<1>(B);
    std::get<2>(A) += std::get<2>(B);
    return A;
}

template <typename T>
std::tuple<T,T,T> operator+=(std::tuple<T,T,T> &A,
    const std::tuple<T,T,T> &B)
{
    std::get<0>(A) += std::get<0>(B);
    std::get<1>(A) += std::get<1>(B);
    std::get<2>(A) += std::get<2>(B);
    return A;
}

template <typename T>
std::tuple<T,T,T> operator+(const std::tuple<T,T,T> &A,
    const std::tuple<T,T,T> &B)
{
    auto res = A;
    res += B;
    return res;
}

template <typename T>
std::tuple<T,T,T> operator-=(const std::tuple<T&,T&,T&> &A,
    const std::tuple<T,T,T> &B)
{
    std::get<0>(A) -= std::get<0>(B);
    std::get<1>(A) -= std::get<1>(B);
    std::get<2>(A) -= std::get<2>(B);
    return A;
}

template <typename T>
std::tuple<T,T,T> operator-=(std::tuple<T,T,T> &A,
    const std::tuple<T,T,T> &B)
{
    std::get<0>(A) -= std::get<0>(B);
    std::get<1>(A) -= std::get<1>(B);
    std::get<2>(A) -= std::get<2>(B);
    return A;
}

template <typename T>
std::tuple<T,T,T> operator-(const std::tuple<T,T,T> &A,
    const std::tuple<T,T,T> &B)
{
    auto res = A;
    res -= B;
    return res;
}

template <typename T>
std::tuple<T,T,T> operator*=(const std::tuple<T&,T&,T&> &A,
    const std::tuple<value_t,value_t,value_t> &B)
{
    std::get<0>(A) *= std::get<0>(B);
    std::get<1>(A) *= std::get<1>(B);
    std::get<2>(A) *= std::get<2>(B);
    return A;
}

template <typename T>
std::tuple<T,T,T> operator*=(std::tuple<T,T,T> &A,
    const std::tuple<value_t,value_t,value_t> &B)
{
    std::get<0>(A) *= std::get<0>(B);
    std::get<1>(A) *= std::get<1>(B);
    std::get<2>(A) *= std::get<2>(B);
    return A;
}

template <typename T>
std::tuple<T,T,T> operator*(const std::tuple<T,T,T> &A,
    const std::tuple<value_t,value_t,value_t> &B)
{
    auto res = A;
    res *= B;
    return res;
}

template <typename T, size_t N>
std::tuple<std::array<T,N>,std::array<T,N>,std::array<T,N>> operator*(
    const std::tuple<T,T,T> &A,
    const std::tuple<std::array<value_t,N>,std::array<value_t,N>,std::array<value_t,N>> &B)
{
    std::tuple<std::array<T,N>,std::array<T,N>,std::array<T,N>> res;
    std::get<0>(res) = std::get<0>(A) * std::get<0>(B);
    std::get<1>(res) = std::get<1>(A) * std::get<1>(B);
    std::get<2>(res) = std::get<2>(A) * std::get<2>(B);
    return res;
}

inline std::vector<RegAS> operator-(const std::vector<RegAS> &A)
{
    std::vector<RegAS> res;
    for (const auto &v : A) {
        res.push_back(-v);
    }
    return res;
}

inline std::vector<RegXS> operator-(const std::vector<RegXS> &A)
{
    return A;
}

inline std::vector<RegBS> operator-(const std::vector<RegBS> &A)
{
    return A;
}

template <size_t N>
inline std::vector<RegAS> operator-(const std::array<RegAS,N> &A)
{
    std::vector<RegAS> res;
    for (const auto &v : A) {
        res.push_back(-v);
    }
    return res;
}

template <size_t N>
inline std::array<RegXS,N> operator-(const std::array<RegXS,N> &A)
{
    return A;
}

template <size_t N>
inline std::array<RegBS,N> operator-(const std::array<RegBS,N> &A)
{
    return A;
}

template <typename S, size_t N>
inline std::array<S,N> &operator+=(std::array<S,N> &A, const std::array<S,N> &B)
{
    for (size_t i=0;i<N;++i) {
        A[i] += B[i];
    }
    return A;
}

template <typename S, size_t N>
inline std::array<S,N> &operator-=(std::array<S,N> &A, const std::array<S,N> &B)
{
    for (size_t i=0;i<N;++i) {
        A[i] -= B[i];
    }
    return A;
}

template <typename S, size_t N>
inline std::array<S,N> &operator^=(std::array<S,N> &A, const std::array<S,N> &B)
{
    for (size_t i=0;i<N;++i) {
        A[i] ^= B[i];
    }
    return A;
}

// XOR the bit B into the low bit of A
template <typename S, size_t N>
inline std::array<S,N> &xor_lsb(std::array<S,N> &A, bit_t B)
{
    A[0] ^= lsb128_mask[B];
    return A;
}

template <typename S, size_t N>
inline std::tuple<std::array<value_t,N>,std::array<value_t,N>,std::array<value_t,N>>
    combine(
        const std::tuple<std::array<S,N>,std::array<S,N>,std::array<S,N>> &A,
        const std::tuple<std::array<S,N>,std::array<S,N>,std::array<S,N>> &B,
        nbits_t nbits = VALUE_BITS) {
    return std::make_tuple(
        combine(std::get<0>(A), std::get<0>(B), nbits),
        combine(std::get<1>(A), std::get<1>(B), nbits),
        combine(std::get<2>(A), std::get<2>(B), nbits));
}


// The _maximum_ number of bits in an MPC address; the actual size of
// the memory will typically be set at runtime, but it cannot exceed
// this value.  It is more efficient (in terms of communication) in some
// places for this value to be at most 32.

#ifndef ADDRESS_MAX_BITS
#define ADDRESS_MAX_BITS 32
#endif

// Addresses of MPC secret-shared memory are of this type

#if ADDRESS_MAX_BITS <= 32
using address_t = uint32_t;
#elif ADDRESS_MAX_BITS <= 64
using address_t = uint64_t;
#else
#error "Unsupported value of ADDRESS_MAX_BITS"
#endif

#if ADDRESS_MAX_BITS > VALUE_BITS
#error "VALUE_BITS must be at least as large as ADDRESS_MAX_BITS"
#endif

// A multiplication triple is a triple (X0,Y0,Z0) held by P0 (and
// correspondingly (X1,Y1,Z1) held by P1), with all values random,
// but subject to the relation that X0*Y1 + Y0*X1 = Z0+Z1

using MultTriple = std::tuple<value_t, value_t, value_t>;
// The *Name structs are a way to get strings representing the names of
// the types as would be given to preprocessing to create them in
// advance.
struct MultTripleName { static constexpr const char *name = "m"; };

// A half-triple is (X0,Z0) held by P0 (and correspondingly (Y1,Z1) held
// by P1), with all values random, but subject to the relation that
// X0*Y1 = Z0+Z1

using HalfTriple = std::tuple<value_t, value_t>;
struct HalfTripleName { static constexpr const char *name = "h"; };

// An AND triple is a triple (X0,Y0,Z0) held by P0 (and correspondingly
// (X1,Y1,Z1) held by P1), with all values random, but subject to the
// relation that X0&Y1 ^ Y0&X1 = Z0^Z1

using AndTriple = std::tuple<value_t, value_t, value_t>;
struct AndTripleName { static constexpr const char *name = "a"; };

// The type of nodes in a DPF.  This must be at least as many bits as
// the security parameter, and at least twice as many bits as value_t.

using DPFnode = __m128i;

// XOR the bit B into the low bit of A
inline DPFnode &xor_lsb(DPFnode &A, bit_t B)
{
    A = _mm_xor_si128(A, lsb128_mask[B]);
    return A;
}

// A Select triple for type V (V is DPFnode, value_t, or bit_t) is a
// triple of (X0,Y0,Z0) where X0 is a bit and Y0 and Z0 are Vs held by
// P0 (and correspondingly (X1,Y1,Z1) held by P1), with all values
// random, but subject to the relation that (X0*Y1) ^ (Y0*X1) = Z0^Z1.
// This is a struct instead of a tuple for alignment reasons.

template <typename V>
struct SelectTriple {
    bit_t X;
    V Y, Z;
};
// Of the three options for V, we only ever store V = value_t
struct ValSelectTripleName { static constexpr const char *name = "s"; };

// These are defined in rdpf.hpp, but declared here to avoid cyclic
// header dependencies.
template <nbits_t WIDTH> struct RDPFPair;
struct RDPFPairName { static constexpr const char *name = "r"; };
// Incremental RDPFs
struct IRDPFPairName { static constexpr const char *name = "i"; };
template <nbits_t WIDTH> struct RDPFTriple;
struct RDPFTripleName { static constexpr const char *name = "r"; };
// Incremental RDPFs
struct IRDPFTripleName { static constexpr const char *name = "i"; };
struct CDPF;
struct CDPFName { static constexpr const char *name = "c"; };

// We want the I/O (using << and >>) for many classes
// to just be a common thing: write out the bytes
// straight from memory

#define DEFAULT_IO(CLASSNAME)                  \
    template <typename T>                      \
    T& operator>>(T& is, CLASSNAME &x)         \
    {                                          \
        is.read((char *)&x, sizeof(x));        \
        return is;                             \
    }                                          \
                                               \
    template <typename T>                      \
    T& operator<<(T& os, const CLASSNAME &x)   \
    {                                          \
        os.write((const char *)&x, sizeof(x)); \
        return os;                             \
    }

// Default I/O for various types

DEFAULT_IO(DPFnode)
DEFAULT_IO(RegBS)
DEFAULT_IO(RegAS)
DEFAULT_IO(RegXS)
DEFAULT_IO(MultTriple)
DEFAULT_IO(HalfTriple)
// We don't need one for AndTriple because it's exactly the same type as
// MultTriple

// I/O for arrays
template <typename T, typename S, size_t N>
T& operator>>(T& is, std::array<S,N> &x)
{
    for (size_t i=0;i<N;++i) {
        is >> x[i];
    }
    return is;
}

template <typename T, typename S, size_t N>
T& operator<<(T& os, const std::array<S,N> &x)
{
    for (size_t i=0;i<N;++i) {
        os << x[i];
    }
    return os;
}

// I/O for SelectTriples
template <typename T, typename V>
T& operator>>(T& is, SelectTriple<V> &x)
{
    is.read((char *)&x.X, sizeof(x.X));
    is.read((char *)&x.Y, sizeof(x.Y));
    is.read((char *)&x.Z, sizeof(x.Z));
    return is;
}

template <typename T, typename V>
T& operator<<(T& os, const SelectTriple<V> &x)
{
    os.write((const char *)&x.X, sizeof(x.X));
    os.write((const char *)&x.Y, sizeof(x.Y));
    os.write((const char *)&x.Z, sizeof(x.Z));
    return os;
}

// And for pairs and triples

#define DEFAULT_TUPLE_IO(CLASSNAME)                                  \
    template <typename T>                                            \
    T& operator>>(T& is, std::tuple<CLASSNAME, CLASSNAME> &x)        \
    {                                                                \
        is >> std::get<0>(x) >> std::get<1>(x);                      \
        return is;                                                   \
    }                                                                \
                                                                     \
    template <typename T>                                            \
    T& operator<<(T& os, const std::tuple<CLASSNAME, CLASSNAME> &x)  \
    {                                                                \
        os << std::get<0>(x) << std::get<1>(x);                      \
        return os;                                                   \
    }                                                                \
                                                                     \
    template <typename T>                                            \
    T& operator>>(T& is, std::tuple<CLASSNAME, CLASSNAME, CLASSNAME> &x) \
    {                                                                \
        is >> std::get<0>(x) >> std::get<1>(x) >> std::get<2>(x);    \
        return is;                                                   \
    }                                                                \
                                                                     \
    template <typename T>                                            \
    T& operator<<(T& os, const std::tuple<CLASSNAME, CLASSNAME, CLASSNAME> &x) \
    {                                                                \
        os << std::get<0>(x) << std::get<1>(x) << std::get<2>(x);    \
        return os;                                                   \
    }

DEFAULT_TUPLE_IO(RegAS)
DEFAULT_TUPLE_IO(RegXS)

// And for pairs and triples of arrays

template <typename T, typename S, size_t N>
T& operator>>(T& is, std::tuple<std::array<S,N>, std::array<S,N>> &x)
{
    is >> std::get<0>(x) >> std::get<1>(x);
    return is;
}

template <typename T, typename S, size_t N>
T& operator<<(T& os, const std::tuple<std::array<S,N>, std::array<S,N>> &x)
{
    os << std::get<0>(x) << std::get<1>(x);
    return os;
}

template <typename T, typename S, size_t N>
T& operator>>(T& is, std::tuple<std::array<S,N>, std::array<S,N>, std::array<S,N>> &x)
{
    is >> std::get<0>(x) >> std::get<1>(x) >> std::get<2>(x);
    return is;
}

template <typename T, typename S, size_t N>
T& operator<<(T& os, const std::tuple<std::array<S,N>, std::array<S,N>, std::array<S,N>> &x)
{
    os << std::get<0>(x) << std::get<1>(x) << std::get<2>(x);
    return os;
}

enum ProcessingMode {
    MODE_ONLINE,        // Online mode, after preprocessing has been done
    MODE_PREPROCESSING, // Preprocessing mode
    MODE_ONLINEONLY     // Online-only mode, where all computations are
};                      // done online

#endif
