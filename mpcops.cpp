#include "mpcops.hpp"
#include "bitutils.hpp"

// P0 and P1 both hold additive shares of x (shares are x0 and x1) and y
// (shares are y0 and y1); compute additive shares of z = x*y =
// (x0+x1)*(y0+y1). x, y, and z are each at most nbits bits long.
//
// Cost:
// 2 words sent in 1 message
// consumes 1 MultTriple
void mpc_mul(MPCTIO &tio, yield_t &yield,
    RegAS &z, RegAS x, RegAS y,
    nbits_t nbits)
{
    const value_t mask = MASKBITS(nbits);
    // Compute z to be an additive share of (x0*y1+y0*x1)
    mpc_cross(tio, yield, z, x, y, nbits);
    // Add x0*y0 (the peer will add x1*y1)
    z.ashare = (z.ashare + x.ashare * y.ashare) & mask;
}

// P0 and P1 both hold additive shares of x (shares are x0 and x1) and y
// (shares are y0 and y1); compute additive shares of z = x0*y1 + y0*x1.
// x, y, and z are each at most nbits bits long.
//
// Cost:
// 2 words sent in 1 message
// consumes 1 MultTriple
void mpc_cross(MPCTIO &tio, yield_t &yield,
    RegAS &z, RegAS x, RegAS y,
    nbits_t nbits)
{
    const value_t mask = MASKBITS(nbits);
    size_t nbytes = BITBYTES(nbits);
    auto [X, Y, Z] = tio.multtriple(yield);

    // Send x+X and y+Y
    value_t blind_x = (x.ashare + X) & mask;
    value_t blind_y = (y.ashare + Y) & mask;

    tio.queue_peer(&blind_x, nbytes);
    tio.queue_peer(&blind_y, nbytes);

    yield();

    // Read the peer's x+X and y+Y
    value_t  peer_blind_x=0, peer_blind_y=0;
    tio.recv_peer(&peer_blind_x, nbytes);
    tio.recv_peer(&peer_blind_y, nbytes);

    z.ashare = ((x.ashare * peer_blind_y) - (Y * peer_blind_x) + Z) & mask;
}

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
    nbits_t nbits, bool tally)
{
    const value_t mask = MASKBITS(nbits);
    size_t nbytes = BITBYTES(nbits);
    auto [X, Z] = tio.halftriple(yield, tally);

    // Send x+X
    value_t blind_x = (x + X) & mask;

    tio.queue_peer(&blind_x, nbytes);

    yield();

    // Read the peer's y+Y
    value_t  peer_blind_y=0;
    tio.recv_peer(&peer_blind_y, nbytes);

    if (tio.player() == 0) {
        z.ashare = ((x * peer_blind_y) + Z) & mask;
    } else if (tio.player() == 1) {
        z.ashare = ((-X * peer_blind_y) + Z) & mask;
    }
}

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
    nbits_t nbits)
{
    const value_t mask = MASKBITS(nbits);

    // Compute additive shares of [(1-2*f0)*y0]*f1 + [(1-2*f1)*y1]*f0
    value_t bs_fval = value_t(f.bshare);
    RegAS fval;
    fval.ashare = bs_fval;
    mpc_cross(tio, yield, z, y*(1-2*bs_fval), fval, nbits);

    // Add f0*y0 (and the peer will add f1*y1)
    z.ashare = (z.ashare + bs_fval*y.ashare) & mask;

    // Now the shares add up to:
    // [(1-2*f0)*y0]*f1 + [(1-2*f1)*y1]*f0 + f0*y0 + f1*y1
    // which you can rearrange to see that it's equal to the desired
    // (f0 + f1 - 2*f0*f1)*(y0+y1), since f0 XOR f1 = (f0 + f1 - 2*f0*f1).
}

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
    nbits_t nbits)
{
    const value_t mask = MASKBITS(nbits);

    // The desired result is z = x + f * (y-x)
    mpc_flagmult(tio, yield, z, f, y-x, nbits);
    z.ashare = (z.ashare + x.ashare) & mask;
}

// P0 and P1 hold bit shares f0 and f1 of the single bit f, and XOR
// shares of the values x and y; compute XOR shares of z, where z = x if
// f=0 and z = y if f=1.  x, y, and z are each at most nbits bits long.
//
// Cost:
// 2 words sent in 1 message
// consumes 1 SelectTriple
void mpc_select(MPCTIO &tio, yield_t &yield,
    RegXS &z, RegBS f, RegXS x, RegXS y,
    nbits_t nbits)
{
    const value_t mask = MASKBITS(nbits);
    size_t nbytes = BITBYTES(nbits);
    // Sign-extend f (so 0 -> 0000...0; 1 -> 1111...1)
    value_t fext = (-value_t(f.bshare)) & mask;

    // Compute XOR shares of f & (x ^ y)
    auto [X, Y, Z] = tio.valselecttriple(yield);

    bit_t blind_f = f.bshare ^ X;
    value_t d = (x.xshare ^ y.xshare) & mask;
    value_t blind_d = (d ^ Y) & mask;

    // Send the blinded values
    tio.queue_peer(&blind_f, sizeof(blind_f));
    tio.queue_peer(&blind_d, nbytes);

    yield();

    // Read the peer's values
    bit_t peer_blind_f = 0;
    value_t peer_blind_d;
    tio.recv_peer(&peer_blind_f, sizeof(peer_blind_f));
    peer_blind_f &= 1;
    tio.recv_peer(&peer_blind_d, nbytes);
    peer_blind_d &= mask;

    // Compute our share of f ? x : y = (f * (x ^ y))^x
    value_t peer_blind_fext = -value_t(peer_blind_f);
    z.xshare = ((fext & peer_blind_d) ^ (Y & peer_blind_fext) ^
            (fext & d) ^ (Z ^ x.xshare)) & mask;
}

// P0 and P1 hold bit shares f0 and f1 of the single bit f, and bit
// shares of the values x and y; compute bit shares of z, where
// z = x if f=0 and z = y if f=1.
//
// Cost:
// 1 byte sent in 1 message
// consumes 1/64 AndTriple
void mpc_select(MPCTIO &tio, yield_t &yield,
    RegBS &z, RegBS f, RegBS x, RegBS y)
{
    // The desired result is z = x ^ (f & (y^x))
    mpc_and(tio, yield, z, f, y^x);
    z ^= x;
}

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
    nbits_t nbits)
{
    const value_t mask = MASKBITS(nbits);

    // Let s = f*(y-x).  Then the desired result is
    // x <- x + s, y <- y - s.
    RegAS s;
    mpc_flagmult(tio, yield, s, f, y-x, nbits);
    x.ashare = (x.ashare + s.ashare) & mask;
    y.ashare = (y.ashare - s.ashare) & mask;
}

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
    nbits_t nbits, bool tally)
{
    const value_t mask = MASKBITS(nbits);

    // We use the fact that for any nbits-bit A and B,
    // A+B = (A XOR B) + 2*(A AND B)  mod 2^nbits
    // so if we have additive shares C0 and C1 of 2*(A AND B)
    // (so C0 + C1 = 2*(A AND B)), then (A-C0) and (B-C1) are
    // additive shares of (A XOR B).

    // To get additive shares of 2*(A AND B) (mod 2^nbits), we first
    // note that we can ignore the top bits of A and B, since the
    // multiplication by 2 will shift it out of the nbits-bit range.
    // For the other bits, use valuemult to get the product of the
    // corresponding bit i of A and B (i=0..nbits-2), and compute
    // C = \sum_i 2^{i+1} * (A_i * B_i).

    // This can all be done in a single message, using the coroutine
    // mechanism to have all nbits-1 instances of valuemult queue their
    // message, then yield, so that all of their messages get sent at
    // once, then each will read their results.

    RegAS as_bitand[nbits-1];
    std::vector<coro_t> coroutines;
    for (nbits_t i=0; i<nbits-1; ++i) {
        coroutines.emplace_back(
            [&tio, &as_bitand, &xs_x, i, nbits, tally](yield_t &yield) {
                mpc_valuemul(tio, yield, as_bitand[i],
                    (xs_x.xshare>>i)&1, nbits, tally);
            });
    }
    run_coroutines(yield, coroutines);
    value_t as_C = 0;
    for (nbits_t i=0; i<nbits-1; ++i) {
        mpz_t tmp;
        mpz_mul_2exp(tmp, as_bitand[i].ashare.get_mpz_t(), i+1);
        as_C = value_t(tmp);
    }
    as_x.ashare = (xs_x.xshare - as_C) & mask;
}

// P0 and P1 hold XOR shares x0 and x1 of x. x is at most nbits bits
// long. Return x to P0 and P1 (and 0 to P2).
//
// Cost: 1 word sent in 1 message
value_t mpc_reconstruct(MPCTIO &tio, yield_t &yield,
    RegXS x, nbits_t nbits)
{
    RegXS res;
    size_t nbytes = BITBYTES(nbits);
    if (tio.player() < 2) {
        tio.queue_peer(&x, nbytes);
        yield();
        tio.recv_peer(&res, nbytes);
        res ^= x;
    } else {
        yield();
    }
    return res.xshare;
}

// P0 and P1 hold additive shares x0 and x1 of x. x is at most nbits
// bits long. Return x to P0 and P1 (and 0 to P2).
//
// Cost: 1 word sent in 1 message
value_t mpc_reconstruct(MPCTIO &tio, yield_t &yield,
    RegAS x, nbits_t nbits)
{
    RegAS res;
    size_t nbytes = BITBYTES(nbits);
    if (tio.player() < 2) {
        tio.queue_peer(&x, nbytes);
        yield();
        tio.recv_peer(&res, nbytes);
        res += x;
    } else {
        yield();
    }
    return res.ashare;
}

// P0 and P1 hold bit shares f0 and f1 of f. Return f to P0 and P1 (and
// 0 to P2).
//
// Cost: 1 word sent in 1 message
bool mpc_reconstruct(MPCTIO &tio, yield_t &yield, RegBS f)
{
    RegBS res;
    if (tio.player() < 2) {
        tio.queue_peer(&f, 1);
        yield();
        tio.recv_peer(&res, 1);
        res ^= f;
    } else {
        yield();
    }
    return res.bshare;
}

// P0 and P1 hold bit shares of f, and DPFnode XOR shares x0,y0 and
// x1,y1 of x and y.  Set z to x=x0^x1 if f=0 and to y=y0^y1 if f=1.
//
// Cost:
// 6 64-bit words sent in 2 messages
// consumes one AndTriple
void mpc_reconstruct_choice(MPCTIO &tio, yield_t &yield,
    DPFnode &z, RegBS f, DPFnode &x, DPFnode &y)
{
    // Sign-extend f (so 0 -> 0000...0; 1 -> 1111...1)
    // DPFnode fext = GMPMasks<VALUE_BITS>::if_mask[f.bshare];

    // Compute XOR shares of f & (x ^ y)

    SelectTriple<DPFnode> triple = tio.nodeselecttriple(yield);
    auto X = triple.X;
    // DPFnode Y(triple.Y);  man kann auch direkt darauf zugreifen
    // DPFnode Z(triple.Z);

    bit_t blind_f = f.bshare ^ X;
    DPFnode d = x ^ y;
    DPFnode blind_d = d ^ triple.Y;


    // Send the blinded values
    tio.queue_peer(&blind_f, sizeof(blind_f));
    tio.queue_peer(blind_d);

    yield();

    // Read the peer's values
    bit_t peer_blind_f = false;
    DPFnode peer_blind_d, tmp;
    tio.recv_peer(&peer_blind_f, sizeof(peer_blind_f));
    tio.recv_peer(tmp);
    peer_blind_d = std::move(tmp);

    // Compute _our share_ of f ? x : y = (f * (x ^ y))^x
    // DPFnode zshare =
    //         (fext & peer_blind_d) ^ (triple.Y & peer_blind_fext) ^
    //         (fext & d) ^ (Z ^ x);
    DPFnode tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, zshare;
    mpz_and(tmp0.get_mpz_t(), GMPMasks<VALUE_BITS>::if_mask[f.bshare].get_mpz_t(), peer_blind_d.get_mpz_t());
    mpz_and(tmp1.get_mpz_t(), triple.Y.get_mpz_t(), GMPMasks<VALUE_BITS>::if_mask[peer_blind_f].get_mpz_t());
    mpz_and(tmp2.get_mpz_t(), GMPMasks<VALUE_BITS>::if_mask[f.bshare].get_mpz_t(), d.get_mpz_t());
    mpz_xor(tmp3.get_mpz_t(), triple.Z.get_mpz_t(), x.get_mpz_t());
    mpz_xor(tmp4.get_mpz_t(), tmp0.get_mpz_t(), tmp1.get_mpz_t());
    mpz_xor(tmp5.get_mpz_t(), tmp2.get_mpz_t(), tmp3.get_mpz_t());
    mpz_xor(zshare.get_mpz_t(), tmp4.get_mpz_t(), tmp5.get_mpz_t());

    // Now exchange shares
    tio.queue_peer(zshare);  // wurde im letzten versuch noch kopiert vor dem versenden

    yield();

    DPFnode peer_zshare;
    tio.recv_peer(peer_zshare);
    // peer_zshare = zshare_tmp;

    std::cout << "Davor" << std::endl;
    z = zshare ^ peer_zshare;
    // mpz_xor(z.get_mpz_t(), zshare.get_mpz_t(), peer_zshare.get_mpz_t());
}

// P0 and P1 hold bit shares of x and y.  Set z to bit shares of x & y.
//
// Cost:
// 1 byte sent in 1 message
// consumes 1/64 AndTriple
void mpc_and(MPCTIO &tio, yield_t &yield,
    RegBS &z, RegBS x, RegBS y)
{
    // Compute XOR shares of x & y
    auto T = tio.bitselecttriple(yield);
    bit_t blind_x = x.bshare ^ T.X;
    bit_t blind_y = y.bshare ^ T.Y;

    // Send the blinded values
    uint8_t v = (blind_x << 1) | blind_y;
    tio.queue_peer(&v, sizeof(v));

    yield();

    // Read the peer's values
    bit_t peer_blind_x = 0;
    bit_t peer_blind_y = 0;
    uint8_t peer_v = 0;
    tio.recv_peer(&peer_v, sizeof(peer_v));
    peer_blind_x = (peer_v >> 1) & 1;
    peer_blind_y = peer_v & 1;

    // Compute our share of x & y
    z.bshare = (x.bshare & peer_blind_y) ^ (T.Y & peer_blind_x) ^
        (x.bshare & y.bshare) ^ T.Z;
}

// P0 and P1 hold bit shares of x and y.  Set z to bit shares of x | y.
//
// Cost:
// 1 byte sent in 1 message
// consumes 1/64 AndTriple
void mpc_or(MPCTIO &tio, yield_t &yield,
    RegBS &z, RegBS x, RegBS y)
{
    if (tio.player() == 0) {
        x.bshare = !x.bshare;
        y.bshare = !y.bshare;
    }
    mpc_and(tio, yield, z, x, y);
    if (tio.player() == 0) {
        z.bshare = !z.bshare;
    }
}
