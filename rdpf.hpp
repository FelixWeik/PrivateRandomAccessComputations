#ifndef __RDPF_HPP__
#define __RDPF_HPP__

#include <vector>
#include <iostream>

#include "mpcio.hpp"
#include "coroutine.hpp"
#include "types.hpp"
#include "bitutils.hpp"
#include "dpf.hpp"

// DPFs for oblivious random accesses to memory.  See dpf.hpp for the
// differences between the different kinds of DPFs.

struct RDPF : public DPF {
    // The amount we have to scale the low words of the leaf values by
    // to get additive shares of a unit vector
    value_t unit_sum_inverse;
    // Additive share of the scaling value M_as such that the high words
    // of the leaf values for P0 and P1 add to M_as * e_{target}
    RegAS scaled_sum;
    // XOR share of the scaling value M_xs such that the high words
    // of the leaf values for P0 and P1 XOR to M_xs * e_{target}
    RegXS scaled_xor;
    // If we're saving the expansion, put it here
    std::vector<DPFnode> expansion;

    RDPF() {}

    // Construct a DPF with the given (XOR-shared) target location, and
    // of the given depth, to be used for random-access memory reads and
    // writes.  The DPF is constructed collaboratively by P0 and P1,
    // with the server P2 helping by providing correlated randomness,
    // such as SelectTriples.
    //
    // Cost:
    // (2 DPFnode + 2 bytes)*depth + 1 word communication in
    // 2*depth + 1 messages
    // (2 DPFnode + 1 byte)*depth communication from P2 to each party
    // 2^{depth+1}-2 local AES operations for P0,P1
    // 0 local AES operations for P2
    RDPF(MPCTIO &tio, yield_t &yield,
        RegXS target, nbits_t depth, bool save_expansion = false);

    // Do we have a precomputed expansion?
    inline bool has_expansion() const { return expansion.size() > 0; }

    // Get an element of the expansion
    inline node get_expansion(address_t index) const {
        return expansion[index];
    }

    // Get the leaf node for the given input
    //
    // Cost: depth AES operations
    DPFnode leaf(address_t input, size_t &aes_ops) const;

    // Expand the DPF if it's not already expanded
    void expand(size_t &aes_ops);

    // Get the bit-shared unit vector entry from the leaf node
    inline RegBS unit_bs(DPFnode leaf) const {
        RegBS b;
        b.bshare = get_lsb(leaf);
        return b;
    }

    // Get the additive-shared unit vector entry from the leaf node
    inline RegAS unit_as(DPFnode leaf) const {
        RegAS a;
        value_t lowword = value_t(_mm_cvtsi128_si64x(leaf));
        if (whichhalf == 1) {
            lowword = -lowword;
        }
        a.ashare = lowword * unit_sum_inverse;
        return a;
    }

    // Get the XOR-shared scaled vector entry from the leaf ndoe
    inline RegXS scaled_xs(DPFnode leaf) const {
        RegXS x;
        value_t highword =
            value_t(_mm_cvtsi128_si64x(_mm_srli_si128(leaf,8)));
        x.xshare = highword;
        return x;
    }

    // Get the additive-shared scaled vector entry from the leaf ndoe
    inline RegAS scaled_as(DPFnode leaf) const {
        RegAS a;
        value_t highword =
            value_t(_mm_cvtsi128_si64x(_mm_srli_si128(leaf,8)));
        if (whichhalf == 1) {
            highword = -highword;
        }
        a.ashare = highword;
        return a;
    }

};

// Computational peers will generate triples of RDPFs with the _same_
// random target for use in Duoram.  They will each hold a share of the
// target (neither knowing the complete target index).  They will each
// give one of the DPFs (not a matching pair) to the server, but not the
// shares of the target index.  So computational peers will hold a
// RDPFTriple (which includes both an additive and an XOR share of the
// target index), while the server will hold a RDPFPair (which does
// not).

struct RDPFTriple {
    // The type of node triples
    using node = std::tuple<DPFnode, DPFnode, DPFnode>;

    RegAS as_target;
    RegXS xs_target;
    RDPF dpf[3];

    // The depth
    inline nbits_t depth() const { return dpf[0].depth(); }

    // The seed
    inline node get_seed() const {
        return std::make_tuple(dpf[0].get_seed(), dpf[1].get_seed(),
            dpf[2].get_seed());
    }

    // Do we have a precomputed expansion?
    inline bool has_expansion() const {
        return dpf[0].expansion.size() > 0;
    }

    // Get an element of the expansion
    inline node get_expansion(address_t index) const {
        return std::make_tuple(dpf[0].get_expansion(index),
            dpf[1].get_expansion(index), dpf[2].get_expansion(index));
    }

    RDPFTriple() {}

    // Construct three RDPFs of the given depth all with the same
    // randomly generated target index.
    RDPFTriple(MPCTIO &tio, yield_t &yield,
        nbits_t depth, bool save_expansion = false);

    // Descend the three RDPFs in lock step
    node descend(const node &parent, nbits_t parentdepth,
        bit_t whichchild, size_t &aes_ops) const;

    // Templated versions of functions to get DPF components and outputs
    // so that the appropriate one can be selected with a template
    // parameter

    template <typename T>
    inline std::tuple<T,T,T> scaled_value() const;

    template <typename T>
    inline std::tuple<T,T,T> unit(node leaf) const;

    template <typename T>
    inline std::tuple<T,T,T> scaled(node leaf) const;
};

struct RDPFPair {
    // The type of node pairs
    using node = std::tuple<DPFnode, DPFnode>;

    RDPF dpf[2];

    RDPFPair() {}

    // Create an RDPFPair from an RDPFTriple, keeping two of the RDPFs
    // and dropping one.  This _moves_ the dpfs from the triple to the
    // pair, so the triple will no longer be valid after using this.
    // which0 and which1 indicate which of the dpfs to keep.
    RDPFPair(RDPFTriple &&trip, int which0, int which1) {
        dpf[0] = std::move(trip.dpf[which0]);
        dpf[1] = std::move(trip.dpf[which1]);
    }

    // The depth
    inline nbits_t depth() const { return dpf[0].depth(); }

    // The seed
    inline node get_seed() const {
        return std::make_tuple(dpf[0].get_seed(), dpf[1].get_seed());
    }

    // Do we have a precomputed expansion?
    inline bool has_expansion() const {
        return dpf[0].expansion.size() > 0;
    }

    // Get an element of the expansion
    inline node get_expansion(address_t index) const {
        return std::make_tuple(dpf[0].get_expansion(index),
            dpf[1].get_expansion(index));
    }

    // Descend the two RDPFs in lock step
    node descend(const node &parent, nbits_t parentdepth,
        bit_t whichchild, size_t &aes_ops) const;

    // Templated versions of functions to get DPF components and outputs
    // so that the appropriate one can be selected with a template
    // parameter

    template <typename T>
    inline std::tuple<T,T> scaled_value() const;

    template <typename T>
    inline std::tuple<T,T> unit(node leaf) const;

    template <typename T>
    inline std::tuple<T,T> scaled(node leaf) const;

};

// Streaming evaluation, to avoid taking up enough memory to store
// an entire evaluation.  T can be RDPF, RDPFPair, or RDPFTriple.
template <typename T>
class StreamEval {
    const T &rdpf;
    size_t &aes_ops;
    bool use_expansion;
    nbits_t depth;
    address_t counter_xor_offset;
    address_t indexmask;
    address_t pathindex;
    address_t nextindex;
    std::vector<typename T::node> path;
public:
    // Create a StreamEval object that will start its output at index
    // start.  It will wrap around to 0 when it hits 2^depth.  If
    // use_expansion is true, then if the DPF has been expanded, just
    // output values from that.  If use_expansion=false or if the DPF
    // has not been expanded, compute the values on the fly.  If
    // xor_offset is non-zero, then the outputs are actually
    // DPF(start XOR xor_offset)
    // DPF((start+1) XOR xor_offset)
    // DPF((start+2) XOR xor_offset)
    // etc.
    StreamEval(const T &rdpf, address_t start,
        address_t xor_offset, size_t &aes_ops,
        bool use_expansion = true);

    // Get the next value (or tuple of values) from the evaluator
    typename T::node next();
};

// Parallel evaluation.  This class launches a number of threads each
// running a StreamEval to evaluate a chunk of the RDPF (or RDPFPair or
// RDPFTriple), and accumulates the results within each chunk, and then
// accumulates all the chunks together.  T can be RDPF, RDPFPair, or
// RDPFTriple.
template <typename T>
struct ParallelEval {
    const T &rdpf;
    address_t start;
    address_t xor_offset;
    address_t num_evals;
    int num_threads;
    size_t &aes_ops;

    // Create a Parallel evaluator that will evaluate the given rdpf at
    // DPF(start XOR xor_offset)
    // DPF((start+1) XOR xor_offset)
    // DPF((start+2) XOR xor_offset)
    // ...
    // DPF((start+num_evals-1) XOR xor_offset)
    // where all indices are taken mod 2^depth, and accumulate the
    // results into a single answer.
    ParallelEval(const T &rdpf, address_t start,
        address_t xor_offset, address_t num_evals,
        int num_threads, size_t &aes_ops) :
        rdpf(rdpf), start(start), xor_offset(xor_offset),
        num_evals(num_evals), num_threads(num_threads),
        aes_ops(aes_ops) {}

    // Run the parallel evaluator.  The type V is the type of the
    // accumulator; init should be the "zero" value of the accumulator.
    // The type W (process) is a lambda type with the signature
    // (const ParallelEval &, int, address_t, const T::node &) -> V
    // which will be called like this for each i from 0 to num_evals-1,
    // across num_thread threads:
    // value_i = process(*this, t, i, DPF((start+i) XOR xor_offset))
    // t is the thread number (0 <= t < num_threads).
    // The type X (accumulate) is a lambda type with the signature
    // (const ParallelEval &, V &, const V &)
    // which will be called to combine the num_evals values of accum,
    // first accumulating the values within each thread (starting with
    // the init value), and then accumulating the totals from each
    // thread together (again starting with the init value):
    //
    // total = init
    // for each thread t:
    //     accum_t = init
    //     for each accum_i generated by thread t:
    //         accumulate(*this, acccum_t, value_i)
    //     accumulate(*this, total, accum_t)
    template <typename V, typename W, typename X>
    inline V reduce(V init, W process, X accumulate);
};

#include "rdpf.tcc"

#endif
