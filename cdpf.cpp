#include <bsd/stdlib.h> // arc4random_buf
#include "bitutils.hpp"
#include "cdpf.hpp"

// Generate a pair of CDPFs with the given target value
std::tuple<CDPF,CDPF> CDPF::generate(value_t target, size_t &aes_ops)
{
    CDPF dpf0, dpf1;
    nbits_t depth = VALUE_BITS - 7;

    // Pick two random seeds
    arc4random_buf(&dpf0.seed, sizeof(dpf0.seed));
    arc4random_buf(&dpf1.seed, sizeof(dpf1.seed));
    // Ensure the flag bits (the lsb of each node) are different
    dpf0.seed = set_lsb(dpf0.seed, 0);
    dpf1.seed = set_lsb(dpf1.seed, 1);
    dpf0.whichhalf = 0;
    dpf1.whichhalf = 1;
    dpf0.cfbits = 0;
    dpf1.cfbits = 0;
    dpf0.as_target.randomize();
    dpf1.as_target.ashare = target - dpf0.as_target.ashare;
    dpf0.xs_target.randomize();
    dpf1.xs_target.xshare = target ^ dpf0.xs_target.xshare;

    // The current node in each CDPF as we descend the tree.  The
    // invariant is that cur0 and cur1 are the nodes on the path to the
    // target at level curlevel.  They will necessarily be different,
    // and indeed must have different flag (low) bits.
    DPFnode cur0 = dpf0.seed;
    DPFnode cur1 = dpf1.seed;
    nbits_t curlevel = 0;

    while(curlevel < depth) {
        // Construct the two (uncorrected) children of each node
        DPFnode left0, right0, left1, right1;
        prgboth(left0, right0, cur0, aes_ops);
        prgboth(left1, right1, cur1, aes_ops);

        // Which way lies the target?
        bool targetdir = !!(target & (value_t(1)<<((depth+7)-curlevel-1)));
        DPFnode CW;
        bool cfbit = !get_lsb(left0 ^ left1 ^ right0 ^ right1);
        bool flag0 = get_lsb(cur0);
        bool flag1 = get_lsb(cur1);
        // The last level is special
        if (curlevel < depth-1) {
            if (targetdir == 0) {
                // The target is to the left, so make the correction word
                // and bit make the right children the same and the left
                // children have different flag bits.

                // Recall that descend will apply (only for the party whose
                // current node (cur0 or cur1) has the flag bit set, for
                // which exactly one of the two will) CW to both children,
                // and cfbit to the flag bit of the right child.
                CW = right0 ^ right1 ^ lsb128_mask[cfbit];

                // Compute the current nodes for the next level
                // Exactly one of these two XORs will fire, so afterwards,
                // cur0 ^ cur1 = left0 ^ left1 ^ CW, which will have low bit
                // 1 by the definition of cfbit.
                cur0 = xor_if(left0, CW, flag0);
                cur1 = xor_if(left1, CW, flag1);
            } else {
                // The target is to the right, so make the correction word
                // and bit make the left children the same and the right
                // children have different flag bits.
                CW = left0 ^ left1;

                // Compute the current nodes for the next level
                // Exactly one of these two XORs will fire, so similar to
                // the above, afterwards, cur0 ^ cur1 = right0 ^ right1 ^ CWR,
                // which will have low bit 1.
                DPFnode CWR = CW ^ lsb128_mask[cfbit];
                cur0 = xor_if(right0, CWR, flag0);
                cur1 = xor_if(right1, CWR, flag1);
            }
        } else {
            // We're at the last level before the leaves.  We still want
            // the children not in the direction of targetdir to end up
            // the same, but now we want the child in the direction of
            // targetdir to also end up the same, except for the single
            // target bit.  Importantly, the low bit (the flag bit in
            // all other nodes) is not special, and will in fact usually
            // end up the same for the two DPFs (unless the target bit
            // happens to be the low bit of the word; i.e., the low 7
            // bits of target are all 0).

            // This will be a 128-bit word with a single bit set, in
            // position (target & 0x7f).
            uint8_t loc = (target & 0x7f);
            DPFnode target_set_bit = _mm_set_epi64x(
                loc >= 64 ? (uint64_t(1)<<(loc-64)) : 0,
                loc >= 64 ? 0 : (uint64_t(1)<<loc));

            if (targetdir == 0) {
                // We want the right children to be the same, and the
                // left children to be the same except for the target
                // bit.
                // Remember for exactly one of the two parties, CW will
                // be applied to the left and CWR will be applied to the
                // right.
                CW = left0 ^ left1 ^ target_set_bit;
                DPFnode CWR = right0 ^ right1;
                dpf0.leaf_cwr = CWR;
                dpf1.leaf_cwr = CWR;
            } else {
                // We want the left children to be the same, and the
                // right children to be the same except for the target
                // bit.
                // Remember for exactly one of the two parties, CW will
                // be applied to the left and CWR will be applied to the
                // right.
                CW = left0 ^ left1;
                DPFnode CWR = right0 ^ right1 ^ target_set_bit;
                dpf0.leaf_cwr = CWR;
                dpf1.leaf_cwr = CWR;
            }
        }
        dpf0.cw.push_back(CW);
        dpf1.cw.push_back(CW);
        dpf0.cfbits |= (value_t(cfbit)<<curlevel);
        dpf1.cfbits |= (value_t(cfbit)<<curlevel);
        ++curlevel;
    }

    return std::make_tuple(dpf0, dpf1);
}

// Generate a pair of CDPFs with a random target value
std::tuple<CDPF,CDPF> CDPF::generate(size_t &aes_ops)
{
    value_t target;
    arc4random_buf(&target, sizeof(target));
    return generate(target, aes_ops);
}

// Get the leaf node for the given input.  We don't actually use
// this in the protocol, but it's useful for testing.
DPFnode CDPF::leaf(value_t input, size_t &aes_ops) const
{
    nbits_t depth = cw.size();
    DPFnode node = seed;
    input >>= 7;
    for (nbits_t d=0;d<depth-1;++d) {
        bit_t dir = !!(input & (value_t(1)<<(depth-d-1)));
        node = descend(node, d, dir, aes_ops);
    }
    // The last layer is special
    bit_t dir = input & 1;
    node = descend_to_leaf(node, dir, aes_ops);
    return node;
}

// Compare the given additively shared element to 0.  The output is
// a triple of bit shares; the first is a share of 1 iff the
// reconstruction of the element is negative; the second iff it is
// 0; the third iff it is positive.  (All as two's-complement
// VALUE_BIT-bit integers.)  Note in particular that exactly one of
// the outputs will be a share of 1, so you can do "greater than or
// equal to" just by adding the greater and equal outputs together.
// Note also that you can compare two RegAS values A and B by
// passing A-B here.
std::tuple<RegBS,RegBS,RegBS> CDPF::compare(MPCTIO &tio, yield_t &yield,
    RegAS x)
{
    // Reconstruct S = target-x
    RegBS gt, eq;
    // The server does nothing in this protocol
    if (tio.player() < 2) {
        RegAS S_share = as_target - x;
        tio.iostream_peer() << x;
        yield();
        RegAS peer_S_share;
        tio.iostream_peer() >> peer_S_share;
        value_t S = S_share.ashare + peer_S_share.ashare;

        // Now we're going to simultaneously descend the DPF tree for
        // the values S and T = S + 2^63.  Note that the 1 values of V
        // (see the explanation of the algorithm in cdpf.hpp) are those
        // values _strictly_ larger than S and smaller than T (noting
        // they can "wrap around" 2^64).  In level 1 of the tree, the
        // paths to S and T will necessarily be at the two different
        // children of the root seed, but they could be in either order.
        // From then on, they will proceed in lockstep, either both
        // going left, or both going right.  If they both go left, we
        // also compute the right sibling on the S path, and add it to
        // the gt flag.  If they both go right, we also compute the left
        // sibling on the T path, and add it to the gt flag.  When we
        // hit the leaves, the gt flag will account for all of the
        // complete leaf nodes strictly greater than S and strictly less
        // than T.  Then we just have to pull out the parity of the
        // appropriate bits in the two leaf nodes containing S and T
        // respectively to complete the computation of gt, and also to
        // get the single bit eq.
    }
    // Once we have gt and eq (which cannot both be 1), lt is just 1
    // exactly if they're both 0.
    RegBS lt;
    lt.bshare = 1 ^ eq.bshare ^ gt.bshare;

    return std::make_tuple(lt, eq, gt);
}
