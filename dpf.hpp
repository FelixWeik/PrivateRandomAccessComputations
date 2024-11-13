#ifndef __DPF_HPP__
#define __DPF_HPP__

#include <vector>
#include "prg.hpp"

// We have two major kinds of distributed point functions (DPFs): ones
// used for random-access memory (RDPFs) and ones used for comparisons
// (CDPFs).  The major differences:
//
// RDPFs are of depth r in order to obliviously access a memory of size
// 2^r.  They are created jointly by P0 and P1, with O(r) communication,
// but O(2^r) local computation.  They can output bit shares of a
// single-bit unit vector, word-sized additive shares of a unit vector,
// XOR shares of a scaled unit vector, or additive shares of a scaled
// unit vector.  They are typically used by evaluating _all_ 2^r leaves.
// All of the 2^r leaves have to be computed at creation time, anyway,
// so you can choose to store an "expanded" version of them that just
// records those precomputed values, making them much faster to use in
// the online phase, at the cost of storage and memory (and the time it
// takes to just read them from disk, particular on rotational media).
//
// CDPFs are only used to compare VALUE_BITS-bit values (typically
// VALUE_BITS = 64), and can only output bit shares of a single-bit unit
// vector. This allows for an optimization where the leaf nodes of the
// DPF (which are 128 bits wide) can subsume the last 7 levels of the
// tree, so the CDPF is actually of height VALUE_BITS-7 (typically 57).
// They are never used to expand all leaves, since that's way too large,
// nor could P0 and P1 jointly compute them in the way they compute
// RDPFs. On the other hand, P2 never sees these CDPFs in the online
// phase, so P2 can just create them and send the halves to P0 and P1 at
// preprocessing time.  They are very cheap to create, store, and send
// over the network (in the preprocessing phase), and evaluate (in the
// online phase); all of these are O(VALUE_BITS-7), somewhat abusing O()
// notation here.

struct DPF {
    // The type of nodes
    using node = DPFnode;

    // The 128-bit (256-bit respectively) seed
    DPFnode seed;
    // 0 = left half, 1 = right half
    bit_t whichhalf;
    // correction words (holds n dpf nodes with n being the number of levels in the dpf)
    std::vector<DPFnode> cw;
    // correction flag bits: the one for level i is bit i of this word
    // these are xor shares of a standard basis vector (the shifting vector (equivalent to t in the paper))
    value_t cfbits;

    // The seed
    node get_seed() const { return seed; }

    // Descend from a node at depth parentdepth to one of its children
    // whichchild = 0: left child
    // whichchild = 1: right child
    //
    // Cost: 1 AES operation
    inline DPFnode descend(const DPFnode &parent, nbits_t parentdepth,
        bit_t whichchild, size_t &aes_ops) const;
};

// Descend from a node at depth parentdepth to one of its children
// whichchild = 0: left child
// whichchild = 1: right child
inline DPFnode DPF::descend(const DPFnode &parent, nbits_t parentdepth,
    bit_t whichchild, size_t &aes_ops) const
{
    DPFnode prgout;
    bool flag = get_lsb(parent);
    prg(prgout, parent, whichchild, aes_ops);  // generation on the fly possible, since prg returns fixed value for given seed
    if (flag) {
        DPFnode CW = cw[parentdepth];
        bit_t cfbit = !!(cfbits & (value_t(1)<<parentdepth));  // bool-alias: true if flagbit is set at parentdepth, else false
#if VALUE_BITS == 64
        DPFnode CWR = CW ^ lsb128_mask[cfbit];
        prgout ^= whichchild ? CWR : CW;
#else //TODO only covered 128-bit case here => add more if needed
        DPFnode CWR = _mm256_xor_si256(CW, lsb256_mask[cfbit]);
        prgout = _mm256_xor_si256(prgout, whichchild ? CWR : CW);  // xors the child with the given correction word (includes, whether used or not)
#endif
    }
    return prgout;
}

// Don't warn if we never actually use these functions
static void dump_node(DPFnode node, const char *label = nullptr)
__attribute__ ((unused));
static void dump_level(DPFnode *nodes, size_t num, const char *label = nullptr)
__attribute__ ((unused));

static void dump_node(DPFnode node, const char *label)
{
    if (label) printf("%s: ", label);
    for(int i=0;i<16;++i) { printf("%02x", ((unsigned char *)&node)[15-i]); } printf("\n");
}

static void dump_level(DPFnode *nodes, size_t num, const char *label)
{
    if (label) printf("%s:\n", label);
    for (size_t i=0;i<num;++i) {
        dump_node(nodes[i]);
    }
    printf("\n");
}


#endif
