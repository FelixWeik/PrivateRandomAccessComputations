#ifndef __CDPF_HPP__
#define __CDPF_HPP__

#include <tuple>

#include "mpcio.hpp"
#include "coroutine.hpp"
#include "types.hpp"
#include "dpf.hpp"

// DPFs for doing comparisons of (typically) 64-bit values. We use the
// technique from:
//
// Kyle Storrier, Adithya Vadapalli, Allan Lyons, Ryan Henry.
// Grotto: Screaming fast (2 + 1)-PC for Z_{2^n} via (2, 2)-DPFs
// https://eprint.iacr.org/2023/108
//
// The idea is that we have a pair of DPFs with 64-bit inputs and a
// single-bit output.  The outputs of these DPFs are the same for all
// 64-bit inputs x except for one special one (target), where they're
// different, but if you have just one of the DPFs, you can't tell what
// the value of target is.  The construction of the DPF is a binary
// tree, where each interior node has a 128-bit value, the low bit of
// which is the "flag" bit.  The invariant is that if a node is on the
// path leading to the target, then not only are the two 128-bit values
// on the node (one from each DPF) different, but their flag (low) bits
// are themselves different, and if a node is not on the path leading to
// the target, then its 128-bit value is the _same_ in the two DPFs.
// Each DPF also comes with an additive share (target0 or target1) of
// the random target value.
//
// Given additive shares x0 and x1 of x, two parties can determine
// bitwise shares of whether x>0 as follows: exchange (target0-x0) and
// (target1-x1); both sides add them to produce S = (target-x) = (target0-x0)+(target1-x1).
// Notionally consider (but do not actually construct) a bit vector V of
// length 2^64 with 1s at positions S+1, S+2, ..., S+(2^63-1), wrapping
// around if the indices exceed 2^64-1.  Now consider (but again do not
// actually do) the dot product of V with the full evaluation of the
// DPFs.  The full evaluations of the DPFs are random bit vectors that
// differ in only the bit at position target, so the two dot products
// (which are each a single bit) will be a bitwise sharing of the value
// of V at position target.  Note that if V[target] = 1, then target =
// S+k for some 1 <= k <= 2^63-1, then since target = S+x = (target-x) + x
// , we have that x = k is in that same range; i.e. x>0 as a 64-bit
// signed integer (and similarly if V[target] = 0, then x <= 0).
//
// So far, this is all standard, and for DPFs of smaller depth, this is
// the same technique we're doing for RDPFs.  But we can't do it for
// vectors of size 2^64; that's too big.  Even for 2^32 it would be
// annoying.  The observation made in the Grotto paper is that you can
// actually compute this bit sharing in time linear in the *depth* of
// the DPF (that is, logarithmic in the length of V), for some kinds of
// vectors V, including the "single block of 1s" one described above.
//
// The key insight is that if you look at any _interior_ node of the
// tree, the corresponding nodes on the two DPFs will be a bit sharing
// of the sum of all the leaves in the subtree rooted at that interior
// node: 0 if target is not in that subtree, and 1 if it is.  So you
// just have to find the minimal set of interior nodes such that the
// leaves of the subtrees rooted at those nodes is exactly the block of
// 1s in V, and then each party adds up the flag bits of those leaves.
// The result is a bit sharing of 1 if V[target]=1 and 0 if V[target]=0;
// that is, it is a bit sharing of V[target], and so (as above) of the
// result of the comparison [x>0].  You can also find and evaluate the
// flag bits of this minimal set in time and memory linear in the depth
// of the DPF.
//
// So at the end, we've computed a bit sharing of [x>0] with local
// computation linear in the depth of the DPF (concretely, 114 AES
// operations), and only a *single word* of communication in each
// direction (exchanging the target{i}-x{i} values).  Of course, this
// assumes you have one pair of these DPFs lying around, and you have to
// use a fresh pair with a fresh random target value for each
// comparison, since revealing target-x for two different x's but the
// same target leaks the difference of the x's. But in the 3-party
// setting (or even the 2+1-party setting), you can just have the server
// at preprocessing time precompute a bunch of these pairs in advance,
// and hand bunches of the first item in each pair to player 0 and the
// second item in each pair to player 1 (a single message from the
// server to each of player 0 and player 1). These DPFs are very fast to
// compute, and very small (< 1KB each) to transmit and store.

// See also dpf.hpp for the differences between these DPFs and the ones
// we use for oblivious random access to memory.

struct CDPF : public DPF {
    // Additive and XOR shares of the target value
    RegAS as_target;
    RegXS xs_target;
    // The extra correction word we'll need for the right child at the
    // final leaf layer; this is needed because we're making the tree 7
    // layers shorter than you would naively expect (depth 57 instead of
    // 64), and having the 128-bit labels on the leaf nodes directly
    // represent the 128 bits that would have come out of the subtree of
    // a (notional) depth-64 tree rooted at that depth-57 node.
    DPFnode leaf_cwr;

    // Generate a pair of CDPFs with the given target value
    //
    // Cost:
    // 4*VALUE_BITS - 28 = 228 local AES operations (with value_bits = 64)
    static std::tuple<CDPF,CDPF> generate(value_t target, size_t &aes_ops);

    // Generate a pair of CDPFs with a random target value
    //W
    // Cost:
    // 4*VALUE_BITS - 28 = 228 local AES operations
    static std::tuple<CDPF,CDPF> generate(size_t &aes_ops);

    // Descend from the parent of a leaf node to the leaf node
    inline DPFnode descend_to_leaf(const DPFnode &parent,
        bit_t whichchild, size_t &aes_ops) const;

    // Get the leaf node for the given input.  We don't actually use
    // this in the protocol, but it's useful for testing.
    DPFnode leaf(value_t input, size_t &aes_ops) const;

    // Get the appropriate (RegXS or RegAS) target
    inline void get_target(RegAS &target) const { target = as_target; }
    inline void get_target(RegXS &target) const { target = xs_target; }

    // Compare the given additively shared element (x) to 0. The output is
    // a triple of bit shares; the first is a share of 1 iff the
    // reconstruction of the element is negative; the second iff it is
    // 0; the third iff it is positive.  (All as two's-complement
    // VALUE_BIT-bit integers.)  Note in particular that exactly one of
    // the outputs will be a share of 1, so you can do "greater than or
    // equal to" just by adding the greater and equal outputs together.
    // Note also that you can compare two RegAS values A and B by
    // passing A-B here.
    //
    // Cost:
    // 1 word sent in 1 message
    // 2*VALUE_BITS - 14 = 114 local AES operations
    std::tuple<RegBS,RegBS,RegBS> compare(MPCTIO &tio, yield_t &yield,
        RegAS x, size_t &aes_ops);

    // You can call this version directly if you already have S = target-x
    // reconstructed.  This routine is entirely local; no communication
    // is needed.
    //
    // Cost:
    // 2*VALUE_BITS - 14 = 114 local AES operations
    std::tuple<RegBS,RegBS,RegBS> compare(value_t S, size_t &aes_ops);

    // Determine whether the given additively or XOR shared element is 0.
    // The output is a bit share, which is a share of 1 iff the passed
    // element is a share of 0.  Note also that you can compare two RegAS or
    // RegXS values A and B for equality by passing A-B here.
    //
    // Cost:
    // 1 word sent in 1 message
    // VALUE_BITS - 7 = 57 local AES operations
    template <typename T>
    RegBS is_zero(MPCTIO &tio, yield_t &yield,
        const T &x, size_t &aes_ops);

    // You can call this version directly if you already have S = target-x
    // reconstructed.  This routine is entirely local; no communication
    // is needed.  This function is identical to compare, above, except that
    // it only computes what's needed for the eq output.
    //
    // Cost:
    // VALUE_BITS - 7 = 57 local AES operations
    RegBS is_zero(value_t S, size_t &aes_ops);
};

// Descend from the parent of a leaf node to the leaf node
inline DPFnode CDPF::descend_to_leaf(const DPFnode &parent,
    bit_t whichchild, size_t &aes_ops) const
{
    DPFnode prgout;
    bool flag = get_lsb(parent);
    prg(prgout, parent, whichchild, aes_ops);
    if (flag) {
        DPFnode CW = cw.back();
        DPFnode CWR = leaf_cwr;
        prgout ^= (whichchild ? CWR : CW);
    }
    return prgout;
}

#include "cdpf.tcc"

#endif
