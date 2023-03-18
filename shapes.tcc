#ifndef __SHAPES_TCC__
#define __SHAPES_TCC__

// Constructor for the Pad shape.
template <typename T>
Duoram<T>::Pad::Pad(Shape &parent, MPCTIO &tio, yield_t &yield,
    address_t padded_size, size_t padval) :
    Shape(parent, parent.duoram, tio, yield)
{
    int player = tio.player();
    padvalp = new T;
    padvalp->set(player*padval);
    zerop = new T;
    peerpadvalp = new T;
    peerpadvalp->set((1-player)*padval);
    this->set_shape_size(padded_size);
}

// Copy the given Pad except for the tio and yield
template <typename T>
Duoram<T>::Pad::Pad(const Pad &copy_from, MPCTIO &tio, yield_t &yield) :
    Shape(copy_from, tio, yield)
{
    padvalp = new T;
    padvalp->set(copy_from.padvalp->share());
    zerop = new T;
    peerpadvalp = new T;
    peerpadvalp->set(copy_from.peerpadvalp->share());
}

// Destructor
template <typename T>
Duoram<T>::Pad::~Pad()
{
    delete padvalp;
    delete zerop;
    delete peerpadvalp;
}

// Constructor for the Stride shape.
template <typename T>
Duoram<T>::Stride::Stride(Shape &parent, MPCTIO &tio, yield_t &yield,
    size_t offset, size_t stride) :
    Shape(parent, parent.duoram, tio, yield)
{
    size_t parentsize = parent.size();
    if (offset > parentsize) {
        offset = parentsize;
    }
    this->offset = offset;
    this->stride = stride;
    // How many items are there if you take every stride'th item,
    // starting at offset?  strideregionsize corrects for the offset, so
    // we're asking how many multiples of stride are there strictly less
    // than strideregionsize.  That's just ceil(strideregionsize/stride)
    // which is the same as (strideregionsize + stride - 1)/stride with
    // integer truncated division.
    size_t strideregionsize = parentsize - offset;
    size_t numelements = (strideregionsize + stride - 1) / stride;
    this->set_shape_size(numelements);
}

#endif