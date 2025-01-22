#include "duoram.hpp"
#include "shapes.hpp"

// Assuming the memory is already sorted, do an oblivious binary
// search for the smallest index containing the value at least the
// given one.  (The answer will be the length of the Shape if all
// elements are smaller than the target.) Only available for additive
// shared databases for now.

// The basic version uses log(N) ORAM reads of size N, where N is the
// smallest power of 2 strictly larger than the Shape size
template <>
RegAS Duoram<RegAS>::Shape::basic_binary_search(RegAS &target)
{
    if (this->shape_size == 0) {
        RegAS zero;
        return zero;
    }
    // // Create a Pad of the smallest power of 2 size strictly greater
    // // than the Shape size
    // address_t padsize = 1;
    // nbits_t depth = 0;
    // while (padsize <= this->shape_size) {
    //     padsize *= 2;
    //     ++depth;
    // }
    // Duoram<RegAS>::Pad P(*this, tio, yield, padsize);
    //
    // // Start in the middle
    // RegAS index;
    // index.set(this->tio.player() ? 0 : (1<<(depth-1))-1);
    // // Invariant: index points to the last element of the left half of
    // // the remaining possible range, which is of width (1<<depth).
    // while (depth > 0) {
    //     // Obliviously read the value there
    //     RegAS val = P[index];
    //     // Compare it to the target
    //     // CDPF cdpf = tio.cdpf(this->yield);
    //     // auto [lt, eq, gt] = cdpf.compare(this->tio, this->yield,
    //     //     val-target, tio.aes_ops());
    //     if (depth > 1) {
    //         // If val >= target, the answer is here or to the left
    //         // and we should subtract 2^{depth-2} from index
    //         // If val < target, the answer is strictly to the right
    //         // and we should add 2^{depth-2} to index
    //         // So we unconditionally subtract 2^{depth-2} from index, and
    //         // add (lt)*2^{depth-1}.
    //         RegAS uncond;
    //         uncond.set(tio.player() ? 0 : address_t(1)<<(depth-2));
    //         RegAS cond;
    //         cond.set(tio.player() ? 0 : address_t(1)<<(depth-1));
    //         RegAS condprod;
    //         mpc_flagmult(this->tio, this->yield, condprod, lt, cond);
    //         index -= uncond;
    //         index += condprod;
    //     } else {
    //         // The possible range is of width 2, and we're pointing to
    //         // the first element of it.
    //         // If val >= target, the answer is here or to the left, so
    //         // it's here.
    //         // If val < target, the answer is strictly to the right
    //         // so add lt to index
    //         RegAS cond;
    //         cond.set(tio.player() ? 0 : 1);
    //         RegAS condprod;
    //         mpc_flagmult(this->tio, this->yield, condprod, lt, cond);
    //         index += condprod;
    //     }
    //     --depth;
    // }
    //
    // return index;
    return RegAS{};
}

// This version does 1 ORAM read of size 2, 1 of size 4, 1 of size
// 8, ..., 1 of size N/2, where N is the smallest power of 2 strictly
// larger than the Shape size
template <>
RegXS Duoram<RegAS>::Shape::binary_search(RegAS &target)
{
    // if (this->shape_size == 0) {
    //     RegXS zero;
    //     return zero;
    // }
    // // Create a Pad of the smallest power of 2 size strictly greater
    // // than the Shape size
    // address_t padsize = 1;
    // nbits_t depth = 0;
    // while (padsize <= this->shape_size) {
    //     padsize *= 2;
    //     ++depth;
    // }
    // Duoram<RegAS>::Pad P(*this, tio, yield, padsize);
    // // Explicitly read the middle item
    // address_t mid = (1<<(depth-1))-1;
    // RegAS val = P[mid];
    // // Compare it to the target
    // CDPF cdpf = tio.cdpf(this->yield);
    // auto [lt, eq, gt] = cdpf.compare(this->tio, this->yield,
    //     val-target, tio.aes_ops());
    // if (depth == 1) {
    //     // There was only one item in the Shape, and mid will equal 0, so
    //     // val is (a share of) that item, P[0].  If val >= target, the
    //     // answer is here or to the left, so it must be 0.  If val <
    //     // target, the answer is strictly to the right, so it must be 1.
    //     // So just return lt.
    //     RegXS ret;
    //     ret.xshare = lt.bshare;
    //     return ret;
    // }
    // auto oidx = P.oblivindex(depth-1);
    // oidx.incr(lt);
    // --depth;
    // while(depth > 0) {
    //     // Create the Stride shape; the ORAM will operate only over
    //     // elements of the Stride, which will consist of exactly those
    //     // elements of the Pad we could possibly be accessing at this
    //     // depth.  Those will be elements start=(1<<(depth-1)-1,
    //     // start+(1<<depth), start+(2<<depth), start+(3<<depth), and so
    //     // on.  The invariant is that the range of remaining possible
    //     // answers is of width (1<<depth), and we will look at the
    //     // rightmost element of the left half.  If that value (val) has
    //     // val >= target, then the answer is at that position or to the
    //     // left, so we append a 0 to the index.  If val < targer, then
    //     // the answer is strictly to the right, so we append a 1 to the
    //     // index.  That is, always append lt to the index.
    //     Duoram<RegAS>::Stride S(P, tio, yield, (1<<(depth-1))-1, 1<<depth);
    //     RegAS val = S[oidx];
    //     CDPF cdpf = tio.cdpf(this->yield);
    //     auto [lt, eq, gt] = cdpf.compare(this->tio, this->yield,
    //         val-target, tio.aes_ops());
    //     oidx.incr(lt);
    //     --depth;
    // }
    // return oidx.index();
    return RegXS{};
}
