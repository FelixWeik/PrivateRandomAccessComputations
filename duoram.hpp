#ifndef __DUORAM_HPP__
#define __DUORAM_HPP__

#include <optional>
#include <functional>

#include "types.hpp"
#include "mpcio.hpp"
#include "coroutine.hpp"
#include "rdpf.hpp"

// Implementation of the 3-party protocols described in:
// Adithya Vadapalli, Ryan Henry, Ian Goldberg, "Duoram: A
// Bandwidth-Efficient Distributed ORAM for 2- and 3-Party Computation".

// A Duoram object is like physical memory: it's just a flat address
// space, and you can't access it directly.  Instead, you need to access
// it through a "Shape", such as Flat, Tree, Path, etc.  Shapes can be
// nested, so you can have a Path of a Subtree of a Tree sitting on the
// base Duoram.  Each Shape's parent must remain in scope (references to
// it must remain valid) for the lifetime of the child Shape.  Each
// shape is bound to a context, which is a thread-specific MPCTIO and a
// coroutine-specific yield_t.  If you launch new threads and/or
// coroutines, you'll need to make a copy of the current Shape for your
// new context, and call context() on it.  Be sure not to call context()
// on a Shape shared with other threads or coroutines.

// This is templated, because you can have a Duoram of additively shared
// (RegAS) or XOR shared (RegXS) elements, or more complex cell types
// (see cell.hpp for example). The paper assumes RegAS to be the
// standard sharing type for the Duoram implementation. This introduces
// the necessity of converting the XOR shares of the DPF to additive shares.

// Please note that the duoram object held by the server differs from the
// duoram held by the peers. while the peers also hold the database, the
// server only holds each peers blind. the differentiation happens in the
// constructor by the number of the player (peer = 0 || 1; server = 2). These
// values will not be initialised if they are not needed by the given party.

template <typename T>
class Duoram {
    // The computational parties have three vectors: the share of the
    // database itself, the party's own blinding factors for its
    // database share, and the _other_ computational party's blinded
    // database share (its database share plus its blind).

    // The player number (0 and 1 for the computational parties and 2
    // for the server)
    int player;
    // size of the Duoram
    size_t oram_size;

    // The server has two vectors: a copy of each computational party's
    // blind.  The database vector will remain empty.

    std::vector<T> database;         // computational parties only

    std::vector<T> blind;            // computational parties use this name
    std::vector<T> &p0_blind;        // server uses this name

    std::vector<T> peer_blinded_db;  // computational parties (= blinded db of the peer (for p0 its p1 and vice versa))
    std::vector<T> &p1_blind;        // server

public:
    // The type of this Duoram
    using type = T;

    // The different Shapes are subclasses of this inner class
    class Shape;
    // These are the different Shapes that exist
    class Flat;
    class Pad;
    class Stride;
    class Path;

    // Oblivious indices for use in related-index ORAM accesses
    template <typename U, nbits_t WIDTH>
    class OblivIndex;

    // Pass the player number and desired size
    Duoram(int player, size_t size);

    // Get the size
    inline size_t size() { return oram_size; }

    // Get the basic Flat shape for this Duoram
    Flat flat(MPCTIO &tio, yield_t &yield, size_t start = 0,
            size_t len = 0) {
        return Flat(*this, tio, yield, start, len);
    }

    // For debugging; print the contents of the Duoram to stdout
    void dump() const;
};

// The parent class of all Shapes.  This is an abstract class that
// cannot itself be instantiated.

template <typename T>
class Duoram<T>::Shape {
    // Subclasses should be able to access _other_ Shapes'
    // get_{comp,server} functions
    friend class Flat;
    friend class Pad;
    friend class Stride;
    friend class Path;

    template <typename U, nbits_t WIDTH>
    friend class OblivIndex;

    // When you index into a shape (A[x]), you get one of these types,
    // depending on the type of x (the index), _not_ on the type T (the
    // underlying type of the Duoram).  That is, you can have an
    // additive-shared index (x) into an XOR-shared database (T), for
    // example.

    // When x is additively or XOR shared
    // U is the sharing type of the indices, while T is the sharing type
    // of the data in the database.  If we are referencing an entire
    // entry of type T, then the field type FT will equal T, and the
    // field selector type FST will be nullopt_t.  If we are referencing
    // a particular field of T, then FT will be the type of the field
    // (RegAS or RegXS) and FST will be a pointer-to-member T::* type
    // pointing to that field.  Sh is the specific Shape subtype used to
    // create the MemRefS.  WIDTH is the RDPF width to use.
    template <typename U, typename FT, typename FST, typename Sh, nbits_t WIDTH>
    class MemRefS;
    // When x is unshared explicit value.  FT and FST are as above.
    template <typename FT, typename FST>
    class MemRefExpl;
    // When x is a vector or array of values of type U, used to denote a
    // collection of independent memory operations that can be performed
    // simultaneously.  Sh is the specific Shape subtype used to create
    // the MemRefInd.
public: // TODO das ist jetzt bisschen Pfusch, muss das weg?
    template <typename U, typename Sh>
    class MemRefInd;
 protected:
    // A reference to the parent shape.  As with ".." in the root
    // directory of a filesystem, the topmost shape is indicated by
    // having parent = *this.
    const Shape &parent;

    // A reference to the backing physical storage
    Duoram &duoram;

    // The size of this shape
    size_t shape_size;

    // The number of bits needed to address this shape (the number of
    // bits in shape_size-1)
    nbits_t addr_size;

    // And a mask with the low addr_size bits set
    address_t addr_mask;

    // The Shape's context (MPCTIO and yield_t)
    MPCTIO &tio;
    yield_t &yield;

    // If you enable explicit-only mode, sending updates of your blind
    // to the server and of your blinded database to your peer will be
    // temporarily disabled.  When you disable it (which will happen
    // automatically at the next ORAM read or write, or you can do it
    // explicitly), new random blinds will be chosen for the whole
    // Shape, and the blinds sent to the server, and the blinded
    // database sent to the peer.
    bool explicitmode;

    // A function to set the shape_size and compute addr_size and
    // addr_mask
    void set_shape_size(size_t sz);

    // We need a constructor because we hold non-static references; this
    // constructor is called by the subclass constructors
    Shape(const Shape &parent, Duoram &duoram, MPCTIO &tio,
        yield_t &yield) : parent(parent), duoram(duoram), shape_size(0),
        tio(tio), yield(yield), explicitmode(false) {}

    // Copy the given Shape except for the tio and yield
    Shape(const Shape &copy_from, MPCTIO &tio, yield_t &yield) :
        parent(copy_from.parent), duoram(copy_from.duoram),
        shape_size(copy_from.shape_size),
        addr_size(copy_from.addr_size), addr_mask(copy_from.addr_mask),
        tio(tio), yield(yield),
        explicitmode(copy_from.explicitmode) {}

    // The index-mapping function. Input the index relative to this
    // shape, and output the corresponding index relative to the parent
    // shape.
    //
    // This is a pure virtual function; all subclasses of Shape must
    // implement it, and of course Shape itself therefore cannot be
    // instantiated.
    virtual size_t indexmap(size_t idx) const = 0;

    // Get a pair (for the server) of references to the underlying
    // Duoram entries at share virtual index idx.
    virtual inline std::tuple<T&,T&> get_server(size_t idx,
        std::nullopt_t null = std::nullopt) const {
        size_t parindex = indexmap(idx);
        if (&(this->parent) == this) {
            return std::tie(
                duoram.p0_blind[parindex],
                duoram.p1_blind[parindex]);
        } else {
            return this->parent.get_server(parindex, null);
        }
    }

    // Get a triple (for the computational players) of references to the
    // underlying Duoram entries at share virtual index idx.
    virtual inline std::tuple<T&,T&,T&> get_comp(size_t idx,
        std::nullopt_t null = std::nullopt) const {
        size_t parindex = indexmap(idx);
        if (&(this->parent) == this) {
            return std::tie(
                duoram.database[parindex],
                duoram.blind[parindex],
                duoram.peer_blinded_db[parindex]);
        } else {
            return this->parent.get_comp(parindex, null);
        }
    }

    // Get a pair (for the server) of references to a particular field
    // of the underlying Duoram entries at share virtual index idx.
    template <typename FT>
    inline std::tuple<FT&,FT&> get_server(size_t idx, FT T::*field) const {
        size_t parindex = indexmap(idx);
        if (&(this->parent) == this) {
            return std::tie(
                duoram.p0_blind[parindex].*field,
                duoram.p1_blind[parindex].*field);
        } else {
            return this->parent.get_server(parindex, field);
        }
    }

    // Get a triple (for the computational players) of references to a
    // particular field to the underlying Duoram entries at share
    // virtual index idx.
    template <typename FT>
    inline std::tuple<FT&,FT&,FT&> get_comp(size_t idx, FT T::*field) const {
        size_t parindex = indexmap(idx);
        if (&(this->parent) == this) {
            return std::tie(
                duoram.database[parindex].*field,
                duoram.blind[parindex].*field,
                duoram.peer_blinded_db[parindex].*field);
        } else {
            return this->parent.get_comp(parindex, field);
        }
    }

public:
    // Get the size
    inline size_t size() const { return shape_size; }

    // Initialize the contents of the Shape to a constant.  This method
    // does no communication; all the operations are local.  This only
    // works for T=RegXS or RegAS.
    void init(size_t value) {
        T v;
        v.set(value);
        init([v] (size_t i) { return v; });
    }

    // As above, but for general T
    void init(const T &value) {
        init([value] (size_t i) { return value; });
    }

    // As above, but use the default initializer for T (probably sets
    // everything to 0).
    void init() {
        T deflt;
        init(deflt);
    }

    // Pass a function f: size_t -> size_t, and initialize element i of the
    // Shape to f(i) for each i.  This method does no communication; all
    // the operations are local.  This function must be deterministic
    // and public.  Only works for T=RegAS or RegXS.
    void init(std::function<size_t(size_t)> f) {
        int player = tio.player();
        if (player < 2) {
            for (size_t i=0; i<shape_size; ++i) {
                auto [DB, BL, PBD] = get_comp(i);
                BL.set(0);
                if (player) {
                    DB.set(f(i));
                    PBD.set(0);
                } else {
                    DB.set(0);
                    PBD.set(f(i));
                }
            }
        } else {
            for (size_t i=0; i<shape_size; ++i) {
                auto [BL0, BL1] = get_server(i);
                BL0.set(0);
                BL1.set(0);
            }
        }
    }

    // Pass a function f: size_t -> T, and initialize element i of the
    // Shape to f(i) for each i.  This method does no communication; all
    // the operations are local.  This function must be deterministic
    // and public.
    void init(std::function<T(size_t)> f) {
        int player = tio.player();
        if (player < 2) {
            for (size_t i=0; i<shape_size; ++i) {
                auto [DB, BL, PBD] = get_comp(i);
                BL = T();
                if (player) {
                    DB = f(i);
                    PBD = T();
                } else {
                    DB = T();
                    PBD = f(i);
                }
            }
        } else {
            for (size_t i=0; i<shape_size; ++i) {
                auto [BL0, BL1] = get_server(i);
                BL0 = T();
                BL1 = T();
            }
        }
    }

    // Assuming the Shape is already sorted, do an oblivious binary
    // search for the smallest index containing the value at least the
    // given one.  (The answer will be the length of the Shape if all
    // elements are smaller than the target.) Only available for additive
    // shared databases for now.

    // The basic version uses log(N) ORAM reads of size N, where N is
    // the smallest power of 2 strictly larger than the Shape size
    RegAS basic_binary_search(RegAS &target);
    // This version does 1 ORAM read of size 2, 1 of size 4, 1 of size
    // 8, ..., 1 of size N/2, where N is the smallest power of 2
    // strictly larger than the Shape size
    RegXS binary_search(RegAS &target);

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
    void explicitonly(bool enable);

    // Create an OblivIndex, non-incrementally (supply the shares of the
    // index directly) or incrementally (the bits of the index will be
    // supplied later, one at a time)

    // Non-incremental, RegXS index
    OblivIndex<RegXS,1> oblivindex(const RegXS &idx, nbits_t depth=0) {
        if (depth == 0) {
            depth = this->addr_size;
        }
        typename Duoram<T>::template OblivIndex<RegXS,1>
            res(this->tio, this->yield, idx, depth);
        return res;
    }

    // Non-incremental, RegAS index
    OblivIndex<RegAS,1> oblivindex(const RegAS &idx, nbits_t depth=0) {
        if (depth == 0) {
            depth = this->addr_size;
        }
        typename Duoram<T>::template OblivIndex<RegAS,1>
            res(this->tio, this->yield, idx, depth);
        return res;
    }

    // Incremental (requires RegXS index, supplied bit-by-bit later)
    OblivIndex<RegXS,1> oblivindex(nbits_t depth=0) {
        if (depth == 0) {
            depth = this->addr_size;
        }
        typename Duoram<T>::template OblivIndex<RegXS,1>
            res(this->tio, this->yield, depth);
        return res;
    }

    // For debugging or checking your answers (using this in general is
    // of course insecure)

    // This one reconstructs the whole database
    std::vector<T> reconstruct() const;

    // This one reconstructs a single database value
    T reconstruct(const T& share) const;
};

// The most basic shape is Flat.  It is almost always the topmost shape,
// and serves to provide MPCTIO and yield_t context to a Duoram without
// changing the indices or size (but can specify a subrange if desired).

template <typename T>
class Duoram<T>::Flat : public Duoram<T>::Shape {
    // If this is a subrange, start may be non-0, but it's usually 0
    size_t start;
    size_t len;

    inline size_t indexmap(size_t idx) const {
        size_t paridx = idx + start;
        return paridx;
    }

    // Internal function to aid bitonic_sort
    void butterfly(address_t start, address_t len, bool dir);

public:
    // Constructor.  len=0 means the maximum size (the parent's size
    // minus start).
    Flat(Duoram &duoram, MPCTIO &tio, yield_t &yield, size_t start = 0,
        size_t len = 0);

    // Constructor.  len=0 means the maximum size (the parent's size
    // minus start).
    Flat(const Shape &parent, MPCTIO &tio, yield_t &yield, size_t start = 0,
        size_t len = 0);

    // Copy the given Flat except for the tio and yield
    Flat(const Flat &copy_from, MPCTIO &tio, yield_t &yield) :
        Shape(copy_from, tio, yield), start(copy_from.start),
        len(copy_from.len) {}

    // Update the context (MPCTIO and yield if you've started a new
    // thread, or just yield if you've started a new coroutine in the
    // same thread).  Returns a new Shape with an updated context.
    Flat context(MPCTIO &new_tio, yield_t &new_yield) const {
        return Flat(*this, new_tio, new_yield);
    }
    Flat context(yield_t &new_yield) const {
        return Flat(*this, this->tio, new_yield);
    }

    // Index into this Flat in various ways => return value has type MemRefS (if not using direct address)
    typename Duoram::Shape::template MemRefS<RegAS,T,std::nullopt_t,Flat,1>
            operator[](const RegAS &idx) {
                typename Duoram<T>::Shape::
                    template MemRefS<RegAS,T,std::nullopt_t,Flat,1>
                    res(*this, idx, std::nullopt);
                return res;
    }
    typename Duoram::Shape::template MemRefS<RegXS,T,std::nullopt_t,Flat,1>
            operator[](const RegXS &idx) {
                typename Duoram<T>::Shape::
                    template MemRefS<RegXS,T,std::nullopt_t,Flat,1>
                    res(*this, idx, std::nullopt);
                return res;
    }
    template <typename U, nbits_t WIDTH>
    typename Duoram::Shape::template MemRefS<U,T,std::nullopt_t,Flat,WIDTH>
            operator[](OblivIndex<U,WIDTH> &obidx) {
        typename Duoram<T>::Shape::
            template MemRefS<RegXS,T,std::nullopt_t,Flat,WIDTH>
            res(*this, obidx, std::nullopt);
        return res;
    }
    typename Duoram::Shape::template MemRefExpl<T,std::nullopt_t>
            operator[](address_t idx) {
        typename Duoram<T>::Shape::
            template MemRefExpl<T,std::nullopt_t>
            res(*this, idx, std::nullopt);
        return res;
    }
    template <typename U>
    typename Duoram::Shape::template MemRefInd<U, Flat>
            operator[](const std::vector<U> &indcs) {
        typename Duoram<T>::Shape::
            template MemRefInd<U,Flat>
            res(*this, indcs);
        return res;
    }
    template <typename U, size_t N>
    typename Duoram::Shape::template MemRefInd<U, Flat>
            operator[](const std::array<U,N> &indcs) {
        typename Duoram<T>::Shape::
            template MemRefInd<U,Flat>
            res(*this, indcs);
        return res;
    }
    Duoram<RegAS>::Shape::MemRefInd<RegAS, Flat>
        operator[](const IndexAS &idx) {
            std::vector<RegAS> i = idx.getVector();
            Duoram<RegAS>::Shape::MemRefInd<RegAS, Flat>
            res(*this, i);
            return res;
    }
    Duoram<RegXS>::Shape::MemRefInd<RegXS, Flat>
        operator[](const IndexXS &idx) {
            std::vector<RegXS> i = idx.getVector();
            Duoram<RegXS>::Shape::MemRefInd<RegXS, Flat>
            res(*this, i);
            return res;
        }

    // Oblivious sort the elements indexed by the two given indices.
    // Without reconstructing the values, if dir=0, this[idx1] will
    // become a share of the smaller of the reconstructed values, and
    // this[idx2] will become a share of the larger.  If dir=1, it's the
    // other way around.
    //
    // Note: this only works for additively shared databases
    template<typename U,typename V>
    void osort(const U &idx1, const V &idx2, bool dir=0);

    // Bitonic sort the elements from start to start+len-1, in
    // increasing order if dir=0 or decreasing order if dir=1. Note that
    // the elements must be at most 63 bits long each for the notion of
    // ">" to make consistent sense.
    void bitonic_sort(address_t start, address_t len, bool dir=0);
};

// Oblivious indices for use in related-index ORAM accesses.
// Verwaltet die RDPFTriples (für Peers) / Pairs (für Server); Einsatz im Kontext von MemRefS (= oblivious reads und writes)
// performing oblivious reads can be done using oblivIndex, but also by using RegAS or RegXS

template <typename T>
template <typename U, nbits_t WIDTH>
class Duoram<T>::OblivIndex {
    template <typename Ux,typename FT,typename FST,typename Sh,nbits_t WIDTHx>
    friend class Shape::MemRefS;

    int player;
    std::optional<RDPFTriple<WIDTH>> dt;
    std::optional<RDPFPair<WIDTH>> dp;
    nbits_t curdepth, maxdepth;
    nbits_t next_windex;
    bool incremental;
    U idx;  // either RegXS or RegAS

public:
    // Non-incremental constructor
    OblivIndex(MPCTIO &tio, yield_t &yield, const U &idx, nbits_t depth) :
        player(tio.player()), curdepth(depth), maxdepth(depth),
        next_windex(0), incremental(false), idx(idx)
    {
        if (player < 2) {
            dt = tio.rdpftriple<WIDTH>(yield, depth);
        } else {
            dp = tio.rdpfpair<WIDTH>(yield, depth);
        }
    }

    // Incremental constructor: only for U=RegXS
    OblivIndex(MPCTIO &tio, yield_t &yield, nbits_t depth) :
        player(tio.player()), curdepth(0), maxdepth(depth),
        next_windex(0), incremental(true), idx(RegXS())
    {
        if (player < 2) {
            dt = tio.rdpftriple<WIDTH>(yield, depth, true);
        } else {
            dp = tio.rdpfpair<WIDTH>(yield, depth, true);
        }
    }


   // The function unit_vector takes in an XOR-share of an index foundindx and a size
   // The function outputs _boolean shares_ of a standard-basis vector of size (with the non-zero index at foundindx)
   // For example suppose nitems = 6; and suppose P0 and P1 take parameters foundindx0 and foundindx1 such that, foundindx0 \oplus foundindx1 = 3
   // P0 and P1 output vectors r0 and r1 such that r0 \oplus r1 = [000100]
   std::vector<RegBS> unit_vector(MPCTIO &tio, yield_t &yield, size_t nitems, RegXS foundidx)
   {
      std::vector<RegBS> standard_basis(nitems);

      if (player < 2) {
          U indoffset;
          dt->get_target(indoffset);
          indoffset -= foundidx;
          U peerindoffset;
          tio.queue_peer(&indoffset, BITBYTES(curdepth));
          yield();
          tio.recv_peer(&peerindoffset, BITBYTES(curdepth));
          auto indshift = combine(indoffset, peerindoffset, curdepth);

          // Pick one of the DPF triples, we can also pick dpf[0] or dpf[2]
          auto se = StreamEval(dt->dpf[1], 0, indshift,  tio.aes_ops(), true);

          for (size_t j = 0; j < nitems; ++j) {
               typename RDPF<WIDTH>::LeafNode  leaf = se.next();
               standard_basis[j] = dt->dpf[1].unit_bs(leaf);
          }

       } else {
          yield();
       }

       return standard_basis;
    }

    // Incrementally append a (shared) bit to the oblivious index
    void incr(RegBS bit)
    {
        assert(incremental);
        idx.xshare = (idx.xshare << 1) | value_t(bit.bshare);
        ++curdepth;
        if (player < 2) {
            dt->depth(curdepth);
        } else {
            dp->depth(curdepth);
        }
    }

    // Get a copy of the index
    U index() { return idx; }

    nbits_t depth() {return curdepth;}

    // Get the next wide-RDPF index
    nbits_t windex() { assert(next_windex < WIDTH); return next_windex++; }
};

// An additive or XOR shared memory reference.  You get one of these
// from a Shape A and an additively shared RegAS index x, or an XOR
// shared RegXS index x, with A[x].  Then you perform operations on this
// object, which do the Duoram operations.  As above, T is the sharing
// type of the data in the database, while U is the sharing type of the
// index used to create this memory reference.  If we are referencing an
// entire entry of type T, then the field type FT will equal T, and the
// field selector type FST will be nullopt_t.  If we are referencing a
// particular field of T, then FT will be the type of the field (RegAS
// or RegXS) and FST will be a pointer-to-member T::* type pointing to
// that field.  Sh is the specific Shape subtype used to create the
// MemRefS.  WIDTH is the RDPF width to use.

// TODO aktuelle Idee: template FT durch pointer FT* ersetzen,
//  dann zugrundeliegende Operationen auf FT* anpassen
//  FT* wäre in diesem Fall einfach BigXS bzw BigAS

template <typename T>
template <typename U, typename FT, typename FST, typename Sh, nbits_t WIDTH>
class Duoram<T>::Shape::MemRefS {
    Sh &shape;
    // oblividx is a reference to the OblivIndex we're using.  In the
    // common case, we own the actual OblivIndex, and it's stored in
    // our_oblividx, and oblividx is a pointer to that.  Sometimes
    // (for example incremental ORAM accesses), the caller will own (and
    // modify between uses) the OblivIndex.  In that case, oblividx will
    // be a pointer to the caller's OblivIndex object, and
    // our_oblividx will be nullopt. => There exist multiple oblivIndex objects
    std::optional<Duoram<T>::OblivIndex<U,WIDTH>> our_oblividx;
    Duoram<T>::OblivIndex<U,WIDTH> *oblividx;

    FST fieldsel;

    // Oblivious update to a shared index of Duoram memory, only for FT = RegAS or RegXS
    // TODO: Change to BigAS / BigXS (diese aufgabe fällt wahrscheinlich über memrefind änderung weg)
    MemRefS<U,FT,FST,Sh,WIDTH> &oram_update(const FT& M, const prac_template_true&);
    // Oblivious update to a shared index of Duoram memory, for
    // FT not RegAS or RegXS
    MemRefS<U,FT,FST,Sh,WIDTH> &oram_update(const FT& M, const prac_template_false&);

public:
    MemRefS<U,FT,FST,Sh,WIDTH>(Sh &shape, const U &idx, FST fieldsel) :
        shape(shape), fieldsel(fieldsel) {
        our_oblividx.emplace(shape.tio, shape.yield, idx,
            shape.addr_size);
        oblividx = &(*our_oblividx);
    }

    MemRefS<U,FT,FST,Sh,WIDTH>(Sh &shape, OblivIndex<U,WIDTH> &obidx, FST fieldsel) :
        shape(shape), fieldsel(fieldsel), oblividx(&obidx) {
    }

    // Create a MemRefS for accessing a particular field of T
    template <typename SFT>
    MemRefS<U,SFT,SFT T::*,Sh,WIDTH> field(SFT T::*subfieldsel) {
        auto res = MemRefS<U,SFT,SFT T::*,Sh,WIDTH>(this->shape,
            *oblividx, subfieldsel);
        return res;
    }

    // Oblivious read from a shared index of Duoram memory
    operator FT();

    // Oblivious update to a shared index of Duoram memory (will call oram_update)
    MemRefS<U,FT,FST,Sh,WIDTH> &operator+=(const FT& M);

    // Oblivious write to a shared index of Duoram memory
    MemRefS<U,FT,FST,Sh,WIDTH> &operator=(const FT& M);
};

// An explicit memory reference.  You get one of these from a Shape A
// and an address_t index x with A[x].  Then you perform operations on
// this object, which update the Duoram state without performing Duoram
// operations.  If we are referencing an entire entry of type T, then
// the field type FT will equal T, and the field selector type FST will
// be nullopt_t.  If we are referencing a particular field of T, then FT
// will be the type of the field (RegAS or RegXS) and FST will be a
// pointer-to-member T::* type pointing to that field.

template <typename T> template <typename FT, typename FST>
class Duoram<T>::Shape::MemRefExpl {
    Shape &shape;
    address_t idx;
    FST fieldsel;

public:
    MemRefExpl(Shape &shape, address_t idx, FST fieldsel) :
        shape(shape), idx(idx), fieldsel(fieldsel) {}

    // Create a MemRefExpl for accessing a partcular field of T
    template <typename SFT>
    MemRefExpl<SFT,SFT T::*> field(SFT T::*subfieldsel) {
        auto res = MemRefExpl<SFT,SFT T::*>(this->shape, idx, subfieldsel);
        return res;
    }

    // Explicit read from a given index of Duoram memory
    // => memrefexpl can be implicitly converted to FT (RegAS / RegXS)
    operator FT();

    // Explicit update to a given index of Duoram memory
    MemRefExpl &operator+=(const FT& M);

    // Explicit write to a given index of Duoram memory
    MemRefExpl &operator=(const FT& M);

    // Convenience function
    MemRefExpl &operator-=(const FT& M) {
        *this += (-M);
        return *this;
    }
};

// A collection of independent memory references that can be processed
// simultaneously.  You get one of these from a Shape A (of specific
// subclass Sh) and a vector or array of indices v with each element of
// type U.

template <typename T> template <typename U, typename Sh>
class Duoram<T>::Shape::MemRefInd {
    Sh &shape;
    std::vector<U> indcs;

public:
    MemRefInd(Sh &shape, std::vector<U> indcs) :
        shape(shape), indcs(indcs) {}
    template <size_t N>
    MemRefInd(Sh &shape, std::array<U,N> aindcs) :
        shape(shape) { for ( auto &i : aindcs ) { indcs.push_back(i); } }

    // Independent reads from shared or explicit indices of Duoram memory
    operator std::vector<T>();

    // Independent updates to shared or explicit indices of Duoram memory
    MemRefInd &operator+=(const std::vector<T>& M);
    template <size_t N>
    MemRefInd &operator+=(const std::array<T,N>& M);
    MemRefInd &operator+=(const BigAS &M);
    MemRefInd &operator+=(const BigXS &M);

    // Independent writes to shared or explicit indices of Duoram memory
    MemRefInd &operator=(const std::vector<T>& M);
    template <size_t N>
    MemRefInd &operator=(const std::array<T,N>& M);

    // Convenience function
    MemRefInd &operator-=(const std::vector<T>& M) { *this += (-M); return *this; }
    template <size_t N>
    MemRefInd &operator-=(const std::array<T,N>& M) { *this += (-M); return *this; }
};

#include "duoram.tcc"

#endif
