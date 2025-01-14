#ifndef TYPES_TCC
#define TYPES_TCC

#ifndef VALUE_BITS
#define VALUE_BITS 64
#include <gmpxx.h>
#endif

#include <gmp.h>

// Values in MPC secret-shared memory are of this type.
// This is the type of the underlying shared value, not the types of the
// shares themselves.
using value_t = mpz_class;

using DPFnode = mpz_class;

#endif