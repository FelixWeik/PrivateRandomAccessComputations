#ifndef __RDPF_HPP__
#define __RDPF_HPP__

#include <array>
#include <vector>

#include "mpcio.hpp"
#include "coroutine.hpp"
#include "types.hpp"
#include "bitutils.hpp"
#include "dpf.hpp"

// DPFs for oblivious random accesses to memory.  See dpf.hpp for the
// differences between the different kinds of DPFs.

// A single RDPF can use its unit vector for any as reads of the same
// memory location as you like, as long as it's OK that everyone _knows_
// it's the same memory location.  The same RDPF can also be configured
// to allow for WIDTH independent updates; if you otherwise would try to
// reuse the same RDPF for multiple updates of the same memory location,
// you would leak the difference between the update _values_.  Typically
// WIDTH=1, since most RDPFs are not reused at all.
//
// We implement this by have a "wide" LeafNode type that can store one
// 64-bit value for the read, and WIDTH 64-bit values for the writes.
// Since each DPFnode is 128 bits, you need 1 + (WIDTH/2) DPFnodes in a
// LeafNode.  We will also need to pass around arrays of WIDTH RegAS and
// RegXS values, so we make dedicated wide types for those (RegASW and
// RegXSW).

template <nbits_t WIDTH>
struct RDPF : public DPF {
    template <typename T>
    using W = std::array<T, WIDTH>;
    // The wide shared register types
    // Array holding WIDTH elements of type RegAS => AS of value M
    using RegASW = W<RegAS>;
    // Array holding WIDTH elements of type RegXS => XS of value M
    using RegXSW = W<RegXS>;
    // The number of 128-bit leaf node entries you need to get 1 unit
    // value and WIDTH scaled values (each is 64 bits)
    static const nbits_t LWIDTH = 1 + (WIDTH/2);
    using LeafNode = std::array<DPFnode,LWIDTH>;  // DPFNode wird aufgeteilt => 64bit für read, 64bit für write (writes WIDTH mal möglich)

    // Information for leaf levels of the RDPF.  Normal RDPFs only have
    // one leaf level (at the bottom), but incremental RDPFs have a leaf
    // level for each level of the DPF.
    struct LeafInfo {
        static const nbits_t W = WIDTH;
        // The correction word for this leaf level
        LeafNode leaf_cw;
        // The amount we have to scale the low words of the leaf values by
        // to get additive shares of a unit vector (value is 64-bit vector)
        value_t unit_sum_inverse;
        // Additive share of the scaling values M_as such that the high words
        // of the WIDTH leaf values for P0 and P1 add to M_as * e_{target}
        std::array<RegAS,WIDTH> scaled_sum;
        // XOR share of the scaling values M_xs such that the high words
        // of the WIDTH leaf values for P0 and P1 XOR to M_xs * e_{target}
        std::array<RegXS,WIDTH> scaled_xor;
        // If we're saving the expansion, put it here
        std::vector<LeafNode> expansion;

        LeafInfo() : unit_sum_inverse(0) {}
    };

    // The depth of this RDPF.  If this is not an incremental DPF, then
    // both the maximum depth and current depth are just the normal
    // depth (specified at DPF creation time).  If this is an
    // incremental DPF, then the maximum depth is the one specified at
    // creation time, but the current depth will be between 1 and that
    // value (inclusive).
    nbits_t maxdepth, curdepth;

    // The LeafInfo for each leaf level.  Normal RDPFs only have one
    // leaf level, so this will be a vector of length 1.  Incremental
    // RDPFs will have one entry for each level in the DPF.  The entry
    // corresponding to level i of the DPF (of total depth d) is
    // leaf_info[d-i].
    std::vector<LeafInfo> li;

    // The leaf correction flag bits for each leaf level.  The bit for
    // level i (for an incremental DPF of max depth m) is leaf_cfbits &
    // (1<<(m-i)).  For a normal (not incremental) RDPF, it's the same,
    // but therefore only the low bit gets used.
    value_t leaf_cfbits;

    RDPF() = default;

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
        RegXS target, nbits_t depth, bool incremental = false,
        bool save_expansion = false);

    // Do we have a precomputed expansion?
    inline bool has_expansion() const {
        return li[maxdepth-curdepth].expansion.size() > 0;
    }

    // Get an element of the expansion
    inline LeafNode get_expansion(address_t index) const {
        return li[maxdepth-curdepth].expansion[index];
    }

    // The depth
    inline nbits_t depth() const { return curdepth; }

    // Set the current depth for an incremental RDPF; 0 means to use
    // maxdepth
    inline void depth(nbits_t newdepth) {
        if (newdepth > 0 && newdepth < maxdepth) {
            curdepth = newdepth;
        } else {
            curdepth = maxdepth;
        }
    }

    // Get the leaf node for the given input
    //
    // Cost: depth AES operations
    LeafNode leaf(address_t input, size_t &aes_ops) const;

    // Expand the DPF if it's not already expanded
    void expand(size_t &aes_ops);

    // Descend from a node at depth parentdepth to one of its leaf children
    // whichchild = 0: left child
    // whichchild = 1: right child
    //
    // Cost: 1 AES operation
    inline LeafNode descend_to_leaf(const DPFnode &parent,
        nbits_t parentdepth, bit_t whichchild, size_t &aes_ops) const;

    // Get the bit-shared unit vector entry from the leaf node
    inline RegBS unit_bs(const LeafNode &leaf) const {
        RegBS b;
        b.bshare = get_lsb(leaf[0]);
        return b;;;
    }

    // Get the additive-shared unit vector entry from the leaf node
    inline RegAS unit_as(const LeafNode &leaf) const {
        RegAS a;
        value_t lowword = value_t(_mm_cvtsi128_si64x(leaf[0]));
        if (whichhalf == 1) {
            lowword = -lowword;
        }
        a.ashare = lowword * li[maxdepth-curdepth].unit_sum_inverse;
        return a;
    }

    // Get the XOR-shared scaled vector entry from the leaf node
    inline RegXSW scaled_xs(const LeafNode &leaf) const {
        RegXSW x;
        nbits_t j = 0;
        value_t highword =
            value_t(_mm_cvtsi128_si64x(_mm_srli_si128(leaf[0],8)));
        x[j++].xshare = highword;
        for (nbits_t i=1;i<LWIDTH;++i) {
            value_t lowword =
                value_t(_mm_cvtsi128_si64x(leaf[i]));
            value_t highword =
                value_t(_mm_cvtsi128_si64x(_mm_srli_si128(leaf[i],8)));
            x[j++].xshare = lowword;
            if (j < WIDTH) {
                x[j++].xshare = highword;
            }
        }
        return x;
    }

    // Get the additive-shared scaled vector entry from the leaf node
    inline RegASW scaled_as(const LeafNode &leaf) const {
        RegASW a;
        nbits_t j = 0;
        value_t highword =
            value_t(_mm_cvtsi128_si64x(_mm_srli_si128(leaf[0],8)));
        if (whichhalf == 1) {
            highword = -highword;
        }
        a[j++].ashare = highword;
        for (nbits_t i=1;i<WIDTH;++i) {
            value_t lowword =
                value_t(_mm_cvtsi128_si64x(leaf[i]));
            value_t highword =
                value_t(_mm_cvtsi128_si64x(_mm_srli_si128(leaf[i],8)));
            if (whichhalf == 1) {
                lowword = -lowword;
                highword = -highword;
            }
            a[j++].ashare = lowword;
            if (j < WIDTH) {
                a[j++].ashare = highword;
            }
        }
        return a;
    }

private:
    // Expand one leaf layer of the DPF if it's not already expanded (implemented in rdpf.tcc)
    void expand_leaf_layer(nbits_t li_index, size_t &aes_ops);
};

// Computational peers will generate triples of RDPFs with the _same_
// random target for use in Duoram.  They will each hold a share of the
// target (neither knowing the complete target index).  They will each
// give one of the DPFs (not a matching pair) to the server, but not the
// shares of the target index.  So computational peers will hold a
// RDPFTriple (which includes both an additive and an XOR share of the
// target index), while the server will hold a RDPFPair (which does
// not).

template <nbits_t WIDTH>
struct RDPFTriple {
    template <typename T>
    using Triple = std::tuple<T, T, T>;
    template <typename T>
    using WTriple = std::tuple<
        typename std::array<T,WIDTH>,
        typename std::array<T,WIDTH>,
        typename std::array<T,WIDTH> >;

    // The type of triples of nodes, LeafNodes, and the wide shared
    // register types
    using node = Triple<DPFnode>;
    using LeafNode = Triple<typename RDPF<WIDTH>::LeafNode>;
    using RegASWT = WTriple<RegAS>;
    using RegXSWT = WTriple<RegXS>;

    RegAS as_target;
    RegXS xs_target;
    RDPF<WIDTH> dpf[3]; // this is the actual rdpf triple

    // The depth
    inline nbits_t depth() const { return dpf[0].depth(); }

    // Set the current depth for an incremental RDPFTriple; 0 means to
    // use maxdepth
    inline void depth(nbits_t newdepth) {
        dpf[0].depth(newdepth);
        dpf[1].depth(newdepth);
        dpf[2].depth(newdepth);
    }

    // The seed
    inline node get_seed() const {
        return std::make_tuple(dpf[0].get_seed(), dpf[1].get_seed(),
            dpf[2].get_seed());
    }

    // Do we have a precomputed expansion?
    inline bool has_expansion() const {
        int li_index = dpf[0].maxdepth - dpf[0].curdepth;
        return dpf[0].li[li_index].expansion.size() > 0;
    }

    // Get an element of the expansion
    inline LeafNode get_expansion(address_t index) const {
        return std::make_tuple(dpf[0].get_expansion(index),
            dpf[1].get_expansion(index), dpf[2].get_expansion(index));
    }

    RDPFTriple() = default;

    // Construct three RDPFs of the given depth all with the same
    // randomly generated target index.
    RDPFTriple(MPCTIO &tio, yield_t &yield,
        nbits_t depth, bool incremental = false, bool save_expansion = false);

    // Descend the three RDPFs in lock step
    node descend(const node &parent, nbits_t parentdepth,
        bit_t whichchild, size_t &aes_ops) const;

    // Descend the three RDPFs in lock step to a leaf node
    LeafNode descend_to_leaf(const node &parent, nbits_t parentdepth,
        bit_t whichchild, size_t &aes_ops) const;

    // Overloaded versions of functions to get DPF components and
    // outputs so that the appropriate one can be selected with a
    // parameter

    // Only RegXS, not RegAS, indices are used with incremental RDPFs
    inline void get_target(RegAS &target) const { target = as_target; }
    inline void get_target(RegXS &target) const {
        target = xs_target >> (dpf[0].maxdepth - dpf[0].curdepth);
    }

    // Additive share of the scaling value M_as such that the high words
    // of the leaf values for P0 and P1 add to M_as * e_{target}
    inline void scaled_value(RegASWT &v) const {
        int li_index = dpf[0].maxdepth - dpf[0].curdepth;
        std::get<0>(v) = dpf[0].li[li_index].scaled_sum;
        std::get<1>(v) = dpf[1].li[li_index].scaled_sum;
        std::get<2>(v) = dpf[2].li[li_index].scaled_sum;
    }

    // XOR share of the scaling value M_xs such that the high words
    // of the leaf values for P0 and P1 XOR to M_xs * e_{target}
    inline void scaled_value(RegXSWT &v) const {
        int li_index = dpf[0].maxdepth - dpf[0].curdepth;
        std::get<0>(v) = dpf[0].li[li_index].scaled_xor;
        std::get<1>(v) = dpf[1].li[li_index].scaled_xor;
        std::get<2>(v) = dpf[2].li[li_index].scaled_xor;
    }

    // Get the additive-shared unit vector entry from the leaf node
    inline void unit(std::tuple<RegAS,RegAS,RegAS> &u, const LeafNode &leaf) const {
        std::get<0>(u) = dpf[0].unit_as(std::get<0>(leaf));
        std::get<1>(u) = dpf[1].unit_as(std::get<1>(leaf));
        std::get<2>(u) = dpf[2].unit_as(std::get<2>(leaf));
    }

    // Get the bit-shared unit vector entry from the leaf node
    inline void unit(std::tuple<RegXS,RegXS,RegXS> &u, const LeafNode &leaf) const {
        std::get<0>(u) = dpf[0].unit_bs(std::get<0>(leaf));
        std::get<1>(u) = dpf[1].unit_bs(std::get<1>(leaf));
        std::get<2>(u) = dpf[2].unit_bs(std::get<2>(leaf));
    }

    // For any more complex entry type, that type will handle the conversion
    // for each DPF
    template <typename T>
    inline void unit(std::tuple<T,T,T> &u, const LeafNode &leaf) const {
        std::get<0>(u).unit(dpf[0], std::get<0>(leaf));
        std::get<1>(u).unit(dpf[1], std::get<1>(leaf));
        std::get<2>(u).unit(dpf[2], std::get<2>(leaf));
    }

    // Get the additive-shared scaled vector entry from the leaf node
    inline void scaled(RegASWT &s, const LeafNode &leaf) const {
        std::get<0>(s) = dpf[0].scaled_as(std::get<0>(leaf));
        std::get<1>(s) = dpf[1].scaled_as(std::get<1>(leaf));
        std::get<2>(s) = dpf[2].scaled_as(std::get<2>(leaf));
    }

    // Get the XOR-shared scaled vector entry from the leaf node
    inline void scaled(RegXSWT &s, const LeafNode &leaf) const {
        std::get<0>(s) = dpf[0].scaled_xs(std::get<0>(leaf));
        std::get<1>(s) = dpf[1].scaled_xs(std::get<1>(leaf));
        std::get<2>(s) = dpf[2].scaled_xs(std::get<2>(leaf));
    }
};

template <nbits_t WIDTH>
struct RDPFPair {
    template <typename T>
    using Pair = std::tuple<T, T>;
    template <typename T>
    using WPair = std::tuple<
        typename std::array<T,WIDTH>,
        typename std::array<T,WIDTH> >;

    // The type of pairs of nodes, LeafNodes, and the wide shared
    // register types
    using node = Pair<DPFnode>;
    using LeafNode = Pair<typename RDPF<WIDTH>::LeafNode>;
    using RegASWP = WPair<RegAS>;
    using RegXSWP = WPair<RegXS>;

    RDPF<WIDTH> dpf[2];

    RDPFPair() = default;

    // The depth
    inline nbits_t depth() const { return dpf[0].depth(); }

    // Set the current depth for an incremental RDPFPair; 0 means to use
    // maxdepth
    inline void depth(nbits_t newdepth) {
        dpf[0].depth(newdepth);
        dpf[1].depth(newdepth);
    }

    // The seed
    inline node get_seed() const {
        return std::make_tuple(dpf[0].get_seed(), dpf[1].get_seed());
    }

    // Do we have a precomputed expansion?
    inline bool has_expansion() const {
        int li_index = dpf[0].maxdepth - dpf[0].curdepth;
        return dpf[0].li[li_index].expansion.size() > 0;
    }

    // Get an element of the expansion
    inline LeafNode get_expansion(address_t index) const {
        return std::make_tuple(dpf[0].get_expansion(index),
            dpf[1].get_expansion(index));
    }

    // Descend the two RDPFs in lock step
    node descend(const node &parent, nbits_t parentdepth,
        bit_t whichchild, size_t &aes_ops) const;

    // Descend the two RDPFs in lock step to a leaf node
    LeafNode descend_to_leaf(const node &parent, nbits_t parentdepth,
        bit_t whichchild, size_t &aes_ops) const;

    // Overloaded versions of functions to get DPF components and
    // outputs so that the appropriate one can be selected with a
    // parameter

    // Additive share of the scaling value M_as such that the high words
    // of the leaf values for P0 and P1 add to M_as * e_{target}
    inline void scaled_value(RegASWP &v) const {
        std::get<0>(v) = dpf[0].scaled_sum;
        std::get<1>(v) = dpf[1].scaled_sum;
    }

    // XOR share of the scaling value M_xs such that the high words
    // of the leaf values for P0 and P1 XOR to M_xs * e_{target}
    inline void scaled_value(RegXSWP &v) const {
        std::get<0>(v) = dpf[0].scaled_xor;
        std::get<1>(v) = dpf[1].scaled_xor;
    }

    // Get the additive-shared unit vector entry from the leaf node
    inline void unit(std::tuple<RegAS,RegAS> &u, const LeafNode &leaf) const {
        std::get<0>(u) = dpf[0].unit_as(std::get<0>(leaf));
        std::get<1>(u) = dpf[1].unit_as(std::get<1>(leaf));
    }

    // Get the bit-shared unit vector entry from the leaf node
    inline void unit(std::tuple<RegXS,RegXS> &u, const LeafNode &leaf) const {
        std::get<0>(u) = dpf[0].unit_bs(std::get<0>(leaf));
        std::get<1>(u) = dpf[1].unit_bs(std::get<1>(leaf));
    }

    // For any more complex entry type, that type will handle the conversion
    // for each DPF
    template <typename T>
    inline void unit(std::tuple<T,T> &u, const LeafNode &leaf) const {
        std::get<0>(u).unit(dpf[0], std::get<0>(leaf));
        std::get<1>(u).unit(dpf[1], std::get<1>(leaf));
    }

    // Get the additive-shared scaled vector entry from the leaf node
    inline void scaled(RegASWP &s, const LeafNode &leaf) const {
        std::get<0>(s) = dpf[0].scaled_as(std::get<0>(leaf));
        std::get<1>(s) = dpf[1].scaled_as(std::get<1>(leaf));
    }

    // Get the XOR-shared scaled vector entry from the leaf node
    inline void scaled(RegXSWP &s, const LeafNode &leaf) const {
        std::get<0>(s) = dpf[0].scaled_xs(std::get<0>(leaf));
        std::get<1>(s) = dpf[1].scaled_xs(std::get<1>(leaf));
    }

};

// These are used by computational peers, who hold RPDFTriples, but when
// reading, only need to use 2 of the 3 RDPFs.  The API follows that of
// RDPFPair, but internally, it holds two references to external RDPFs,
// instead of holding the RDPFs themselves.

template <nbits_t WIDTH>
struct RDPF2of3 {
    template <typename T>
    using Pair = std::tuple<T, T>;
    template <typename T>
    using WPair = std::tuple<
        typename std::array<T,WIDTH>,
        typename std::array<T,WIDTH> >;

    // The type of pairs of nodes, LeafNodes, and the wide shared
    // register types
    using node = Pair<DPFnode>;
    using LeafNode = Pair<typename RDPF<WIDTH>::LeafNode>;
    using RegASWP = WPair<RegAS>;
    using RegXSWP = WPair<RegXS>;

    const RDPF<WIDTH> &dpf0, &dpf1;

    // Create an RDPFPair from an RDPFTriple, keeping two of the RDPFs
    // and dropping one.  This _moves_ the dpfs from the triple to the
    // pair, so the triple will no longer be valid after using this.
    // which0 and which1 indicate which of the dpfs to keep.
    RDPF2of3(const RDPFTriple<WIDTH> &trip, int which0, int which1) :
        dpf0(trip.dpf[which0]), dpf1(trip.dpf[which1]) {}

    // The depth
    inline nbits_t depth() const { return dpf0.depth(); }

    // Set the current depth for an incremental RDPFPair; 0 means to use
    // maxdepth
    inline void depth(nbits_t newdepth) {
        dpf0.depth(newdepth);
        dpf1.depth(newdepth);
    }

    // The seed
    inline node get_seed() const {
        return std::make_tuple(dpf0.get_seed(), dpf1.get_seed());
    }

    // Do we have a precomputed expansion?
    inline bool has_expansion() const {
        int li_index = dpf0.maxdepth - dpf0.curdepth;
        return dpf0.li[li_index].expansion.size() > 0;
    }

    // Get an element of the expansion
    inline LeafNode get_expansion(address_t index) const {
        return std::make_tuple(dpf0.get_expansion(index),
            dpf1.get_expansion(index));
    }

    // Descend the two RDPFs in lock step
    node descend(const node &parent, nbits_t parentdepth,
        bit_t whichchild, size_t &aes_ops) const;

    // Descend the two RDPFs in lock step to a leaf node
    LeafNode descend_to_leaf(const node &parent, nbits_t parentdepth,
        bit_t whichchild, size_t &aes_ops) const;

    // Overloaded versions of functions to get DPF components and
    // outputs so that the appropriate one can be selected with a
    // parameter

    // Additive share of the scaling value M_as such that the high words
    // of the leaf values for P0 and P1 add to M_as * e_{target}
    inline void scaled_value(RegASWP &v) const {
        std::get<0>(v)[0] = dpf0.scaled_sum[0];
        std::get<1>(v)[0] = dpf1.scaled_sum[0];
    }

    // XOR share of the scaling value M_xs such that the high words
    // of the leaf values for P0 and P1 XOR to M_xs * e_{target}
    inline void scaled_value(RegXSWP &v) const {
        std::get<0>(v)[0] = dpf0.scaled_xor[0];
        std::get<1>(v)[0] = dpf1.scaled_xor[0];
    }

    // Get the additive-shared unit vector entry from the leaf node
    inline void unit(std::tuple<RegAS,RegAS> &u, const LeafNode &leaf) const {
        std::get<0>(u) = dpf0.unit_as(std::get<0>(leaf));
        std::get<1>(u) = dpf1.unit_as(std::get<1>(leaf));
    }

    // Get the bit-shared unit vector entry from the leaf node
    inline void unit(std::tuple<RegXS,RegXS> &u, const LeafNode &leaf) const {
        std::get<0>(u) = dpf0.unit_bs(std::get<0>(leaf));
        std::get<1>(u) = dpf1.unit_bs(std::get<1>(leaf));
    }

    // For any more complex entry type, that type will handle the conversion
    // for each DPF
    template <typename T>
    inline void unit(std::tuple<T,T> &u, const LeafNode &leaf) const {
        std::get<0>(u).unit(dpf0, std::get<0>(leaf));
        std::get<1>(u).unit(dpf1, std::get<1>(leaf));
    }

    // Get the additive-shared scaled vector entry from the leaf node
    inline void scaled(RegASWP &s, const LeafNode &leaf) const {
        std::get<0>(s) = dpf0.scaled_as(std::get<0>(leaf));
        std::get<1>(s) = dpf1.scaled_as(std::get<1>(leaf));
    }

    // Get the XOR-shared scaled vector entry from the leaf node
    inline void scaled(RegXSWP &s, const LeafNode &leaf) const {
        std::get<0>(s) = dpf0.scaled_xs(std::get<0>(leaf));
        std::get<1>(s) = dpf1.scaled_xs(std::get<1>(leaf));
    }

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
    typename T::LeafNode next();
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
    // (int, address_t, const T::node &) -> V
    // which will be called like this for each i from 0 to num_evals-1,
    // across num_thread threads:
    // value_i = process(t, i, DPF((start+i) XOR xor_offset))
    // t is the thread number (0 <= t < num_threads).
    // The resulting num_evals values will be combined using V's +=
    // operator, first accumulating the values within each thread
    // (starting with the init value), and then accumulating the totals
    // from each thread together (again starting with the init value):
    //
    // total = init
    // for each thread t:
    //     accum_t = init
    //     for each accum_i generated by thread t:
    //         accum_t += value_i
    //     total += accum_t
    template <typename V, typename W>
    inline V reduce(V init, W process);
};

#include "rdpf.tcc"

#endif
