// Templated method implementations for duoram.hpp
#ifndef DUORAM_TCC
#define DUORAM_TCC
#include <stdio.h>

#include "rdpf.hpp"

// Pass the player number and desired size => scales the vectors to the given size (unsigned long has at most 2^64 - 1 values)
// This means a 64-bit vector suffices to index into every item of the database
template <typename T>
Duoram<T>::Duoram(int player, size_t size) : player(player),
        oram_size(size), p0_blind(blind), p1_blind(peer_blinded_db) {
    if (player < 2) {
        database.resize(size);  // initialisiert den vektor mit size-vielen T werten (alle null) => kann dann geupdatet werden
        blind.resize(size);
        peer_blinded_db.resize(size);
    } else {
        p0_blind.resize(size);  // adjusts the vector length to match the size of the database (= value stored in size)
        p1_blind.resize(size);  // same here
    }
}

// For debugging; print the contents of the Duoram to stdout
template <typename T>
void Duoram<T>::dump() const
{
    for (size_t i=0; i<oram_size; ++i) {
        if (player < 2) {
            printf("%04lx ", i);
            database[i].dump();
            printf(" ");
            blind[i].dump();
            printf(" ");
            peer_blinded_db[i].dump();
            printf("\n");
        } else {
            printf("%04lx ", i);
            p0_blind[i].dump();
            printf(" ");
            p1_blind[i].dump();
            printf("\n");
        }
    }
    printf("\n");
}

// Enable or disable explicit-only mode.  Only using [] with
// explicit (address_t) indices are allowed in this mode.  Using []
// with RegAS or RegXS indices will automatically turn off this
// mode, or you can turn it off explicitly.  In explicit-only mode,
// updates to the memory in the Shape will not induce communication
// to the server or peer, but when it turns off, a message of the
// size of the entire Shape will be sent to each of the server and
// the peer.  This is useful if you're going to be doing multiple
// explicit writes to every element of the Shape before you do your
// next oblivious read or write.  Bitonic sort is a prime example.
template <typename T>
void Duoram<T>::Shape::explicitonly(bool enable)
{
    if (enable == true)  {
        explicitmode = true;
    } else if (explicitmode == true) {
        explicitmode = false;
        // Reblind the whole Shape
        int player = tio.player();
        if (player < 2) {
            for (size_t i=0; i<shape_size; ++i) {
                auto [ DB, BL, PBD ] = get_comp(i);
                BL.randomize();
                mpz_t tmp;
                mpz_add(tmp, DB.share().get_mpz_t(), BL.share().get_mpz_t());
                auto tmp0 = mpz_get_str(nullptr, 2, BL.share().get_mpz_t());
                tio.iostream_server().write(tmp0, sizeof(tmp0));
                auto tmp1 = mpz_get_str(nullptr, 2, tmp);
                tio.iostream_peer().write(tmp1, sizeof(tmp1));
            }
            yield();
            for (size_t i=0; i<shape_size; ++i) {
                auto [ DB, BL, PBD ] = get_comp(i);
                tio.iostream_peer() >> PBD;
            }
        } else {
            yield();
            for (size_t i=0; i<shape_size; ++i) {
                auto [BL0, BL1] = get_server(i);
                tio.iostream_p0() >> BL0;
                tio.iostream_p1() >> BL1;
            }
        }
    }

}

// For debugging or checking your answers (using this in general is
// of course insecure)
// This one reconstructs the whole Shape
template <typename T>
std::vector<T> Duoram<T>::Shape::reconstruct() const
{
    int player = tio.player();
    std::vector<T> res;
    res.resize(shape_size);
    // Player 1 sends their share of the database to player 0
    if (player == 1) {
        for (size_t i=0; i < shape_size; ++i) {
            T elt = std::get<0>(get_comp(i));
            tio.queue_peer(&elt, sizeof(T));
        }
        yield();
    } else if (player == 0) {
        yield();
        for(size_t i=0; i < shape_size; ++i) {
            tio.recv_peer(&res[i], sizeof(T));
            T myelt = std::get<0>(get_comp(i));
            res[i] += myelt;
        }
    } else if (player == 2) {
        // The server (player 2) only syncs with the yield
        yield();
    }

    // Players 1 and 2 will get an empty vector here
    return res;
}

// This one reconstructs a single database value
template <typename T>
T Duoram<T>::Shape::reconstruct(const T& share) const
{
    int player = tio.player();
    T res;

    // Player 1 sends their share of the value to player 0
    if (player == 1) {
        tio.queue_peer(&share, sizeof(T));
        yield();
    } else if (player == 0) {
        yield();
        tio.recv_peer(&res, sizeof(T));
        res += share;
    } else if (player == 2) {
        // The server (player 2) only syncs with the yield
        yield();
    }

    // Players 1 and 2 will get 0 here
    return res;
}

// Function to set the shape_size of a shape and compute the number of
// bits you need to address a shape of that size (which is the number of
// bits in sz-1).  This is typically called by subclass constructors.
template <typename T>
void Duoram<T>::Shape::set_shape_size(size_t sz)
{
    shape_size = sz;
    // Compute the number of bits in (sz-1)
    // But use 0 if sz=0 for some reason (though that should never
    // happen)
    if (sz > 1) {
        addr_size = 64-__builtin_clzll(sz-1);
        addr_mask = address_t((size_t(1)<<addr_size)-1);
    } else {
        addr_size = 0;
        addr_mask = 0;
    }
}

// Constructor for the Flat shape.  len=0 means the maximum size (the
// parent's size minus start).
template <typename T>
Duoram<T>::Flat::Flat(Duoram &duoram, MPCTIO &tio, yield_t &yield,
    size_t start, size_t len) : Shape(*this, duoram, tio, yield)
{
    size_t parentsize = duoram.size();
    if (start > parentsize) {
        start = parentsize;
    }
    this->start = start;
    size_t maxshapesize = parentsize - start;
    if (len > maxshapesize || len == 0) {
        len = maxshapesize;
    }
    this->len = len;
    this->set_shape_size(len);
}

// Constructor for the Flat shape.  len=0 means the maximum size (the
// parent's size minus start).
template <typename T>
Duoram<T>::Flat::Flat(const Shape &parent, MPCTIO &tio, yield_t &yield,
    size_t start, size_t len) : Shape(parent, parent.duoram, tio, yield)
{
    size_t parentsize = parent.size();
    if (start > parentsize) {
        start = parentsize;
    }
    this->start = start;
    size_t maxshapesize = parentsize - start;
    if (len > maxshapesize || len == 0) {
        len = maxshapesize;
    }
    this->len = len;
    this->set_shape_size(len);
}

// Bitonic sort the elements from start to start+len-1, in
// increasing order if dir=0 or decreasing order if dir=1. Note that
// the elements must be at most 63 bits long each for the notion of
// ">" to make consistent sense.
template <typename T>
void Duoram<T>::Flat::bitonic_sort(address_t start, address_t len, bool dir)
{
    // if (len < 2) return;
    // if (len == 2) {
    //     osort(start, start+1, dir);
    //     return;
    // }
    // address_t leftlen, rightlen;
    // leftlen = (len+1) >> 1;
    // rightlen = len >> 1;
    //
    // // Recurse on the first half (opposite to the desired order)
    // // and the second half (desired order) in parallel
    // run_coroutines(this->yield,
    //     [this, start, leftlen, dir](yield_t &yield) {
    //         Flat Acoro = context(yield);
    //         Acoro.bitonic_sort(start, leftlen, !dir);
    //     },
    //     [this, start, leftlen, rightlen, dir](yield_t &yield) {
    //         Flat Acoro = context(yield);
    //         Acoro.bitonic_sort(start+leftlen, rightlen, dir);
    //     });
    // // Merge the two into the desired order
    // butterfly(start, len, dir);
}

// Internal function to aid bitonic_sort
template <typename T>
void Duoram<T>::Flat::butterfly(address_t start, address_t len, bool dir)
{
    // if (len < 2) return;
    // if (len == 2) {
    //     osort(start, start+1, dir);
    //     return;
    // }
    // address_t leftlen, rightlen, offset, num_swaps;
    // // leftlen = (len+1) >> 1;
    // leftlen = 1;
    // while(2*leftlen < len) {
    //     leftlen *= 2;
    // }
    // rightlen = len - leftlen;
    // offset = leftlen;
    // num_swaps = rightlen;
    //
    // // Sort pairs of elements offset apart in parallel
    // std::vector<coro_t> coroutines;
    // for (address_t i=0; i<num_swaps;++i) {
    //     coroutines.emplace_back(
    //         [this, start, offset, dir, i](yield_t &yield) {
    //             Flat Acoro = context(yield);
    //             Acoro.osort(start+i, start+i+offset, dir);
    //         });
    // }
    // run_coroutines(this->yield, coroutines);
    // // Recurse on each half in parallel
    // run_coroutines(this->yield,
    //     [this, start, leftlen, dir](yield_t &yield) {
    //         Flat Acoro = context(yield);
    //         Acoro.butterfly(start, leftlen, dir);
    //     },
    //     [this, start, leftlen, rightlen, dir](yield_t &yield) {
    //         Flat Acoro = context(yield);
    //         Acoro.butterfly(start+leftlen, rightlen, dir);
    //     });
}

// Helper functions to specialize the read and update operations for
// RegAS and RegXS shared indices
template <typename U>
inline address_t IfRegAS(address_t val);
template <typename U>
inline address_t IfRegXS(address_t val);

template <>
inline address_t IfRegAS<RegAS>(address_t val) { return val; }
template <>
inline address_t IfRegAS<RegXS>(address_t val) { return 0; }
template <>
inline address_t IfRegXS<RegAS>(address_t val) { return 0; }
template <>
inline address_t IfRegXS<RegXS>(address_t val) { return val; }

// Oblivious read from an additively or XOR shared index of Duoram memory
// T is the sharing type of the _values_ in the database; U is the
// sharing type of the _indices_ in the database.  If we are referencing
// an entire entry of type T, then the field type FT will equal T, and
// the field selector type FST will be nullopt_t.  If we are referencing
// a particular field of T, then FT will be the type of the field (RegAS
// or RegXS) and FST will be a pointer-to-member T::* type pointing to
// that field.  Sh is the specific Shape subtype used to create the
// MemRefS.  WIDTH is the RDPF width to use.

template <typename T>
template <typename U,typename FT,typename FST,typename Sh,nbits_t WIDTH>
Duoram<T>::Shape::MemRefS<U,FT,FST,Sh,WIDTH>::operator FT()
{
    FT res;
    Sh &shape = this->shape;
    shape.explicitonly(false);
    int player = shape.tio.player();
    if (player < 2) {
        // Computational players do this

        const RDPFTriple<WIDTH> &dt = *(oblividx->dt);
        const nbits_t depth = dt.depth();

        // Compute the index offset
        U indoffset;
        dt.get_target(indoffset);
        indoffset -= oblividx->idx;

        // We only need two of the DPFs for reading
        RDPF2of3<WIDTH> dp(dt, 0, player == 0 ? 2 : 1);

        // Send it to the peer and the server
        shape.tio.queue_peer(&indoffset, BITBYTES(depth));
        shape.tio.queue_server(&indoffset, BITBYTES(depth));

        shape.yield();

        // Receive the above from the peer
        U peerindoffset;
        shape.tio.recv_peer(&peerindoffset, BITBYTES(depth));

        // Reconstruct the total offset
        auto indshift = combine(indoffset, peerindoffset, depth);

        // Evaluate the DPFs and compute the dotproducts
        ParallelEval pe(dp, IfRegAS<U>(indshift.get_ui()), IfRegXS<U>(indshift.get_ui()),
            shape.shape_size, shape.tio.cpu_nthreads(),
            shape.tio.aes_ops());
        FT init;
        res = pe.reduce(init, [this, &dp, &shape] (int thread_num,
                address_t i, const typename RDPFPair<WIDTH>::LeafNode &leaf) {
            // The values from the two DPFs, which will each be of type T
            std::tuple<FT,FT> V;
            dp.unit(V, leaf);
            auto [V0, V1] = V;
            // References to the appropriate cells in our database, our
            // blind, and our copy of the peer's blinded database
            auto [DB, BL, PBD] = shape.get_comp(i, fieldsel);
            return (DB + PBD).mulshare(V0) - BL.mulshare(V1-V0);
        });

        shape.yield();

        // Receive the cancellation term from the server
        FT gamma;
        shape.tio.iostream_server() >> gamma;
        res += gamma;
    } else {
        // The server does this

        const RDPFPair<WIDTH> &dp = *(oblividx->dp);
        const nbits_t depth = dp.depth();
        U p0indoffset, p1indoffset;

        shape.yield();

        // Receive the index offset from the computational players and
        // combine them
        shape.tio.recv_p0(&p0indoffset, BITBYTES(depth));
        shape.tio.recv_p1(&p1indoffset, BITBYTES(depth));
        auto indshift = combine(p0indoffset, p1indoffset, depth);

        // Evaluate the DPFs to compute the cancellation terms
        std::tuple<FT,FT> init, gamma;
        ParallelEval pe(dp, IfRegAS<U>(indshift.get_ui()), IfRegXS<U>(indshift.get_ui()),
            shape.shape_size, shape.tio.cpu_nthreads(),
            shape.tio.aes_ops());
        gamma = pe.reduce(init, [this, &dp, &shape] (int thread_num,
                address_t i, const typename RDPFPair<WIDTH>::LeafNode &leaf) {
            // The values from the two DPFs, each of type FT
            std::tuple<FT,FT> V;
            dp.unit(V, leaf);
            auto [V0, V1] = V;

            // shape.get_server(i) returns a pair of references to the
            // appropriate cells in the two blinded databases
            auto [BL0, BL1] = shape.get_server(i, fieldsel);
            return std::make_tuple(-BL0.mulshare(V1), -BL1.mulshare(V0));
        });

        // Choose a random blinding factor
        FT rho;
        rho.randomize();

        std::get<0>(gamma) += rho;
        std::get<1>(gamma) -= rho;

        // Send the cancellation terms to the computational players
        // shape.tio.iostream_p0() << std::get<0>(gamma);
        // shape.tio.iostream_p1() << std::get<1>(gamma); ursprüngliche lösung (die geht aber nicht)

        auto tmp0 = mpz_get_str(nullptr, 2, std::get<0>(gamma).share().get_mpz_t());
        auto tmp1 = mpz_get_str(nullptr, 2, std::get<1>(gamma).share().get_mpz_t());

        shape.tio.iostream_p0().write(tmp0, sizeof tmp0);  //TODO checken, ob nicht doch 10er Basis
        shape.tio.iostream_p1().write(tmp1, sizeof tmp1);
        shape.yield();
    }
    return res;  // The server will always get 0
}

// Oblivious update to a shared index of Duoram memory, only for
// FT = RegAS or RegXS.  The template parameters are as above.
template <typename T>
template <typename U, typename FT, typename FST, typename Sh, nbits_t WIDTH>
typename Duoram<T>::Shape::template MemRefS<U,FT,FST,Sh,WIDTH>
    &Duoram<T>::Shape::MemRefS<U,FT,FST,Sh,WIDTH>::oram_update(const FT& M,
        const prac_template_true &)
{
    Sh &shape = this->shape;
    shape.explicitonly(false);
    int player = shape.tio.player();
    if (player < 2) {
        // Computational players do this

        const RDPFTriple<WIDTH> &dt = *(oblividx->dt);  // possible, since MemRefS friend class of OblivIndex
        const nbits_t windex = oblividx->windex();
        const nbits_t depth = dt.depth();

        // Compute the index and message offsets
        U indoffset;
        dt.get_target(indoffset);
        indoffset -= oblividx->idx;
        typename RDPF<WIDTH>::template W<FT> MW;
        MW[windex] = M;
        auto Moffset = std::make_tuple(MW, MW, MW);
        typename RDPFTriple<WIDTH>::template WTriple<FT> scaled_val;
        dt.scaled_value(scaled_val);
        Moffset -= scaled_val;

        // Send them to the peer, and everything except the first offset
        // to the server
        shape.tio.queue_peer(&indoffset, BITBYTES(depth));
        shape.tio.iostream_peer() << Moffset;
        shape.tio.queue_server(&indoffset, BITBYTES(depth));
        shape.tio.iostream_server() << std::get<1>(Moffset) <<
            std::get<2>(Moffset);

        shape.yield();

        // Receive the above from the peer
        U peerindoffset;
        typename RDPFTriple<WIDTH>::template WTriple<FT> peerMoffset;
        shape.tio.recv_peer(&peerindoffset, BITBYTES(depth));
        shape.tio.iostream_peer() >> peerMoffset;

        // Reconstruct the total offsets
        auto indshift = combine(indoffset, peerindoffset, depth);
        auto Mshift = combine(Moffset, peerMoffset);

        // Evaluate the DPFs and add them to the database
        ParallelEval pe(dt, IfRegAS<U>(indshift.get_ui()), IfRegXS<U>(indshift.get_ui()),
            shape.shape_size, shape.tio.cpu_nthreads(),
            shape.tio.aes_ops());
        int init = 0;
        pe.reduce(init, [this, &dt, &shape, &Mshift, player, windex] (int thread_num,
                address_t i, const typename RDPFTriple<WIDTH>::LeafNode &leaf) {
            // The values from the three DPFs
            typename RDPFTriple<WIDTH>::template WTriple<FT> scaled;
            std::tuple<FT,FT,FT> unit;
            dt.scaled(scaled, leaf);
            dt.unit(unit, leaf);
            auto [V0, V1, V2] = scaled + unit * Mshift;
            // References to the appropriate cells in our database, our
            // blind, and our copy of the peer's blinded database
            auto [DB, BL, PBD] = shape.get_comp(i,fieldsel);
            DB += V0[windex];
            if (player == 0) {
                BL -= V1[windex];
                PBD += V2[windex]-V0[windex];
            } else {
                BL -= V2[windex];
                PBD += V1[windex]-V0[windex];
            }
            return 0;
        });
    } else {
        // The server does this

        const RDPFPair<WIDTH> &dp = *(oblividx->dp);
        const nbits_t windex = oblividx->windex();
        const nbits_t depth = dp.depth();
        U p0indoffset, p1indoffset;
        typename RDPFPair<WIDTH>::template WPair<FT> p0Moffset, p1Moffset;

        shape.yield();

        // Receive the index and message offsets from the computational
        // players and combine them
        shape.tio.recv_p0(&p0indoffset, BITBYTES(depth));
        shape.tio.iostream_p0() >> p0Moffset;
        shape.tio.recv_p1(&p1indoffset, BITBYTES(depth));
        shape.tio.iostream_p1() >> p1Moffset;
        auto indshift = combine(p0indoffset, p1indoffset, depth);
        auto Mshift = combine(p0Moffset, p1Moffset);

        // Evaluate the DPFs and subtract them from the blinds
        ParallelEval pe(dp, IfRegAS<U>(indshift.get_ui()), IfRegXS<U>(indshift.get_ui()),
            shape.shape_size, shape.tio.cpu_nthreads(),
            shape.tio.aes_ops());
        int init = 0;
        pe.reduce(init, [this, &dp, &shape, &Mshift, windex] (int thread_num,
                address_t i, const typename RDPFPair<WIDTH>::LeafNode &leaf) {
            // The values from the two DPFs
            typename RDPFPair<WIDTH>::template WPair<FT> scaled;
            std::tuple<FT,FT> unit;
            dp.scaled(scaled, leaf);
            dp.unit(unit, leaf);
            auto [V0, V1] = scaled + unit * Mshift;
            // shape.get_server(i) returns a pair of references to the
            // appropriate cells in the two blinded databases, so we can
            // subtract the pair directly.
            auto [BL0, BL1] = shape.get_server(i,fieldsel);
            BL0 -= V0[windex];
            BL1 -= V1[windex];
            return 0;
        });
    }
    return *this;
}

// Oblivious update to a shared index of Duoram memory, only for
// FT not RegAS or RegXS.  The template parameters are as above.
template <typename T>
template <typename U, typename FT, typename FST, typename Sh, nbits_t WIDTH>
typename Duoram<T>::Shape::template MemRefS<U,FT,FST,Sh,WIDTH>
    &Duoram<T>::Shape::MemRefS<U,FT,FST,Sh,WIDTH>::oram_update(const FT& M,
        const prac_template_false &)
{
    T::update(shape, shape.yield, oblividx->idx, M);
    return *this;
}

// Oblivious update to an additively or XOR shared index of Duoram
// memory. The template parameters are as above.
template <typename T>
template <typename U, typename FT, typename FST, typename Sh, nbits_t WIDTH>
typename Duoram<T>::Shape::template MemRefS<U,FT,FST,Sh,WIDTH>
    &Duoram<T>::Shape::MemRefS<U,FT,FST,Sh,WIDTH>::operator+=(const FT& M)
{
    return oram_update(M, prac_basic_Reg_S<FT>());
}

// Oblivious write to an additively or XOR shared index of Duoram
// memory. The template parameters are as above.
template <typename T>
template <typename U, typename FT, typename FST, typename Sh, nbits_t WIDTH>
typename Duoram<T>::Shape::template MemRefS<U,FT,FST,Sh,WIDTH>
    &Duoram<T>::Shape::MemRefS<U,FT,FST,Sh,WIDTH>::operator=(const FT& M)
{
    FT oldval = *this;
    FT update = M - oldval;
    *this += update;
    return *this;
}

// Oblivious sort with the provided other element.  Without
// reconstructing the values, *this will become a share of the
// smaller of the reconstructed values, and other will become a
// share of the larger.
//
// Note: this only works for additively shared databases
template <> template <typename U,typename V>
void Duoram<RegAS>::Flat::osort(const U &idx1, const V &idx2, bool dir)
{
    // // Load the values in parallel
    // RegAS val1, val2;
    // run_coroutines(yield,
    //     [this, &idx1, &val1](yield_t &yield) {
    //         Flat Acoro = context(yield);
    //         val1 = Acoro[idx1];
    //     },
    //     [this, &idx2, &val2](yield_t &yield) {
    //         Flat Acoro = context(yield);
    //         val2 = Acoro[idx2];
    //     });
    // // Get a CDPF
    // // CDPF cdpf = tio.cdpf(yield);
    // // Use it to compare the values
    // RegAS diff = val1-val2;
    // // auto [lt, eq, gt] = cdpf.compare(tio, yield, diff, tio.aes_ops());
    // RegBS cmp = dir ? lt : gt;
    // // Get additive shares of cmp*diff
    // RegAS cmp_diff;
    // mpc_flagmult(tio, yield, cmp_diff, cmp, diff);
    // // Update the two locations in parallel
    // run_coroutines(yield,
    //     [this, &idx1, &cmp_diff](yield_t &yield) {
    //         Flat Acoro = context(yield);
    //         Acoro[idx1] -= cmp_diff;
    //     },
    //     [this, &idx2, &cmp_diff](yield_t &yield) {
    //         Flat Acoro = context(yield);
    //         Acoro[idx2] += cmp_diff;
    //     });
}

// Explicit read from a given index of Duoram memory
template <typename T> template <typename FT, typename FST>
Duoram<T>::Shape::MemRefExpl<FT,FST>::operator FT()
{
    Shape &shape = this->shape;
    FT res;
    int player = shape.tio.player();
    if (player < 2) {
        res = std::get<0>(shape.get_comp(idx, fieldsel));
    }
    return res;  // The server will always get 0
}

// Explicit update to a given index of Duoram memory
template <typename T> template <typename FT, typename FST>
typename Duoram<T>::Shape::template MemRefExpl<FT,FST>
    &Duoram<T>::Shape::MemRefExpl<FT,FST>::operator+=(const FT& M)
{
    Shape &shape = this->shape;
    int player = shape.tio.player();
    // In explicit-only mode, just update the local DB; we'll sync the
    // blinds and the blinded DB when we leave explicit-only mode.
    if (shape.explicitmode) {
        if (player < 2) {
            auto [ DB, BL, PBD ] = shape.get_comp(idx, fieldsel);
            DB += M;
        }
        return *this;
    }
    if (player < 2) {
        // Computational players do this

        // Pick a blinding factor
        FT blind;
        blind.randomize();

        // Send the blind to the server, and the blinded value to the
        // peer
        auto tmp = mpz_get_str(nullptr, 2, blind.share().get_mpz_t());
        mpz_t tmp0;
        mpz_add(tmp0, M.share().get_mpz_t(), blind.share().get_mpz_t());
        auto tmp1 = mpz_get_str(nullptr, 2, tmp0);
        shape.tio.iostream_server().write(tmp, sizeof tmp);
        shape.tio.iostream_peer().write(tmp1, sizeof tmp1);
        // shape.tio.iostream_server() << blind;
        // shape.tio.iostream_peer() << (M + blind);

        shape.yield();

        // Receive the peer's blinded value
        FT peerblinded;
        shape.tio.iostream_peer() >> peerblinded;

        // Our database, our blind, the peer's blinded database
        auto [ DB, BL, PBD ] = shape.get_comp(idx, fieldsel);
        DB += M;
        BL += blind;
        PBD += peerblinded;
    } else if (player == 2) {
        // The server does this

        shape.yield();

        // Receive the updates to the blinds
        FT p0blind, p1blind;
        shape.tio.iostream_p0() >> p0blind;
        shape.tio.iostream_p1() >> p1blind;

        // The two computational parties' blinds
        auto [ BL0, BL1 ] = shape.get_server(idx, fieldsel);
        BL0 += p0blind;
        BL1 += p1blind;
    }
    return *this;
}

// Explicit write to a given index of Duoram memory
template <typename T> template <typename FT, typename FST>
typename Duoram<T>::Shape::template MemRefExpl<FT,FST>
    &Duoram<T>::Shape::MemRefExpl<FT,FST>::operator=(const FT& M)
{
    FT oldval = *this;
    FT update = M - oldval;
    *this += update;
    return *this;
}

// Independent U-shared reads into a Shape of subtype Sh on a Duoram
// with values of sharing type T
template <typename T> template <typename U, typename Sh>
Duoram<T>::Shape::MemRefInd<U,Sh>::operator std::vector<T>()
{
    std::vector<T> res;
    size_t size = indcs.size();
    res.resize(size);
    std::vector<coro_t> coroutines;
    for (size_t i=0;i<size;++i) {
        coroutines.emplace_back([this, &res, i] (yield_t &yield) {
            Sh Sh_coro = shape.context(yield);
            res[i] = Sh_coro[indcs[i]];
        });
    }
    run_coroutines(shape.yield, coroutines);

    return res;
}

// Independent U-shared updates into a Shape of subtype Sh on a Duoram
// with values of sharing type T (vector version)
template <typename T> template <typename U, typename Sh>
typename Duoram<T>::Shape::template MemRefInd<U,Sh>
    &Duoram<T>::Shape::MemRefInd<U,Sh>::operator+=(const std::vector<T>& M)
{
    size_t size = indcs.size();
    assert(M.size() == size);

    std::vector<coro_t> coroutines;
    for (size_t i=0;i<size;++i) {
        coroutines.emplace_back([this, &M, i] (yield_t &yield) {
            Sh Sh_coro = shape.context(yield);
            Sh_coro[indcs[i]] += M[i];
        });
    }
    run_coroutines(shape.yield, coroutines);

    return *this;
}

// Independent U-shared updates into a Shape of subtype Sh on a Duoram
// with values of sharing type T (array version)
template <typename T> template <typename U, typename Sh> template <size_t N>
typename Duoram<T>::Shape::template MemRefInd<U,Sh>
    &Duoram<T>::Shape::MemRefInd<U,Sh>::operator+=(const std::array<T,N>& M)
{
    size_t size = indcs.size();
    assert(N == size);

    std::vector<coro_t> coroutines;
    for (size_t i=0;i<size;++i) {
        coroutines.emplace_back([this, &M, i] (yield_t &yield) {
            Sh Sh_coro = shape.context(yield);
            Sh_coro[indcs[i]] += M[i];
        });
    }
    run_coroutines(shape.yield, coroutines);

    return *this;
}

// Independent U-shared writes into a Shape of subtype Sh on a Duoram
// with values of sharing type T (vector version)
template <typename T> template <typename U, typename Sh>
typename Duoram<T>::Shape::template MemRefInd<U,Sh>
    &Duoram<T>::Shape::MemRefInd<U,Sh>::operator=(const std::vector<T>& M)
{
    size_t size = indcs.size();
    assert(M.size() == size);

    std::vector<coro_t> coroutines;
    for (size_t i=0;i<size;++i) {
        coroutines.emplace_back([this, &M, i] (yield_t &yield) {
            Sh Sh_coro = shape.context(yield);
            Sh_coro[indcs[i]] = M[i];
        });
    }
    run_coroutines(shape.yield, coroutines);

    return *this;
}

// Independent U-shared writes into a Shape of subtype Sh on a Duoram
// with values of sharing type T (array version)
template <typename T> template <typename U, typename Sh> template <size_t N>
typename Duoram<T>::Shape::template MemRefInd<U,Sh>
    &Duoram<T>::Shape::MemRefInd<U,Sh>::operator=(const std::array<T,N>& M)
{
    size_t size = indcs.size();
    assert(N == size);

    std::vector<coro_t> coroutines;
    for (size_t i=0;i<size;++i) {
        coroutines.emplace_back([this, &M, i] (yield_t &yield) {
            Sh Sh_coro = shape.context(yield);
            Sh_coro[indcs[i]] = M[i];
        });
    }
    run_coroutines(shape.yield, coroutines);

    return *this;
}

#endif