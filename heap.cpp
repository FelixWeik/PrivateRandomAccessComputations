#include <functional>
#include "types.hpp"
#include "duoram.hpp"
#include "cell.hpp"
#include "rdpf.hpp"
#include "shapes.hpp"
#include "heap.hpp"


std::ostream& operator<<(std::ostream& os, __uint128_t value) {
    if (value == 0) return os << "0";

    std::string result;
    while (value > 0) {
        result.insert(result.begin(), '0' + value % 10);
        value /= 10;
    }
    return os << result;
}

/*
The heap datastructure is stored in an array with the starting index as 1 (and not 0)
For nodes stored in index i of the array, the parent is stored at i/2 and
The left and right children are stored at 2i and 2i + 1
All the unused array indicies have MAX_INT stored in them

                                 x1
                               /   \
                              x2    x3
                             /  \   / \
                            x4  x5 x6  ()

  A Heap like above is stored in array like below.

  NULL| x1 | x2 | x3 | x4 | x5 | x6 | MAXINT |

*/

/*
The Optimized Insert Protocol
Takes in the additive share of the value to be inserted
and adds the the value into the heap while keeping the heap property intact

 _Protocol 4_ from PRAC: Round-Efficient 3-Party MPC for Dynamic Data Structures
  Consider the following insertion path with:  x0 < x1 < x2 < NewElement < x3 < x4

        x0                      x0                               x0
        / \                    /  \                             /  \
          x1                      x1                                x1
         /                        /                                 /
        x2                       x2                                x2
         \                        \                                 \
          x3                      ( )                               NewElement
           \                        \                                 \
            x4                       x3                                x3
           /                        /                                 /
         ( )                       x4                                x4

      (Path with new element)       (binary search to determine             (After insertion)
                                     the point where New Element
                                     should be and shift the elements
                                     from that point down the path
                                     from the point)

 The insert protocol begins by adding an empty node at the end of the heap array
 The key observation is that after the insert operation, the only entries that might change are the ones on the path from the root to the new node
 The path from the root to the new node is determined based on the number of entries in the heap, which is publicly known
 The observation is that this path starts off sorted and will end up with the new element (NewElement) inserted into the correct position, preserving the sorted property of the path
 The length of the path is logarithmic with respect to the heap size (path length = log(heap size))
 To find the appropriate insertion position, we use binary search with a single IDPF of height logarithmic with respect to the logarithm of the heap size (IDPF height = log(log(heap size)))
 The advice bits of the IDPF correspond to the bit shares of a vector 'flag' with a single '1' indicating the position where the new value (insertval) must be inserted.
 The shares of 'flag' are locally converted to shares of a vector 'u = [000011111]' using running XORs.
 The bits of 'flag' and 'u' are then used in parallel Flag-Word multiplications, totaling 2 times the logarithm of the heap size, to shift the elements greater than 'insertval' down one position
 And write 'insertval' into the resulting empty location in the path
 This process requires a single message of communication
 The protocol requires one binary search on a database of size log(heap size) (height of the tree)
 Overall, the insert protocol achieves efficient insertion of a new element into the heap, with a complexity of log(log(heap size)) oblivious comparisons
 and 2 x log(heap size) flag multiplications. The flag multiplications
 are all done in a single round.
*/
void MinHeap::insert_optimized(MPCTIO &tio, yield_t & yield, RegAS val) {
    auto HeapArray = oram.flat(tio, yield);
    num_items++;
    typename Duoram<RegAS>::Path P(HeapArray, tio, yield, num_items);
    const RegXS foundidx = P.binary_search(val);
    size_t childindex = num_items;
    // height is the number of nodes on the path from root to the leaf
    uint64_t height = P.size();
    RegAS zero;
    HeapArray[childindex] = zero;


    #ifdef HEAP_VERBOSE
    uint64_t val_reconstruction = mpc_reconstruct(tio, yield, val);
    std::cout << "val_reconstruction = " << val_reconstruction << std::endl;
    #endif

    uint64_t  logheight = std::floor(double(std::log2(height))) + 1;

    std::vector<RegBS> flag;
    std::vector<RegBS> u(height);
    typename Duoram<RegAS>::template OblivIndex<RegXS,1> oidx(tio, yield, foundidx, logheight);
    flag = oidx.unit_vector(tio, yield, height, foundidx);

    #ifdef HEAP_VERBOSE
    uint64_t foundidx_reconstruction = mpc_reconstruct(tio, yield, foundidx);
    std::cout << "foundidx_reconstruction = " << foundidx_reconstruction << std::endl;
    std::cout << std::endl << " =============== " << std::endl;
    for (size_t j = 0; j < height; ++j) {
        uint64_t reconstruction = mpc_reconstruct(tio, yield, flag[j]);
        std::cout << " --->> flag[" << j << "] = " << reconstruction  <<  std::endl;
    }
    #endif

    for (size_t j = 0; j < height; ++j) {
        if(j > 0) {
            u[j] = flag[j] ^ u[j-1];
        } else {
            u[j] = flag[j];
        }
    }

    #ifdef HEAP_VERBOSE
    for (size_t j = 0; j < height; ++j) {
        uint64_t reconstruction = mpc_reconstruct(tio, yield, u[j]);
        std::cout << " --->> [0000111111]][" << j << "] = " << reconstruction << std::endl;
    }
    #endif

    std::vector<RegAS> path(height);
    std::vector<RegAS> w(height);
    std::vector<RegAS> v(height);

    for (size_t j = 0; j < height; ++j) path[j] = P[j];

    std::vector<coro_t> coroutines;
    for (size_t j = 0; j < height; ++j) {
        if (j > 0) {
            coroutines.emplace_back(
                    [&tio, &w, &u, &path, j](yield_t &yield) {
                mpc_flagmult(tio, yield, w[j], u[j-1], path[j-1]-path[j]);
            }
            );
        }
        coroutines.emplace_back(
                [&tio, &v, flag, val, &path, j](yield_t &yield) {
            mpc_flagmult(tio, yield, v[j], flag[j], val - path[j]);
        }
        );
    }

    run_coroutines(tio, coroutines);

    #ifdef HEAP_VERBOSE
    std::cout << "\n\n=================Before===========\n\n";
    auto path_rec_before = P.reconstruct();
    for (size_t j = 0; j < height; ++j) {
        std::cout << j << " --->: " << path_rec_before[j].share() << std::endl;
    }
    std::cout << "\n\n============================\n\n";
    #endif

    coroutines.clear();

    for (size_t j = 0; j < height; ++j) {
        coroutines.emplace_back( [&tio, &v, &w, &P, j](yield_t &yield) {
            auto Pcoro = P.context(yield);
            Pcoro[j] += (w[j] + v[j]);
        });
    }
    run_coroutines(tio, coroutines);

    #ifdef HEAP_VERBOSE
    std::cout << "\n\n=================After===========\n\n";
    auto path_rec_after = P.reconstruct();
    for (size_t j = 0; j < height; ++j) {
        std::cout << j << " --->: " << path_rec_after[j].share() << std::endl;
    }
    std::cout << "\n\n============================\n\n";
    #endif
}

// The Basic Insert Protocol
// Takes in the additive share of the value to be inserted
// And adds the the value into the heap while keeping the heap property intact
// The insert protocol works as follows:
// Step 1: Add a new element to the last entry of the array.
// This new element becomes a leaf in the heap.
// Step 2: Starting from the leaf (the newly added element), compare it with its parent.
// Perform 1 oblivious comparison to determine if the parent is greater than the child.
// Step 3: If the parent is greater than the child, swap them obliviously to maintain the heap property.
// This swap ensures that the parent is always greater than both its children.
// Step 4: Continue moving up the tree by repeating steps 2 and 3 until we reach the root.
// This process ensures that the newly inserted element is correctly positioned in the heap.
// The total cost of the insert protocol is log(num_items) oblivious comparisons and log(num_items) oblivious swaps.
// This protocol follows the approach described as Protocol 3 in the paper "PRAC: Round-Efficient 3-Party MPC for Dynamic Data Structures."
void MinHeap::insert(MPCTIO &tio, yield_t & yield, RegAS val) {

    auto HeapArray = oram.flat(tio, yield);
    num_items++;
    size_t childindex = num_items;
    size_t parentindex = childindex / 2;

    #ifdef HEAP_VERBOSE
    std::cout << "childindex = " << childindex << std::endl;
    std::cout << "parentindex = " << parentindex << std::endl;
    #endif

    HeapArray[num_items] = val;

    while (parentindex > 0) {
        RegAS sharechild = HeapArray[childindex];
        RegAS shareparent = HeapArray[parentindex];
        CDPF cdpf = tio.cdpf(yield);
        RegAS diff = sharechild - shareparent;
        auto[lt, eq, gt] = cdpf.compare(tio, yield, diff, tio.aes_ops());
        mpc_oswap(tio, yield, sharechild, shareparent, lt);
        HeapArray[childindex]  = sharechild;
        HeapArray[parentindex] = shareparent;
        childindex = parentindex;
        parentindex = parentindex / 2;
    }
}



// Note: This function is intended for testing purposes only.
// The purpose of this function is to verify that the heap property is satisfied.
// The function checks if the heap property holds for the given heap structure. It ensures that for each node in the heap, the value of the parent node is less than or equal to the values of its children.
// By calling this function during debugging, you can validate the integrity of the heap structure and ensure that the heap property is maintained correctly.
// It is important to note that this function is not meant for production use and should be used solely for  testing purposes.
void MinHeap::verify_heap_property(MPCTIO &tio, yield_t & yield) {

    #ifdef HEAP_VERBOSE
    std::cout << std::endl << std::endl << "verify_heap_property is being called " << std::endl;
    #endif

    auto HeapArray = oram.flat(tio, yield);

    auto heapreconstruction = HeapArray.reconstruct();

    #ifdef HEAP_VERBOSE
     for (size_t j = 1; j < num_items + 1; ++j) {
            if(tio.player() < 2) std::cout << j << " -----> heapreconstruction[" << j << "] = " << heapreconstruction[j].share() << std::endl;
        }
    #endif

    for (size_t j = 2; j <= num_items; ++j) {
        if (heapreconstruction[j/2].share() > heapreconstruction[j].share()) {
            std::cout << "heap property failure\n\n";
            std::cout << "j = " << j << std::endl;
            std::cout << heapreconstruction[j].share() << std::endl;
            std::cout << "j/2 = " << j/2 << std::endl;
            std::cout << heapreconstruction[j/2].share() << std::endl;
        }

        assert(heapreconstruction[j/2].share() <= heapreconstruction[j].share());
    }

}



#ifdef HEAP_DEBUG
// Note: This function is intended for debugging purposes only.
// The purpose of this function is to assert the fact that the reconstruction values of both the left child and right child are greater than or equal to the reconstruction value of the parent.
// The function performs an assertion check to validate this condition. If the condition is not satisfied, an assertion error will be triggered.
// This function is useful for verifying the correctness of reconstruction values during debugging and ensuring the integrity of the heap structure.
// It is important to note that this function is not meant for production use and should be used solely for debugging purposes.
static void verify_parent_children_heaps(MPCTIO &tio, yield_t & yield, RegAS parent, RegAS leftchild, RegAS rightchild) {
    uint64_t parent_reconstruction = mpc_reconstruct(tio, yield, parent);
    uint64_t leftchild_reconstruction = mpc_reconstruct(tio, yield, leftchild);
    uint64_t rightchild_reconstruction = mpc_reconstruct(tio, yield, rightchild);
    std::cout << "parent_reconstruction = " << parent_reconstruction << std::endl;
    std::cout << "leftchild_reconstruction = " << leftchild_reconstruction << std::endl;
    std::cout << "rightchild_reconstruction = " << rightchild_reconstruction << std::endl << std::endl << std::endl;
    assert(parent_reconstruction <= leftchild_reconstruction);
    assert(parent_reconstruction <= rightchild_reconstruction);
}
#endif

/*
Protocol 6 from PRAC: Round-Efficient 3-Party MPC for Dynamic Data Structures
Takes in as an input the XOR shares of the index at which the heap property has to be restored
Returns the XOR shares of the index of the smaller child

Basic restore heap property has the following functionality:

Before restoring heap property:                      z
                                                    /  \
                                                   y    x

After restoring heap property:        if(y < x AND z < y)       if(y < x AND z > y)        if(y > x AND z < x)           if(y > x AND z > x)

                                                z                         y                        z                              x
                                               /  \                      / \                      / \                            / \
                                              y    x                    z   x                    y    x                         y   z


The function is relying on the "unused" entries in the heap being MAXINT

The protocol works as follows:

Step 1: Compare the left and right children.
Step 2: Compare the smaller child with the parent.
If the smaller child is smaller than the parent, swap the smaller child with the root.

The protocol requires three DORAM (Distributed Oblivious RAM) reads performed in parallel:
- Read the parent, left child, and right child.

Two comparisons are performed:
a) Comparison between the left and right child.
b) Comparison between the smaller child and the parent.

Two MPC-selects are performed in parallel:
- Computing the smaller child and its index using MPC-select operations.

Next, the offsets by which the parent and children need to be updated are computed.
Offset computation involves:
- One flag-flag multiplication.
- Two flag-word multiplications performed in parallel.

Three DORAM update operations are performed in parallel:
- Update the parent, left child, and right child.

The function returns the XOR-share of the smaller child's index.

The total cost of the protocol includes:
- 3 DORAM reads (performed in parallel).
- 2 comparisons.
- 2 MPC-selects (performed in parallel).
- 1 flag-flag multiplication.
- 2 flag-word multiplications (performed in parallel).
- 3 DORAM updates (performed in parallel).
*/
RegXS MinHeap::restore_heap_property(MPCTIO &tio, yield_t & yield, RegXS index) {
    RegAS smallest;
    auto HeapArray = oram.flat(tio, yield);
    RegXS leftchildindex = index;
    leftchildindex = index << 1;
    RegXS rightchildindex;
    rightchildindex.xshare = leftchildindex.xshare ^ (!tio.player());

    RegAS parent, leftchild, rightchild;

    #ifdef HEAP_VERBOSE
    auto index_reconstruction = mpc_reconstruct(tio, yield, index);
    auto leftchildindex_reconstruction = mpc_reconstruct(tio, yield, leftchildindex);
    auto rightchildindex_reconstruction = mpc_reconstruct(tio, yield, rightchildindex);
    std::cout << "index_reconstruction               =  " << index_reconstruction << std::endl;
    std::cout << "leftchildindex_reconstruction      =  " << leftchildindex_reconstruction << std::endl;
    std::cout << "rightchildindex_reconstruction     =  " << rightchildindex_reconstruction << std::endl;
    #endif

   run_coroutines(tio, [&tio, &parent, &HeapArray, index](yield_t &yield) {
                  auto Acoro = HeapArray.context(yield);
                  parent = Acoro[index];},
                  [&tio, &HeapArray, &leftchild, leftchildindex](yield_t &yield) {
                  auto Acoro = HeapArray.context(yield);
                  leftchild  = Acoro[leftchildindex];},
                  [&tio, &rightchild, &HeapArray, rightchildindex](yield_t &yield) {
                  auto Acoro = HeapArray.context(yield);
                  rightchild = Acoro[rightchildindex];});

    CDPF cdpf = tio.cdpf(yield);
    auto[lt_c, eq_c, gt_c] = cdpf.compare(tio, yield, leftchild - rightchild, tio.aes_ops());

    RegXS smallerindex;
    RegAS smallerchild;

    run_coroutines(tio, [&tio, &smallerindex, lt_c, rightchildindex, leftchildindex](yield_t &yield) {
        mpc_select(tio, yield, smallerindex, lt_c, rightchildindex, leftchildindex);
    },  [&tio, &smallerchild, lt_c, rightchild, leftchild](yield_t &yield) {
        mpc_select(tio, yield, smallerchild, lt_c, rightchild, leftchild);
    }
    );

    CDPF cdpf0 = tio.cdpf(yield);
    auto[lt_p, eq_p, gt_p] = cdpf0.compare(tio, yield, smallerchild - parent, tio.aes_ops());

    RegBS ltlt1;

    mpc_and(tio, yield, ltlt1, lt_c, lt_p);

    RegAS update_index_by, update_leftindex_by;

    run_coroutines(tio, [&tio, &update_leftindex_by, ltlt1, parent, leftchild](yield_t &yield) {
        mpc_flagmult(tio, yield, update_leftindex_by, ltlt1, parent - leftchild);
    },  [&tio, &update_index_by, lt_p, parent, smallerchild](yield_t &yield) {
        mpc_flagmult(tio, yield, update_index_by, lt_p, smallerchild - parent);
    }
    );


    run_coroutines(tio, [&tio, &HeapArray, index, update_index_by](yield_t &yield) {
                   auto Acoro = HeapArray.context(yield);
                   Acoro[index] += update_index_by;},
                   [&tio, &HeapArray, leftchildindex, update_leftindex_by](yield_t &yield) {
                   auto Acoro = HeapArray.context(yield);
                   Acoro[leftchildindex] += update_leftindex_by;},
                   [&tio, &HeapArray, rightchildindex, update_index_by, update_leftindex_by](yield_t &yield) {
                   auto Acoro = HeapArray.context(yield);
                   Acoro[rightchildindex] += -(update_index_by + update_leftindex_by);});

    #ifdef HEAP_DEBUG
            verify_parent_children_heaps(tio, yield, HeapArray[index], HeapArray[leftchildindex] , HeapArray[rightchildindex]);
    #endif

    return smallerindex;
}

// This Protocol 7 is derived from PRAC: Round-Efficient 3-Party MPC for Dynamic Data Structures
// Takes in as an input the XOR shares of the index at which
// the heap property has to be restored
// Returns the XOR shares of the index of the smaller child and
// comparison between the left and right child
// This protocol represents an optimized version of restoring the heap property
// The key difference between the optimized and basic versions is that the optimized version utilizes a wide DPF (Distributed Point Function) for reads and writes
// In addition to restoring the heap property, the function also returns
// shares of the index of the smaller child, and the result of the
// comparison (leftchild > rightchild)
// The (leftchild > rightchild) comparison is utilized in the extract_min operation to increment the oblivindx by a certain value
// The function restores the heap property at node index
// The parameter layer is the height at which the node at index lies
// The optimized version achieves improved efficiency by leveraging wide DPF operations for read and write operations
std::pair<RegXS, RegBS> MinHeap::restore_heap_property_optimized(MPCTIO &tio, yield_t & yield, RegXS index, size_t layer, typename Duoram < RegAS > ::template OblivIndex < RegXS, 3 > oidx) {

    auto HeapArray = oram.flat(tio, yield);

    RegXS leftchildindex = index;
    leftchildindex = index << 1;

    RegXS rightchildindex;
    rightchildindex.xshare = leftchildindex.xshare ^ (!tio.player());

    typename Duoram < RegAS > ::Flat P(HeapArray, tio, yield, 1 << layer, 1 << layer);
    typename Duoram < RegAS > ::Flat C(HeapArray, tio, yield, 2 << layer, 2 << layer);
    typename Duoram < RegAS > ::Stride L(C, tio, yield, 0, 2);
    typename Duoram < RegAS > ::Stride R(C, tio, yield, 1, 2);

    RegAS parent, leftchild, rightchild;

    run_coroutines(tio, [&tio, &parent, &P, &oidx](yield_t &yield) {
                    auto Pcoro = P.context(yield);
                    parent = Pcoro[oidx]; },
                    [&tio, &L, &leftchild, &oidx](yield_t &yield) {
                    auto Lcoro = L.context(yield);
                    leftchild  = Lcoro[oidx];},
                    [&tio, &R, &rightchild, &oidx](yield_t &yield) {
                    auto Rcoro = R.context(yield);
                    rightchild = Rcoro[oidx];
                  });

    CDPF cdpf = tio.cdpf(yield);

    auto[lt_c, eq_c, gt_c] = cdpf.compare(tio, yield, leftchild - rightchild, tio.aes_ops());

    RegXS smallerindex;
    RegAS smallerchild;

    run_coroutines(tio, [&tio, &smallerindex, lt_c, rightchildindex, leftchildindex](yield_t &yield) {
        mpc_select(tio, yield, smallerindex, lt_c, rightchildindex, leftchildindex);
    },  [&tio, &smallerchild, lt_c, rightchild, leftchild](yield_t &yield) {
        mpc_select(tio, yield, smallerchild, lt_c, rightchild, leftchild);
    }
    );

    CDPF cdpf0 = tio.cdpf(yield);
    auto[lt_p, eq_p, gt_p] = cdpf0.compare(tio, yield, smallerchild - parent, tio.aes_ops());

    RegBS ltlt1;

    mpc_and(tio, yield, ltlt1, lt_c, lt_p);

    RegAS update_index_by, update_leftindex_by;


    run_coroutines(tio, [&tio, &update_leftindex_by, ltlt1, parent, leftchild](yield_t &yield) {
    mpc_flagmult(tio, yield, update_leftindex_by, ltlt1, parent - leftchild);
    },  [&tio, &update_index_by, lt_p, parent, smallerchild](yield_t &yield) {
        mpc_flagmult(tio, yield, update_index_by, lt_p, smallerchild - parent);
    }
    );

    run_coroutines(tio, [&tio, &P, &oidx, update_index_by](yield_t &yield) {
                    auto Pcoro = P.context(yield);
                    Pcoro[oidx] += update_index_by;},
                    [&tio, &L,  &oidx, update_leftindex_by](yield_t &yield) {
                    auto Lcoro = L.context(yield);
                    Lcoro[oidx] += update_leftindex_by;},
                    [&tio, &R,  &oidx, update_leftindex_by, update_index_by](yield_t &yield) {
                    auto Rcoro = R.context(yield);
                    Rcoro[oidx] += -(update_leftindex_by + update_index_by);
                    });

    auto gteq = gt_c ^ eq_c;

    return {smallerindex, gteq};
}


// Intializes the heap array with 0x7fffffffffffffff
void MinHeap::init(MPCTIO &tio, yield_t & yield) {
    auto HeapArray = oram.flat(tio, yield);
    HeapArray.init(0x7fffffffffffffff);
    num_items = 0;
}


// This function simply inits a heap with values 100,200,...,100*n
// We use this function only to set up our heap
// to do timing experiments on insert and extractmins
void MinHeap::init(MPCTIO &tio, yield_t & yield, size_t n) {
    auto HeapArray = oram.flat(tio, yield);

    num_items = n;
    HeapArray.init([n](size_t i) {
        if (i >= 1 && i <= n) {
            return i*100;
        } else {
            return size_t(0x7fffffffffffffff);
        }
    });
}

// Note: This function is intended for debugging purposes only.
// The purpose of this function is to reconstruct the heap and print its contents.
// The function performs the necessary operations to reconstruct the heap, ensuring that the heap property is satisfied. It then prints the contents of the reconstructed heap.
// This function is useful for debugging and inspecting the state of the heap at a particular point in the program execution.
// It is important to note that this function is not meant for production use and should be used solely for debugging purposes.
void MinHeap::print_heap(MPCTIO &tio, yield_t & yield) {
    auto HeapArray = oram.flat(tio, yield);
    auto Pjreconstruction = HeapArray.reconstruct();
    for (size_t j = 1; j <= num_items; ++j) {
        if(2 * j <= num_items) {
            std::cout << j << "-->> HeapArray[" << j << "] = " <<   Pjreconstruction[j].share() << ", children are: " << Pjreconstruction[2 * j].share() << " and " << Pjreconstruction[2 * j + 1].share() <<  std::endl;
        } else {
            std::cout << j << "-->> HeapArray[" << j << "] = " << std::dec << Pjreconstruction[j].share() << " is a LEAF " <<  std::endl;
        }
    }
}


/*
Restore the head property at an explicit index (typically the root).
the only reason this function exists is because at the root level
the indices to read (the root and its two children) are explicit and not shared
Restore heap property at an index in clear
Takes in as an input the index (in clear) at which
the heap property has to be restored

                root
                /  \
       leftchild    rightchild

After restoring heap property:
if(leftchild < rightchild AND root < leftchild)       if(leftchild < rightchild AND root > leftchild)     if(leftchild > rightchild AND root < rightchild)     if(leftchild > rightchild AND root > rightchild)


                 root                                                        leftchild                                         root                                            rightchild
               /     \                                                        /   \                                           /    \                                           /      \
         leftchild    rightchild                                           root   rightchild                          leftchild    rightchild                            leftchild    root


The restore_heap_property_at_explicit_index protocol works as follows:

Step 1: Compare the left and right children.
Step 2: Compare the smaller child with the root.
If the smaller child is smaller than the root, swap the smaller child with the root.
Unlike the restore_heap_property protocol, restore_heap_property_at_explicit_index begins with three explicit-index (non-DORAM) read operations:
- Read the parent, left child, and right child.
Two comparisons are performed:
a) Comparison between the left and right child.
b) Comparison between the smaller child and the parent.
The above comparisons have to be sequential because we need to find the smallerindex and smallerchild,
which is dependent on the first comparison
Next, the offsets by which the parent and children need to be updated are computed.
Offset computation involves:
- One flag-flag multiplication.
- Two flag-word multiplications.
Three explicit-index (non-DORAM) update operations are required (performed in parallel) to update the parent, left child, and right child.
In total, this protocol requires:
- 2 comparisons.
- 1 flag-flag multiplication.
- 2 flag-word multiplications.
- 3 explicit-index (non-DORAM) reads and updates.
The function returns a pair of a) XOR-share of the index of the smaller child and b) the comparison between left and right children
*/
std::pair<RegXS, RegBS> MinHeap::restore_heap_property_at_explicit_index(MPCTIO &tio, yield_t & yield, size_t index = 1) {
    auto HeapArray = oram.flat(tio, yield);
    RegAS parent = HeapArray[index];
    RegAS leftchild = HeapArray[2 * index];
    RegAS rightchild = HeapArray[2 * index + 1];
    CDPF cdpf = tio.cdpf(yield);
    auto[lt, eq, gt] = cdpf.compare(tio, yield, leftchild - rightchild, tio.aes_ops());

    auto gteq = gt ^ eq;
    RegAS smallerchild;
    mpc_select(tio, yield, smallerchild, lt, rightchild, leftchild);

    uint64_t leftchildindex = (2 * index);
    uint64_t rightchildindex = (2 * index) + 1;
    RegXS smallerindex = (RegXS(lt) & leftchildindex) ^ (RegXS(gteq) & rightchildindex);
    CDPF cdpf0 = tio.cdpf(yield);
    auto[lt1, eq1, gt1] = cdpf0.compare(tio, yield, smallerchild - parent, tio.aes_ops());
    RegBS ltlt1;

    mpc_and(tio, yield, ltlt1, lt, lt1);
    RegAS update_index_by, update_leftindex_by;

    run_coroutines(tio, [&tio, &update_leftindex_by, ltlt1, parent, leftchild](yield_t &yield) {
        mpc_flagmult(tio, yield, update_leftindex_by, ltlt1, parent - leftchild);
    }, [&tio, &update_index_by, lt1, parent, smallerchild](yield_t &yield) {
        mpc_flagmult(tio, yield, update_index_by, lt1, smallerchild - parent);});

    run_coroutines(tio,
        [&tio, &HeapArray, &update_index_by, index](yield_t &yield) {
            auto HeapArraycoro = HeapArray.context(yield);
            HeapArraycoro[index] += update_index_by;
        },
        [&tio, &HeapArray, &update_leftindex_by, leftchildindex](yield_t &yield) {
            auto HeapArraycoro = HeapArray.context(yield);
            HeapArraycoro[leftchildindex] += update_leftindex_by;
        },
        [&tio, &HeapArray, &update_index_by, &update_leftindex_by, rightchildindex](yield_t &yield) {
            auto HeapArraycoro = HeapArray.context(yield);
            HeapArraycoro[rightchildindex] += -(update_index_by + update_leftindex_by);
        });

    #ifdef HEAP_VERBOSE
    RegAS new_parent = HeapArray[index];
    RegAS new_left   = HeapArray[leftchildindex];
    RegAS new_right  = HeapArray[rightchildindex];
    uint64_t parent_R  = mpc_reconstruct(tio, yield, new_parent);
    uint64_t left_R    = mpc_reconstruct(tio, yield, new_left);
    uint64_t right_R   = mpc_reconstruct(tio, yield, new_right);
    std::cout << "parent_R = " << parent_R << std::endl;
    std::cout << "left_R = " << left_R << std::endl;
    std::cout << "right_R = " << right_R << std::endl;
    #endif

    #ifdef HEAP_DEBUG
    verify_parent_children_heaps(tio, yield, HeapArray[index], HeapArray[leftchildindex] , HeapArray[rightchildindex]);
    #endif

    return {smallerindex, gteq};
}


// This Protocol 5 from PRAC: Round-Efficient 3-Party MPC for Dynamic Data Structures
// The extractmin protocol returns the minimum element (the root), removes it
// and restores the heap property
// The function extract_min cannot be called on an empty heap
// Like in the paper, there is only one version of extract_min
// and takes in a boolean parameter to decide if the basic or the optimized version needs to be run
// the optimized version calls the optimized restore_heap_property with everything else remaing the same
// The extractmin algorithm removes the root and replaces it with last leaf node
// After extracting the minimum element from the heap, the heap property is temporarily violated.
// To restore the heap property, we begin at the root layer.
// Step 1: Swap the root with the smaller child if the smaller child is less than the root.
// This step is performed by the function restore_heap_property_at_explicit_index.
// Step 2: Proceed down the tree along the path of the smaller child.
// Repeat the process of swapping the parent with the smaller child if the parent is greater than the smaller child.
// After the swap, make the smaller child the new parent.
// The choice of whether to use restore_heap_property or restore_heap_property_optimized
// depends on whether it is a basic or optimized extraction of the minimum element.
// These functions ensure that the heap property is maintained throughout the tree.
RegAS MinHeap::extract_min(MPCTIO &tio, yield_t & yield, int is_optimized) {

    size_t height = std::log2(num_items);
    RegAS minval;
    auto HeapArray = oram.flat(tio, yield);
    minval = HeapArray[1];
    HeapArray[1] = RegAS(HeapArray[num_items]);
    RegAS v;
    v.ashare = 0x7fffffffffffffff * !tio.player();
    HeapArray[num_items] = v;
    num_items--;

    // If this was the last item, just return it
    if (num_items == 0) {
        return minval;
    }

    auto outroot = restore_heap_property_at_explicit_index(tio, yield);
    RegXS smaller = outroot.first;

    if(is_optimized > 0) {
        typename Duoram < RegAS > ::template OblivIndex < RegXS, 3 > oidx(tio, yield, height);
        oidx.incr(outroot.second);

        for (size_t i = 0; i < height-1; ++i) {
            auto out = restore_heap_property_optimized(tio, yield, smaller, i + 1,  oidx);
            smaller = out.first;
            oidx.incr(out.second);
        }
    }

    if(is_optimized == 0) {
        for (size_t i = 0; i < height - 1; ++i) {
            smaller = restore_heap_property(tio, yield, smaller);
        }
    }

    return minval;
}



void Heap(MPCIO & mpcio,  const PRACOptions & opts, char ** args) {


    MPCTIO tio(mpcio, 0, opts.num_cpu_threads);

    int nargs = 0;

    while (args[nargs] != nullptr) {
        ++nargs;
    }

    int maxdepth = 0;
    int heapdepth = 0;
    size_t n_inserts = 0;
    size_t n_extracts = 0;
    int is_optimized = 0;
    int run_sanity = 0;

    for (int i = 0; i < nargs; i += 2) {
        std::string option = args[i];
        if (option == "-m" && i + 1 < nargs) {
            maxdepth = std::atoi(args[i + 1]);
        } else if (option == "-d" && i + 1 < nargs) {
            heapdepth = std::atoi(args[i + 1]);
        } else if (option == "-i" && i + 1 < nargs) {
            n_inserts = std::atoi(args[i + 1]);
        } else if (option == "-e" && i + 1 < nargs) {
            n_extracts = std::atoi(args[i + 1]);
        } else if (option == "-opt" && i + 1 < nargs) {
            is_optimized = std::atoi(args[i + 1]);
        } else if (option == "-s" && i + 1 < nargs) {
            run_sanity = std::atoi(args[i + 1]);
        }
    }

    run_coroutines(tio, [ & tio, maxdepth, heapdepth, n_inserts, n_extracts, is_optimized, run_sanity, &mpcio](yield_t & yield) {
        size_t size = size_t(1) << maxdepth;
        MinHeap tree(tio.player(), size);
        // This form of init with a third parameter of n sets the heap
        // to contain 100, 200, 300, ..., 100*n.
        tree.init(tio, yield, (size_t(1) << heapdepth) - 1);
        std::cout << "\n===== Init Stats =====\n";
        tio.sync_lamport();
        mpcio.dump_stats(std::cout);
        mpcio.reset_stats();
        tio.reset_lamport();
        for (size_t j = 0; j < n_inserts; ++j) {

            RegAS inserted_val;
            inserted_val.randomize(8);

            #ifdef HEAP_VERBOSE
            inserted_val.ashare = inserted_val.ashare;
            uint64_t inserted_val_rec = mpc_reconstruct(tio, yield, inserted_val);
            std::cout << "inserted_val_rec = " << inserted_val_rec << std::endl << std::endl;
            #endif

            if(is_optimized > 0)  tree.insert_optimized(tio, yield, inserted_val);
            if(is_optimized == 0) tree.insert(tio, yield, inserted_val);
        }

        std::cout << "\n===== Insert Stats =====\n";
        tio.sync_lamport();
        mpcio.dump_stats(std::cout);


        if(run_sanity == 1 && n_inserts != 0) tree.verify_heap_property(tio, yield);


        mpcio.reset_stats();
        tio.reset_lamport();

        #ifdef HEAP_VERBOSE
        tree.print_heap(tio, yield);
        #endif

        bool have_lastextract = false;
        uint64_t lastextract = 0;

        for (size_t j = 0; j < n_extracts; ++j) {

            if(run_sanity == 1) {
                RegAS minval = tree.extract_min(tio, yield, is_optimized);
                uint64_t minval_reconstruction = mpc_reconstruct(tio, yield, minval);
                std::cout << "minval_reconstruction = " << minval_reconstruction << std::endl;
                if (have_lastextract) {
                    assert(minval_reconstruction >= lastextract);
                }
                lastextract = minval_reconstruction;
                have_lastextract = true;
            } else {
                tree.extract_min(tio, yield, is_optimized);
            }

            if (run_sanity == 1) {
                tree.verify_heap_property(tio, yield);
            }

            #ifdef HEAP_VERBOSE
            tree.print_heap(tio, yield);
            #endif
       }

       std::cout << "\n===== Extract Min Stats =====\n";
       tio.sync_lamport();
       mpcio.dump_stats(std::cout);

       #ifdef HEAP_VERBOSE
       tree.print_heap(tio, yield);
       #endif


       if(run_sanity == 1 && n_extracts != 0) tree.verify_heap_property(tio, yield);

    }
    );
}
