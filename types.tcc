#ifndef TYPES_TCC
#define TYPES_TCC

#include <gmp.h>

// Basic Wrapper for a GMP (mpz_t) value
// Can be templated to match certain size
template <unsigned int BIT_SIZE>
struct value_wrapper {
    mpz_t value{};

    value_wrapper() {
        mpz_init2(value, BIT_SIZE);
    }

    value_wrapper(int x) {
        mpz_init_set_si(value, x);
    }

    value_wrapper(const value_wrapper& other) {
        mpz_init2(value, BIT_SIZE);
        mpz_set(value, other.value);
    }

    value_wrapper& operator=(const value_wrapper& other) {
        if (this != &other) {
            mpz_set(value, other.value);
        }
        return *this;
    }

    value_wrapper& operator=(int x) {
        mpz_set_si(value, x);
        return *this;
    }

    ~value_wrapper() {
        mpz_clear(value);
    }

    value_wrapper operator+(const value_wrapper& other) const {
        value_wrapper result;
        mpz_add(result.value, this->value, other.value);
        return result;
    }

    value_wrapper operator-(const value_wrapper& other) const {
        value_wrapper result;
        mpz_sub(result.value, this->value, other.value);
        return result;
    }

    value_wrapper operator*(const value_wrapper& other) const {
        value_wrapper result;
        mpz_mul(result.value, this->value, other.value);
        return result;
    }

    value_wrapper operator/(const value_wrapper& other) const {
        value_wrapper result;
        mpz_fdiv_q(result.value, this->value, other.value);
        return result;
    }

    value_wrapper operator%(const value_wrapper& other) const {
        value_wrapper result;
        mpz_mod(result.value, this->value, other.value);
        return result;
    }

    bool operator==(const value_wrapper& other) const {
        return mpz_cmp(this->value, other.value) == 0;
    }

    bool operator!=(const value_wrapper& other) const {
        return mpz_cmp(this->value, other.value) != 0;
    }

    bool operator<(const value_wrapper& other) const {
        return mpz_cmp(this->value, other.value) < 0;
    }

    bool operator>(const value_wrapper& other) const {
        return mpz_cmp(this->value, other.value) > 0;
    }

    bool operator<=(const value_wrapper& other) const {
        return mpz_cmp(this->value, other.value) <= 0;
    }

    bool operator>=(const value_wrapper& other) const {
        return mpz_cmp(this->value, other.value) >= 0;
    }

    friend std::ostream& operator<<(std::ostream& os, const value_wrapper& val) {
        char* str = mpz_get_str(nullptr, 10, val.value);
        os << str;
        free(str);
        return os;
    }

    value_wrapper operator<<(unsigned int shift) const {
        value_wrapper result;
        mpz_mul_2exp(result.value, this->value, shift);
        return result;
    }

    value_wrapper operator>>(unsigned int shift) const {
        value_wrapper result;
        mpz_fdiv_q_2exp(result.value, this->value, shift);
        return result;
    }

    value_wrapper& operator*=(const value_wrapper& other) {
        mpz_mul(this->value, this->value, other.value);
        return *this;
    }

    value_wrapper& operator<<=(unsigned int shift) {
        mpz_mul_2exp(this->value, this->value, shift);
        return *this;
    }

    value_wrapper& operator>>=(unsigned int shift) {
        mpz_fdiv_q_2exp(this->value, this->value, shift);
        return *this;
    }

    value_wrapper& operator^=(const value_wrapper& other) {
        mpz_xor(this->value, this->value, other.value);
        return *this;
    }

    value_wrapper& operator&=(const value_wrapper& other) {
        mpz_and(this->value, this->value, other.value);
        return *this;
    }

    value_wrapper operator&(const value_wrapper& other) const {
        value_wrapper result;
        mpz_and(result.value, this->value, other.value);
        return result;
    }

    value_wrapper& operator+=(const value_wrapper& other) {
        mpz_add(this->value, this->value, other.value);
        return *this;
    }

    value_wrapper& operator-=(const value_wrapper& other) {
        mpz_sub(this->value, this->value, other.value);
        return *this;
    }

    void dump() const {
        char* str = mpz_get_str(nullptr, 10, this->value);
        std::cout << "value: " << str << std::endl;
        free(str);
    }

    value_wrapper operator^(const value_wrapper& other) const {
        value_wrapper result;
        mpz_xor(result.value, this->value, other.value);
        return result;
    }

    value_wrapper operator-() const {
        value_wrapper result;
        mpz_neg(result.value, this->value);
        return result;
    }

    bool operator!() const {
        return mpz_cmp_ui(this->value, 0) == 0;
    }

    value_wrapper& operator|=(const value_wrapper& other) {
        mpz_ior(this->value, this->value, other.value);
        return *this;
    }

    operator bool() const {
        return mpz_cmp_ui(this->value, 0) != 0;
    }

    operator unsigned int() const {
        return mpz_get_ui(this->value);
    }

    value_wrapper& operator%=(const value_wrapper& other) {
        mpz_mod(this->value, this->value, other.value);
        return *this;
    }

    bool operator==(unsigned int val) const {
        value_wrapper temp(val);
        return *this == temp;
    }

    bool operator!=(unsigned int val) const {
        value_wrapper temp(val);
        return *this != temp;
    }

    bool operator<(unsigned int val) const {
        value_wrapper temp(val);
        return *this < temp;
    }

    bool operator>(unsigned int val) const {
        value_wrapper temp(val);
        return *this > temp;
    }

    bool operator<=(unsigned int val) const {
        value_wrapper temp(val);
        return *this <= temp;
    }

    bool operator>=(unsigned int val) const {
        value_wrapper temp(val);
        return *this >= temp;
    }

    value_wrapper operator&(unsigned int val) const {
        value_wrapper temp(val);
        value_wrapper result;
        mpz_and(result.value, this->value, temp.value);
        return result;
    }

    value_wrapper& operator&=(unsigned int val) {
        value_wrapper temp(val);
        mpz_and(this->value, this->value, temp.value);
        return *this;
    }

    value_wrapper operator^(bool b) const {
        value_wrapper result;
        uint64_t bool_as_int = b ? 1 : 0;
        mpz_t temp;
        mpz_init(temp);
        mpz_set_ui(temp, bool_as_int);
        mpz_xor(result.value, value, temp);
        mpz_clear(temp);

        return result;
    }
};

#endif