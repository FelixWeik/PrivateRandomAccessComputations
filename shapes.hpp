#ifndef __SHAPES_HPP__
#define __SHAPES_HPP__

// Various Shapes beyond the standard Flat (in duoram.hpp)

#include "duoram.hpp"


// A Pad is a Shape that pads an underlying Shape so that read accesses
// past the end return a fixed constant value.  Do _not_ write into a
// Pad!

template <typename T>
class Duoram<T>::Pad : public Duoram<T>::Shape {
    // These are pointers because we need to be able to return a
    // (non-const) T& even from a const Pad.
    T *padvalp;
    T *peerpadvalp;
    T *zerop;
    address_t padded_size;

    size_t indexmap(size_t idx) const override {
        return idx;
    }

    Pad &operator=(const Pad &) = delete;

public:
    // Constructor for the Pad shape. The parent must _not_ be in
    // explicit-only mode.
    Pad(Shape &parent, MPCTIO &tio, yield_t &yield,
        address_t padded_size, value_t padval = 0x7fffffffffffffff);

    // Copy the given Pad except for the tio and yield
    Pad(const Pad &copy_from, MPCTIO &tio, yield_t &yield);

    // Destructor
    ~Pad();

    // Update the context (MPCTIO and yield if you've started a new
    // thread, or just yield if you've started a new coroutine in the
    // same thread).  Returns a new Shape with an updated context.
    Pad context(MPCTIO &new_tio, yield_t &new_yield) const {
        return Pad(*this, new_tio, new_yield);
    }
    Pad context(yield_t &new_yield) const {
        return Pad(*this, this->tio, new_yield);
    }

    // Get a pair (for the server) of references to the underlying
    // Duoram entries at share virtual index idx.  (That is, it gets
    // duoram.p0_blind[indexmap(idx)], etc.)
    std::tuple<T&,T&> get_server(size_t idx,
        std::nullopt_t null = std::nullopt) const override {
        size_t parindex = indexmap(idx);
        if (parindex < this->parent.shape_size) {
            return this->parent.get_server(parindex, null);
        } else {
            return std::tie(*zerop, *zerop);
        }
    }

    // Get a triple (for the computational players) of references to the
    // underlying Duoram entries at share virtual index idx.  (That is,
    // it gets duoram.database[indexmap(idx)], etc.)
    std::tuple<T&,T&,T&> get_comp(size_t idx,
        std::nullopt_t null = std::nullopt) const override {
        size_t parindex = indexmap(idx);
        if (parindex < this->parent.shape_size) {
            return this->parent.get_comp(parindex, null);
        } else {
            return std::tie(*padvalp, *zerop, *peerpadvalp);
        }
    }

    // Index into this Pad in various ways
    typename Duoram::Shape::template MemRefS<RegAS,T,std::nullopt_t,Pad,1>
            operator[](const RegAS &idx) {
        typename Duoram<T>::Shape::
            template MemRefS<RegAS,T,std::nullopt_t,Pad,1>
            res(*this, idx, std::nullopt);
        return res;
    }
    typename Duoram::Shape::template MemRefS<RegXS,T,std::nullopt_t,Pad,1>
            operator[](const RegXS &idx) {
        typename Duoram<T>::Shape::
            template MemRefS<RegXS,T,std::nullopt_t,Pad,1>
            res(*this, idx, std::nullopt);
        return res;
    }
    template <typename U, nbits_t WIDTH>
    typename Duoram::Shape::template MemRefS<U,T,std::nullopt_t,Pad,WIDTH>
            operator[](OblivIndex<U,WIDTH> &obidx) {
        typename Duoram<T>::Shape::
            template MemRefS<RegXS,T,std::nullopt_t,Pad,WIDTH>
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
    Duoram::Shape::MemRefInd<U, Pad>
            operator[](const std::vector<U> &indcs) {
        typename Duoram<T>::Shape::
            template MemRefInd<U,Pad>
            res(*this, indcs);
        return res;
    }
    template <typename U, size_t N>
    Duoram::Shape::MemRefInd<U, Pad>
            operator[](const std::array<U,N> &indcs) {
        typename Duoram<T>::Shape::
            template MemRefInd<U,Pad>
            res(*this, indcs);
        return res;
    }
};


// A Stride is a Shape that represents evenly spaced elements of its
// parent Shape, starting with some offset, and then every stride
// elements.

template <typename T>
class Duoram<T>::Stride : public Duoram<T>::Shape {
    size_t offset;
    size_t stride;

    inline size_t indexmap(size_t idx) const override {
        size_t paridx = offset + idx*stride;
        return paridx;
    }

public:
    // Constructor
    Stride(Shape &parent, MPCTIO &tio, yield_t &yield, size_t offset,
        size_t stride);

    // Copy the given Stride except for the tio and yield
    Stride(const Stride &copy_from, MPCTIO &tio, yield_t &yield) :
        Shape(copy_from, tio, yield), offset(copy_from.offset),
        stride(copy_from.stride) {}

    // Update the context (MPCTIO and yield if you've started a new
    // thread, or just yield if you've started a new coroutine in the
    // same thread).  Returns a new Shape with an updated context.
    Stride context(MPCTIO &new_tio, yield_t &new_yield) const {
        return Stride(*this, new_tio, new_yield);
    }
    Stride context(yield_t &new_yield) const {
        return Stride(*this, this->tio, new_yield);
    }

    // Index into this Stride in various ways
    typename Duoram::Shape::template MemRefS<RegAS,T,std::nullopt_t,Stride,1>
            operator[](const RegAS &idx) {
        typename Duoram<T>::Shape::
            template MemRefS<RegAS,T,std::nullopt_t,Stride,1>
            res(*this, idx, std::nullopt);
        return res;
    }
    typename Duoram::Shape::template MemRefS<RegXS,T,std::nullopt_t,Stride,1>
            operator[](const RegXS &idx) {
        typename Duoram<T>::Shape::
            template MemRefS<RegXS,T,std::nullopt_t,Stride,1>
            res(*this, idx, std::nullopt);
        return res;
    }
    template <typename U, nbits_t WIDTH>
    typename Duoram::Shape::template MemRefS<U,T,std::nullopt_t,Stride,WIDTH>
            operator[](OblivIndex<U,WIDTH> &obidx) {
        typename Duoram<T>::Shape::
            template MemRefS<RegXS,T,std::nullopt_t,Stride,WIDTH>
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
    Duoram::Shape::MemRefInd<U, Stride>
            operator[](const std::vector<U> &indcs) {
        typename Duoram<T>::Shape::
            template MemRefInd<U,Stride>
            res(*this, indcs);
        return res;
    }
    template <typename U, size_t N>
    Duoram::Shape::MemRefInd<U, Stride>
            operator[](const std::array<U,N> &indcs) {
        typename Duoram<T>::Shape::
            template MemRefInd<U,Stride>
            res(*this, indcs);
        return res;
    }
};


// A Path is a Shape that represents a path from the root of a complete
// binary tree down to a given node.

// We assume this layout for the tree (the _parent_ shape of the Path):
// A[0] is unused
// A[1] is the root (layer 0)
// A[2..3] is layer 1
// A[4..7] is layer 2
// ...
// A[(1<<j)..((2<<j)-1)] is layer j
//
// So the parent of x is at location (x/2) and the children of x
// are at locations 2*x and 2*x+1

template <typename T>
class Duoram<T>::Path : public Duoram<T>::Shape {
    size_t target_node;

    inline size_t indexmap(size_t idx) const override {
        size_t paridx = target_node >> (this->shape_size - idx - 1);
        return paridx;
    }

public:
    // Constructor
    Path(Shape &parent, MPCTIO &tio, yield_t &yield,
        size_t target_node);

    // Copy the given Path except for the tio and yield
    Path(const Path &copy_from, MPCTIO &tio, yield_t &yield) :
        Shape(copy_from, tio, yield),
        target_node(copy_from.target_node) {}

    // Update the context (MPCTIO and yield if you've started a new
    // thread, or just yield if you've started a new coroutine in the
    // same thread).  Returns a new Shape with an updated context.
    Path context(MPCTIO &new_tio, yield_t &new_yield) const {
        return Path(*this, new_tio, new_yield);
    }
    Path context(yield_t &new_yield) const {
        return Path(*this, this->tio, new_yield);
    }

    // Index into this Path in various ways
    typename Duoram::Shape::template MemRefS<RegAS,T,std::nullopt_t,Path,1>
            operator[](const RegAS &idx) {
        typename Duoram<T>::Shape::
            template MemRefS<RegAS,T,std::nullopt_t,Path,1>
            res(*this, idx, std::nullopt);
        return res;
    }
    typename Duoram::Shape::template MemRefS<RegXS,T,std::nullopt_t,Path,1>
            operator[](const RegXS &idx) {
        typename Duoram<T>::Shape::
            template MemRefS<RegXS,T,std::nullopt_t,Path,1>
            res(*this, idx, std::nullopt);
        return res;
    }
    template <typename U, nbits_t WIDTH>
    typename Duoram::Shape::template MemRefS<U,T,std::nullopt_t,Path,WIDTH>
            operator[](OblivIndex<U,WIDTH> &obidx) {
        typename Duoram<T>::Shape::
            template MemRefS<RegXS,T,std::nullopt_t,Path,WIDTH>
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
    Duoram::Shape::MemRefInd<U, Path>
            operator[](const std::vector<U> &indcs) {
        typename Duoram<T>::Shape::
            template MemRefInd<U,Path>
            res(*this, indcs);
        return res;
    }
    template <typename U, size_t N>
    Duoram::Shape::MemRefInd<U, Path>
            operator[](const std::array<U,N> &indcs) {
        typename Duoram<T>::Shape::
            template MemRefInd<U,Path>
            res(*this, indcs);
        return res;
    }
};


#include "shapes.tcc"

#endif
