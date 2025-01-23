#ifndef __MPCOPS_HPP__
#define __MPCOPS_HPP__

#include "types.hpp"
#include "mpcio.hpp"
#include "coroutine.hpp"

// P0 and P1 both hold additive shares of x (shares are x0 and x1) and y
// (shares are y0 and y1); compute additive shares of z = x*y =
// (x0+x1)*(y0+y1). x, y, and z are each at most nbits bits long.
//
// Cost:
// 2 words sent in 1 message
// consumes 1 MultTriple
void mpc_mul(MPCTIO &tio, yield_t &yield,
    RegAS &z, RegAS x, RegAS y,
    nbits_t nbits = VALUE_BITS);

// P0 and P1 both hold additive shares of x (shares are x0 and x1) and y
// (shares are y0 and y1); compute additive shares of z = x0*y1 + y0*x1.
// x, y, and z are each at most nbits bits long.
//
// Cost:
// 2 words sent in 1 message
// consumes 1 MultTriple
void mpc_cross(MPCTIO &tio, yield_t &yield,
    RegAS &z, RegAS x, RegAS y,
    nbits_t nbits = VALUE_BITS);

// P0 holds the (complete) value x, P1 holds the (complete) value y;
// compute additive shares of z = x*y.  x, y, and z are each at most
// nbits bits long.  The parameter is called x, but P1 will pass y
// there.  When called by another task during preprocessing, set tally
// to false so that the required halftriples aren't accounted for
// separately from the main preprocessing task.
//
// Cost:
// 1 word sent in 1 message
// consumes 1 HalfTriple
void mpc_valuemul(MPCTIO &tio, yield_t &yield,
    RegAS &z, value_t x,
    nbits_t nbits = VALUE_BITS, bool tally = true);

// P0 and P1 hold bit shares f0 and f1 of the single bit f, and additive
// shares y0 and y1 of the value y; compute additive shares of
// z = f * y = (f0 XOR f1) * (y0 + y1).  y and z are each at most nbits
// bits long.
//
// Cost:
// 2 words sent in 1 message
// consumes 1 MultTriple
void mpc_flagmult(MPCTIO &tio, yield_t &yield,
    RegAS &z, RegBS f, RegAS y,
    nbits_t nbits = VALUE_BITS);

// P0 and P1 hold bit shares f0 and f1 of the single bit f, and additive
// shares of the values x and y; compute additive shares of z, where
// z = x if f=0 and z = y if f=1.  x, y, and z are each at most nbits
// bits long.
//
// Cost:
// 2 words sent in 1 message
// consumes 1 MultTriple
void mpc_select(MPCTIO &tio, yield_t &yield,
    RegAS &z, RegBS f, RegAS x, RegAS y,
    nbits_t nbits = VALUE_BITS);

// P0 and P1 hold bit shares f0 and f1 of the single bit f, and XOR
// shares of the values x and y; compute XOR shares of z, where z = x if
// f=0 and z = y if f=1.  x, y, and z are each at most nbits bits long.
//
// Cost:
// 2 words sent in 1 message
// consumes 1 SelectTriple
void mpc_select(MPCTIO &tio, yield_t &yield,
    RegXS &z, RegBS f, RegXS x, RegXS y,
    nbits_t nbits = VALUE_BITS);

// P0 and P1 hold bit shares f0 and f1 of the single bit f, and bit
// shares of the values x and y; compute bit shares of z, where
// z = x if f=0 and z = y if f=1.
//
// Cost:
// 1 byte sent in 1 message
// consumes 1/64 AndTriple
void mpc_select(MPCTIO &tio, yield_t &yield,
    RegBS &z, RegBS f, RegBS x, RegBS y);

// P0 and P1 hold bit shares f0 and f1 of the single bit f, and additive
// shares of the values x and y. Obliviously swap x and y; that is,
// replace x and y with new additive sharings of x and y respectively
// (if f=0) or y and x respectively (if f=1).  x and y are each at most
// nbits bits long.
//
// Cost:
// 2 words sent in 1 message
// consumes 1 MultTriple
void mpc_oswap(MPCTIO &tio, yield_t &yield,
    RegAS &x, RegAS &y, RegBS f,
    nbits_t nbits = VALUE_BITS);

// P0 and P1 hold XOR shares of x. Compute additive shares of the same
// x. x is at most nbits bits long.  When called by another task during
// preprocessing, set tally to false so that the required halftriples
// aren't accounted for separately from the main preprocessing task.
//
// Cost:
// nbits-1 words sent in 1 message
// consumes nbits-1 HalfTriples
void mpc_xs_to_as(MPCTIO &tio, yield_t &yield,
    RegAS &as_x, RegXS xs_x,
    nbits_t nbits = VALUE_BITS, bool tally = true);

// P0 and P1 hold XOR shares x0 and x1 of x. x is at most nbits bits
// long. Return x to P0 and P1 (and 0 to P2).
//
// Cost: 1 word sent in 1 message
value_t mpc_reconstruct(MPCTIO &tio, yield_t &yield,
    RegXS f, nbits_t nbits = VALUE_BITS);

// P0 and P1 hold additive shares x0 and x1 of x. x is at most nbits
// bits long. Return x to P0 and P1 (and 0 to P2).
//
// Cost: 1 word sent in 1 message
value_t mpc_reconstruct(MPCTIO &tio, yield_t &yield,
    RegAS x, nbits_t nbits = VALUE_BITS);

// P0 and P1 hold bit shares f0 and f1 of f. Return f to P0 and P1 (and
// 0 to P2).
//
// Cost: 1 word sent in 1 message
bool mpc_reconstruct(MPCTIO &tio, yield_t &yield, RegBS x);

// P0 and P1 hold bit shares of f, and DPFnode XOR shares x0,y0 and
// x1,y1 of x and y.  Set z to x=x0^x1 if f=0 and to y=y0^y1 if f=1.
//
// Cost:
// 6 64-bit words sent in 2 messages
// consumes one AndTriple
void mpc_reconstruct_choice(MPCTIO &tio, yield_t &yield,
    DPFnode &z, RegBS f, DPFnode &x, DPFnode &y);

// As above, but for arrays of DPFnode
//
// Cost:
// 6*LWIDTH 64-bit words sent in 2 messages
// consumes LWIDTH AndTriples
template <size_t LWIDTH>
void mpc_reconstruct_choice(MPCTIO &tio, yield_t &yield,
    std::array<DPFnode,LWIDTH> &z, RegBS f,
    const std::array<DPFnode,LWIDTH> &x,
    const std::array<DPFnode,LWIDTH> &y);

// P0 and P1 hold bit shares of x and y.  Set z to bit shares of x & y.
//
// Cost:
// 1 byte sent in 1 message
// consumes 1/64 AndTriple
void mpc_and(MPCTIO &tio, yield_t &yield,
    RegBS &z, RegBS x, RegBS y);

// P0 and P1 hold bit shares of x and y.  Set z to bit shares of x | y.
//
// Cost:
// 1 byte sent in 1 message
// consumes 1/64 AndTriple
void mpc_or(MPCTIO &tio, yield_t &yield,
    RegBS &z, RegBS x, RegBS y);

#include "mpcops.tcc"

#endif
