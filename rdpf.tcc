#ifndef RDPF_TCC
#define RDPF_TCC

// Templated method implementations for rdpf.hpp

#include "mpcops.hpp"

// Compute the multiplicative inverse of x mod 2^{VALUE_BITS}
// This is the same as computing x to the power of
// 2^{VALUE_BITS-1}-1.
static value_t inverse_value_t(value_t x)
{
    int expon = 1;
    value_t xe = x;
    // Invariant: xe = x^(2^expon - 1) mod 2^{VALUE_BITS}
    // Goal: compute x^(2^{VALUE_BITS-1} - 1)
    while (expon < VALUE_BITS-1) {
        xe = xe * xe * x;
        ++expon;
    }
    return xe;
}

// Create a StreamEval object that will start its output at index start.
// It will wrap around to 0 when it hits 2^depth.  If use_expansion
// is true, then if the DPF has been expanded, just output values
// from that.  If use_expansion=false or if the DPF has not been
// expanded, compute the values on the fly.  If xor_offset is non-zero,
// then the outputs are actually
// DPF(start XOR xor_offset)
// DPF((start+1) XOR xor_offset)
// DPF((start+2) XOR xor_offset)
// etc.
template <typename T>
StreamEval<T>::StreamEval(const T &rdpf, address_t start,
    address_t xor_offset, size_t &aes_ops,
    bool use_expansion) : rdpf(rdpf), aes_ops(aes_ops),
    use_expansion(use_expansion), counter_xor_offset(xor_offset)
{
    depth = rdpf.depth();
    // Prevent overflow of 1<<depth
    if (depth < ADDRESS_MAX_BITS) {
        indexmask = (address_t(1)<<depth)-1;
    } else {
        indexmask = ~0;
    }
    start &= indexmask;
    counter_xor_offset &= indexmask;
    // Record that we haven't actually output the leaf for index start
    // itself yet
    nextindex = start;
    if (use_expansion && rdpf.has_expansion()) {
        // We just need to keep the counter, not compute anything
        return;
    }
    path.resize(depth);
    pathindex = start;
    path[0] = rdpf.get_seed();
    for (nbits_t i=1;i<depth;++i) {
        bool dir = !!(pathindex & (address_t(1)<<(depth-i)));
        bool xor_offset_bit =
            !!(counter_xor_offset & (address_t(1)<<(depth-i)));
        path[i] = rdpf.descend(path[i-1], i-1,
            dir ^ xor_offset_bit, aes_ops);
    }
}

template <typename T>
typename T::LeafNode StreamEval<T>::next()
{
    if (use_expansion && rdpf.has_expansion()) {
        // Just use the precomputed values
        typename T::LeafNode leaf =
            rdpf.get_expansion(nextindex ^ counter_xor_offset);
        nextindex = (nextindex + 1) & indexmask;
        return leaf;
    }
    // Invariant: in the first call to next(), nextindex = pathindex.
    // Otherwise, nextindex = pathindex+1.
    // Get the XOR of nextindex and pathindex, and strip the low bit.
    // If nextindex and pathindex are equal, or pathindex is even
    // and nextindex is the consecutive odd number, index_xor will be 0,
    // indicating that we don't have to update the path, but just
    // compute the appropriate leaf given by the low bit of nextindex.
    //
    // Otherwise, say for example pathindex is 010010111 and nextindex
    // is 010011000.  Then their XOR is 000001111, and stripping the low
    // bit yields 000001110, so how_many_1_bits will be 3.
    // That indicates (typically) that path[depth-3] was a left child,
    // and now we need to change it to a right child by descending right
    // from path[depth-4], and then filling the path after that with
    // left children.
    //
    // When we wrap around, however, index_xor will be 111111110 (after
    // we strip the low bit), and how_many_1_bits will be depth-1, but
    // the new top child (of the root seed) we have to compute will be a
    // left, not a right, child.
    uint64_t index_xor = (nextindex ^ pathindex) & ~1;
    nbits_t how_many_1_bits = __builtin_popcount(index_xor);
    if (how_many_1_bits > 0) {
        // This will almost always be 1, unless we've just wrapped
        // around from the right subtree back to the left, in which case
        // it will be 0.
        bool top_changed_bit =
            !!(nextindex & (address_t(1) << how_many_1_bits));
        bool xor_offset_bit =
            !!(counter_xor_offset & (address_t(1) << how_many_1_bits));
        path[depth-how_many_1_bits] =
            rdpf.descend(path[depth-how_many_1_bits-1],
                depth-how_many_1_bits-1,
                top_changed_bit ^ xor_offset_bit, aes_ops);
        for (nbits_t i = depth-how_many_1_bits; i < depth-1; ++i) {
            bool xor_offset_bit =
                !!(counter_xor_offset & (address_t(1) << (depth-i-1)));
            path[i+1] = rdpf.descend(path[i], i, xor_offset_bit, aes_ops);
        }
    }
    bool xor_offset_bit = counter_xor_offset & 1;
    typename T::LeafNode leaf = rdpf.descend_to_leaf(path[depth-1], depth-1,
        (nextindex & 1) ^ xor_offset_bit, aes_ops);
    pathindex = nextindex;
    nextindex = (nextindex + 1) & indexmask;
    return leaf;
}

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
template <typename T> template <typename V, typename W>
inline V ParallelEval<T>::reduce(V init, W process)
{
    size_t thread_aes_ops[num_threads];
    V accums[num_threads];
    boost::asio::thread_pool pool(num_threads);
    address_t threadstart = 0;
    address_t threadchunk = num_evals / num_threads;
    address_t threadextra = num_evals % num_threads;
    nbits_t depth = rdpf.depth();
    address_t indexmask = (depth < ADDRESS_MAX_BITS ?
        ((address_t(1)<<depth)-1) : ~0);
    for (int thread_num = 0; thread_num < num_threads; ++thread_num) {
        address_t threadsize = threadchunk + (address_t(thread_num) < threadextra);
        boost::asio::post(pool,
            [this, &init, &thread_aes_ops, &accums, &process,
                    thread_num, threadstart, threadsize, indexmask] {
                size_t local_aes_ops = 0;
                auto ev = StreamEval(rdpf, (start+threadstart)&indexmask,
                    xor_offset, local_aes_ops);
                V accum = init;
                for (address_t x=0;x<threadsize;++x) {
                    typename T::LeafNode leaf = ev.next();
                    accum += process(thread_num,
                        (threadstart+x)&indexmask, leaf);
                }
                accums[thread_num] = accum;
                thread_aes_ops[thread_num] = local_aes_ops;
            });
        threadstart = (threadstart + threadsize) & indexmask;
    }
    pool.join();
    V total = init;
    for (int thread_num = 0; thread_num < num_threads; ++thread_num) {
        total += accums[thread_num];
        aes_ops += thread_aes_ops[thread_num];
    }
    return total;
}

// Descend from a node at depth parentdepth to one of its leaf children
// whichchild = 0: left child
// whichchild = 1: right child
//
// Cost: 1 AES operation
template <nbits_t WIDTH>
inline typename RDPF<WIDTH>::LeafNode RDPF<WIDTH>::descend_to_leaf(
    const DPFnode &parent, nbits_t parentdepth, bit_t whichchild,
    size_t &aes_ops) const
{
    typename RDPF<WIDTH>::LeafNode prgout;
    bool flag = get_lsb(parent);
    prg(prgout, parent, whichchild, aes_ops);
    if (flag) {
        LeafNode CW = li[maxdepth-parentdepth-1].leaf_cw;
        LeafNode CWR = CW;
        bit_t cfbit = !!value_t(leaf_cfbits & value_t(1 << (maxdepth-parentdepth-1)));
        mpz_class temp_mask = GMPMasks<WIDTH>::lsb_mask[cfbit];
        mpz_xor(CWR[0].get_mpz_t(), temp_mask.get_mpz_t(), CWR[0].get_mpz_t());
        prgout ^= (whichchild ? CWR : CW);
    }
    return prgout;
}

// I/O for RDPFs

template <typename T, nbits_t WIDTH>
T& operator>>(T &is, RDPF<WIDTH> &rdpf)
{
    is.read((char *)&rdpf.seed, sizeof(rdpf.seed));
    rdpf.whichhalf = get_lsb(rdpf.seed);
    uint8_t depth;
    // Add 64 to depth to indicate an expanded RDPF, and add 128 to
    // indicate an incremental RDPF
    is.read((char *)&depth, sizeof(depth));
    bool read_expanded = false;
    bool read_incremental = false;
    if (depth > 128) {
        read_incremental = true;
        depth -= 128;
    }
    if (depth > 64) {
        read_expanded = true;
        depth -= 64;
    }
    rdpf.maxdepth = depth;
    rdpf.curdepth = depth;
    // assert(depth <= ADDRESS_MAX_BITS);
    rdpf.cw.clear();
    for (uint8_t i=0; i<depth-1; ++i) {
        DPFnode cw;
        is.read((char *)&cw, sizeof(cw));
        rdpf.cw.push_back(cw);
    }
    nbits_t num_leaflevels = read_incremental ? depth : 1;
    rdpf.li.resize(num_leaflevels);
    if (read_expanded) {
        for(nbits_t i=0; i<num_leaflevels; ++i) {
            nbits_t level = depth-i;
            rdpf.li[i].expansion.resize(1<<level);
            is.read((char *)rdpf.li[i].expansion.data(),
                sizeof(rdpf.li[i].expansion[0])<<level);
        }
    }
    value_t cfbits = 0;
    is.read((char *)&cfbits, BITBYTES(depth-1));
    rdpf.cfbits = cfbits;
    value_t leaf_cfbits = 0;
    is.read((char *)&leaf_cfbits, BITBYTES(num_leaflevels));
    rdpf.leaf_cfbits = leaf_cfbits;
    for (nbits_t i=0; i<num_leaflevels; ++i) {
        is.read((char *)&rdpf.li[i].leaf_cw,
            sizeof(rdpf.li[i].leaf_cw));
        is.read((char *)&rdpf.li[i].unit_sum_inverse,
            sizeof(rdpf.li[i].unit_sum_inverse));
        is.read((char *)&rdpf.li[i].scaled_sum,
            sizeof(rdpf.li[i].scaled_sum));
        is.read((char *)&rdpf.li[i].scaled_xor,
            sizeof(rdpf.li[i].scaled_xor));
    }

    return is;
}

// Write the DPF to the output stream.  If expanded=true, then include
// the expansion _if_ the DPF is itself already expanded.  You can use
// this to write DPFs to files.
template <typename T, nbits_t WIDTH>
T& write_maybe_expanded(T &os, const RDPF<WIDTH> &rdpf,
    bool expanded = true)
{
    os.write((const char *)&rdpf.seed, sizeof(rdpf.seed));
    uint8_t depth = rdpf.maxdepth;
    // assert(depth <= ADDRESS_MAX_BITS);
    // If we're writing an expansion, add 64 to depth
    uint8_t expanded_depth = depth;
    bool write_expansion = false;
    if (expanded && rdpf.li[0].expansion.size() == (size_t(1)<<depth)) {
        write_expansion = true;
        expanded_depth += 64;
    }
    // If we're writing an incremental RDPF, add 128 to depth
    if (rdpf.li.size() > 1) {
        expanded_depth += 128;
    }
    os.write((const char *)&expanded_depth, sizeof(expanded_depth));
    for (uint8_t i=0; i<depth-1; ++i) {
        os.write((const char *)&rdpf.cw[i], sizeof(rdpf.cw[i]));
    }
    nbits_t num_leaflevels = rdpf.li.size();
    if (write_expansion) {
        for(nbits_t i=0; i<num_leaflevels; ++i) {
            nbits_t level = depth-i;
            os.write((const char *)rdpf.li[i].expansion.data(),
                sizeof(rdpf.li[i].expansion[0])<<level);
        }
    }
    os.write((const char *)&rdpf.cfbits, BITBYTES(depth-1));
    os.write((const char *)&rdpf.leaf_cfbits, BITBYTES(num_leaflevels));
    for (nbits_t i=0; i<num_leaflevels; ++i) {
        os.write((const char *)&rdpf.li[i].leaf_cw,
            sizeof(rdpf.li[i].leaf_cw));
        os.write((const char *)&rdpf.li[i].unit_sum_inverse,
            sizeof(rdpf.li[i].unit_sum_inverse));
        os.write((const char *)&rdpf.li[i].scaled_sum,
            sizeof(rdpf.li[i].scaled_sum));
        os.write((const char *)&rdpf.li[i].scaled_xor,
            sizeof(rdpf.li[i].scaled_xor));
    }

    return os;
}

// The ordinary << version never writes the expansion, since this is
// what we use to send DPFs over the network.
template <typename T, nbits_t WIDTH>
T& operator<<(T &os, const RDPF<WIDTH> &rdpf)
{
    return write_maybe_expanded(os, rdpf, false);
}

// I/O for RDPF Triples

// We never write RDPFTriples over the network, so always write
// the DPF expansions if they're available.
template <typename T, nbits_t WIDTH>
T& operator<<(T &os, const RDPFTriple<WIDTH> &rdpftrip)
{
    write_maybe_expanded(os, rdpftrip.dpf[0], true);
    write_maybe_expanded(os, rdpftrip.dpf[1], true);
    write_maybe_expanded(os, rdpftrip.dpf[2], true);
    nbits_t depth = rdpftrip.dpf[0].depth();
    os.write((const char *)&rdpftrip.as_target.ashare, BITBYTES(depth));
    os.write((const char *)&rdpftrip.xs_target.xshare, BITBYTES(depth));
    return os;
}

template <typename T, nbits_t WIDTH>
T& operator>>(T &is, RDPFTriple<WIDTH> &rdpftrip)
{
    is >> rdpftrip.dpf[0] >> rdpftrip.dpf[1] >> rdpftrip.dpf[2];
    nbits_t depth = rdpftrip.dpf[0].depth();
    rdpftrip.as_target.ashare = 0;
    is.read((char *)&rdpftrip.as_target.ashare, BITBYTES(depth));
    rdpftrip.xs_target.xshare = 0;
    is.read((char *)&rdpftrip.xs_target.xshare, BITBYTES(depth));
    return is;
}

// I/O for RDPF Pairs

// We never write RDPFPairs over the network, so always write
// the DPF expansions if they're available.
template <typename T, nbits_t WIDTH>
T& operator<<(T &os, const RDPFPair<WIDTH> &rdpfpair)
{
    write_maybe_expanded(os, rdpfpair.dpf[0], true);
    write_maybe_expanded(os, rdpfpair.dpf[1], true);
    return os;
}

template <typename T, nbits_t WIDTH>
T& operator>>(T &is, RDPFPair<WIDTH> &rdpfpair)
{
    is >> rdpfpair.dpf[0] >> rdpfpair.dpf[1];
    return is;
}

// Set a DPFnode to zero
static void zero(DPFnode &z)
{
    z = 0;
}

// Set a LeafNode to zero
template <size_t len>
static void zero(std::array<mpz_class,len> &z)
{
    for (auto &m : z) {
        m = 0;
    }
}


// Expand a level of the RDPF into the next level without threads. This
// just computes the PRGs without computing or applying the correction
// words.  L and R will be set to the XORs of the left children and the
// XORs of the right children respectively. NT will be LeafNode if we
// are expanding into a leaf level, DPFnode if not.
// Thus, the sole difference between creating a node level and a leaf level is the type of NT
template <typename NT>
static inline void expand_level_nothreads(size_t start, size_t end,
    const DPFnode *curlevel, NT *nextlevel, NT &L, NT &R,
    size_t &aes_ops)
{
    // Only touch registers in the inner loop if possible
    NT lL, lR;
    zero(lL);
    zero(lR);
    size_t laes_ops = 0;
    for(size_t i=start;i<end;++i) {
        /** Iterates over each element in the given level (*nextLevel is a list)
        * generates for each nod both children with the given node being the seed
        * populates the new list with the nods from left to right
        */
        NT lchild, rchild;
        // will return lchild ^ lchild_ciphertext and rchild ^ rchild_ciphertext
        prgboth(lchild, rchild, curlevel[i], laes_ops);
        lL ^= lchild;  // compound assignemnt operator (bitwise xor) => lL = lL XOR lchild = lchild (lL is zero)
        lR ^= rchild;
        nextlevel[2*i] = lchild;
        nextlevel[2*i+1] = rchild;
    }
    L = lL;
    R = lR;
    aes_ops += laes_ops;
}

// As above, but possibly use threads, based on the RDPF_MTGEN_TIMING_1
// timing benchmarks
template <typename NT>
static inline void expand_level(int max_nthreads, nbits_t level,
    const DPFnode *curlevel, NT *nextlevel, NT &L, NT &R,
    size_t &aes_ops)
{
    size_t curlevel_size = (size_t(1)<<level);
    if (max_nthreads == 1 || level < 19) {
        // No threading
        expand_level_nothreads(0, curlevel_size,
            curlevel, nextlevel, L, R, aes_ops);
    } else {
        int nthreads =
            int(ceil(sqrt(double(curlevel_size/6000))));
        if (nthreads > max_nthreads) {
            nthreads = max_nthreads;
        }
        NT tL[nthreads];
        NT tR[nthreads];
        size_t taes_ops[nthreads];
        size_t threadstart = 0;
        size_t threadchunk = curlevel_size / nthreads;
        size_t threadextra = curlevel_size % nthreads;
        boost::asio::thread_pool pool(nthreads);
        for (int t=0;t<nthreads;++t) {
            size_t threadsize = threadchunk + (size_t(t) < threadextra);
            size_t threadend = threadstart + threadsize;
            taes_ops[t] = 0;
            boost::asio::post(pool,
                [t, &tL, &tR, &taes_ops, threadstart, threadend,
                &curlevel, &nextlevel] {
                    expand_level_nothreads(threadstart, threadend,
                        curlevel, nextlevel, tL[t], tR[t], taes_ops[t]);
                });
            threadstart = threadend;
        }
        pool.join();
        // Again work on registers as much as possible
        NT lL, lR;
        zero(lL);
        zero(lR);
        size_t laes_ops = 0;
        for (int t=0;t<nthreads;++t) {
            lL ^= tL[t];
            lR ^= tR[t];
            laes_ops += taes_ops[t];
        }
        L = lL;
        R = lR;
        aes_ops += laes_ops;
    }
}

// Apply the correction words to an expanded non-leaf level (nextlevel),
// based on the flag bits in curlevel. This version does not use
// threads.
static inline void finalize_nonleaf_layer_nothreads(size_t start,
    size_t end, const DPFnode *curlevel, DPFnode *nextlevel,
    DPFnode CWL, DPFnode CWR)
{
    for(size_t i=start;i<end;++i) {
        bool flag = get_lsb(curlevel[i]);
        nextlevel[2*i] = xor_if(nextlevel[2*i], CWL, flag);
        nextlevel[2*i+1] = xor_if(nextlevel[2*i+1], CWR, flag);
    }
}

// As above, but possibly use threads, based on the RDPF_MTGEN_TIMING_1
// timing benchmarks.  The timing of each iteration of the inner loop is
// comparable to the above, so just use the same computations.  All of
// this could be tuned, of course.
// This function calls the nothreads-equivalent if maxthreads == 1 || level < 19
static inline void finalize_nonleaf_layer(int max_nthreads, nbits_t level,
    const DPFnode *curlevel, DPFnode *nextlevel, DPFnode CWL,
    DPFnode CWR)
{
    size_t curlevel_size = (size_t(1)<<level);
    if (max_nthreads == 1 || level < 19) {
        // No threading
        finalize_nonleaf_layer_nothreads(0, curlevel_size,
            curlevel, nextlevel, CWL, CWR);
    } else {
        int nthreads =
            int(ceil(sqrt(double(curlevel_size/6000))));
        if (nthreads > max_nthreads) {
            nthreads = max_nthreads;
        }
        size_t threadstart = 0;
        size_t threadchunk = curlevel_size / nthreads;
        size_t threadextra = curlevel_size % nthreads;
        boost::asio::thread_pool pool(nthreads);
        for (int t=0;t<nthreads;++t) {
            size_t threadsize = threadchunk + (size_t(t) < threadextra);
            size_t threadend = threadstart + threadsize;
            boost::asio::post(pool,
                [threadstart, threadend, CWL, CWR,
                &curlevel, &nextlevel] {
                    finalize_nonleaf_layer_nothreads(threadstart, threadend,
                        curlevel, nextlevel, CWL, CWR);
                });
            threadstart = threadend;
        }
        pool.join();
    }
}

// Finalize a leaf layer. This applies the correction words, and
// computes the low and high sums and XORs.  This version does not use
// threads.  You can pass save_expansion = false here if you don't need
// to save the expansion.  LN is a LeafNode.
template <size_t WIDTH, typename LN>
static inline void finalize_leaf_layer_nothreads(size_t start,
    size_t end, const DPFnode *curlevel, LN *nextlevel,
    bool save_expansion, LN CWL, LN CWR, value_t &low_sum,
    std::array<value_t,WIDTH> &high_sum,
    std::array<value_t,WIDTH> &high_xor)
{
    value_t llow_sum = 0;  // remember: value_t is a type alias for the 64 bit vector
    std::array<value_t,WIDTH> lhigh_sum;
    std::array<value_t,WIDTH> lhigh_xor;
    lhigh_sum[0] = 0;
    lhigh_xor[0] = 0;
    for(size_t i=start;i<end;++i) {
        bool flag = get_lsb(curlevel[i]);
        LN leftchild = xor_if(nextlevel[2*i], CWL, flag);
        LN rightchild = xor_if(nextlevel[2*i+1], CWR, flag);
        if (save_expansion) {
            nextlevel[2*i] = leftchild;
            nextlevel[2*i+1] = rightchild;
        }
        // Divides the 128bit vectors into two 64-bit vectors leftchild => (lefthigh, leftlow) and right, respectively
        value_t leftlow, rightlow, lefthigh, righthigh;
        mpz_mod_2exp(leftlow.get_mpz_t(), leftchild[0].get_mpz_t(), WIDTH / 2);
        mpz_mod_2exp(rightlow.get_mpz_t(), rightchild[0].get_mpz_t(), WIDTH / 2);
        mpz_fdiv_q_2exp(lefthigh.get_mpz_t(), leftchild[0].get_mpz_t(), WIDTH / 2);
        mpz_fdiv_q_2exp(righthigh.get_mpz_t(), rightchild[0].get_mpz_t(), WIDTH / 2);

        llow_sum += leftlow + rightlow;
        lhigh_sum[0] += lefthigh + righthigh;
        lhigh_xor[0] ^= lefthigh ^ righthigh;
        size_t w = 0;
        for (size_t j=1; j<WIDTH; j+=2) {
            ++w;
            value_t leftlow, lefthigh, rightlow, righthigh;
            mpz_mod_2exp(leftlow.get_mpz_t(), leftchild[w].get_mpz_t(), WIDTH / 2);
            mpz_mod_2exp(rightlow.get_mpz_t(), rightchild[w].get_mpz_t(), WIDTH / 2);
            mpz_fdiv_q_2exp(lefthigh.get_mpz_t(), leftchild[w].get_mpz_t(), WIDTH / 2);
            mpz_fdiv_q_2exp(righthigh.get_mpz_t(), rightchild[w].get_mpz_t(), WIDTH / 2);

            lhigh_sum[j] += (leftlow + rightlow);
            lhigh_xor[j] ^= (leftlow ^ rightlow);
            if (j+1 < WIDTH) {
                lhigh_sum[j+1] += (lefthigh + righthigh);
                lhigh_xor[j+1] ^= (lefthigh ^ righthigh);
            }
        }
    }
    low_sum = llow_sum;
    high_sum = lhigh_sum;
    high_xor = lhigh_xor;
}

// As above, but possibly use threads, based on the RDPF_MTGEN_TIMING_1
// timing benchmarks.  The timing of each iteration of the inner loop is
// comparable to the above, so just use the same computations.  All of
// this could be tuned, of course.
template <size_t WIDTH, typename LN>
static inline void finalize_leaf_layer(int max_nthreads, nbits_t level,
    const DPFnode *curlevel, LN *nextlevel, bool save_expansion,
    LN CWL, LN CWR, value_t &low_sum,
    std::array<value_t,WIDTH> &high_sum,
    std::array<value_t,WIDTH> &high_xor)
{
    size_t curlevel_size = (size_t(1)<<level);
    if (max_nthreads == 1 || level < 19) {
        // No threading
        finalize_leaf_layer_nothreads(0, curlevel_size,
            curlevel, nextlevel, save_expansion, CWL, CWR,
            low_sum, high_sum, high_xor);
    } else {
        int nthreads =
            int(ceil(sqrt(double(curlevel_size/6000))));
        if (nthreads > max_nthreads) {
            nthreads = max_nthreads;
        }
        value_t tlow_sum[nthreads];
        std::array<value_t,WIDTH> thigh_sum[nthreads];
        std::array<value_t,WIDTH> thigh_xor[nthreads];
        size_t threadstart = 0;
        size_t threadchunk = curlevel_size / nthreads;
        size_t threadextra = curlevel_size % nthreads;
        boost::asio::thread_pool pool(nthreads);
        for (int t=0;t<nthreads;++t) {
            size_t threadsize = threadchunk + (size_t(t) < threadextra);
            size_t threadend = threadstart + threadsize;
            boost::asio::post(pool,
                [t, &tlow_sum, &thigh_sum, &thigh_xor, threadstart, threadend,
                &curlevel, &nextlevel, CWL, CWR, save_expansion] {
                    finalize_leaf_layer_nothreads(threadstart, threadend,
                        curlevel, nextlevel, save_expansion, CWL, CWR,
                        tlow_sum[t], thigh_sum[t], thigh_xor[t]);
                });
            threadstart = threadend;
        }
        pool.join();
        low_sum = 0;
        zero(high_sum);
        zero(high_xor);
        for (int t=0;t<nthreads;++t) {
            low_sum += tlow_sum[t];
            high_sum += thigh_sum[t];
            high_xor ^= thigh_xor[t];
        }
    }
}



// Create one level of the RDPF.  NT will be as above: LeafNode if we
// are expanding into a leaf level, DPFnode if not.  LI will be LeafInfo
// if we are expanding into a leaf level, and it is unused otherwise.
template<typename NT, typename LI>
static inline void create_level(MPCTIO &tio, yield_t &yield,
    const DPFnode *curlevel, NT *nextlevel,
    int player, nbits_t level, nbits_t depth, RegBS bs_choice, NT &CW,
    bool &cfbit, bool save_expansion, LI &li, size_t &aes_ops)
{
    // tio.cpu_nthreads() is the maximum number of threads we
    // have available.
    int max_nthreads = tio.cpu_nthreads();

    NT L, R;
    zero(L);
    zero(R);
    // The server doesn't need to do this computation, but it does
    // need to execute mpc_reconstruct_choice so that it sends
    // the AndTriples at the appropriate time.
    if (player < 2) {
#ifdef RDPF_MTGEN_TIMING_1
        if (player == 0) {
            mtgen_timetest_1(level, 0, (1<<23)>>level, curlevel,
                nextlevel, aes_ops);
            size_t niters = 2048;
            if (level > 8) niters = (1<<20)>>level;
            for(int t=1;t<=8;++t) {
                mtgen_timetest_1(level, t, niters, curlevel,
                    nextlevel, aes_ops);
            }
            mtgen_timetest_1(level, 0, (1<<23)>>level, curlevel,
                nextlevel, aes_ops);
        }
#endif
        // Using the timing results gathered above, decide whether
        // to multithread, and if so, how many threads to use.
        expand_level(max_nthreads, level, curlevel, nextlevel,
            L, R, aes_ops);
    }

    // If we're going left (bs_choice = 0), we want the correction
    // word to be the XOR of our right side and our peer's right
    // side; if bs_choice = 1, it should be the XOR or our left side
    // and our peer's left side.

    // We also have to ensure that the flag bits (the lsb) of the
    // side that will end up the same be of course the same, but
    // also that the flag bits (the lsb) of the side that will end
    // up different _must_ be different.  That is, it's not enough
    // for the nodes of the child selected by choice to be different
    // as 128-bit values; they also have to be different in their
    // lsb.

    // This is where we make a small optimization over Appendix C of
    // the Duoram paper: instead of keeping separate correction flag
    // bits for the left and right children, we observe that the low
    // bit of the overall correction word effectively serves as one
    // of those bits, so we just need to store one extra bit per
    // level, not two.  (We arbitrarily choose the one for the right
    // child.)

    // Note that the XOR of our left and right child before and
    // after applying the correction word won't change, since the
    // correction word is applied to either both children or
    // neither, depending on the value of the parent's flag. So in
    // particular, the XOR of the flag bits won't change, and if our
    // children's flag's XOR equals our peer's children's flag's
    // XOR, then we won't have different flag bits even for the
    // children that have different 128-bit values.

    // So we compute our_parity = lsb(L^R)^player, and we XOR that
    // into the R value in the correction word computation.  At the
    // same time, we exchange these parity values to compute the
    // combined parity, which we store in the DPF.  Then when the
    // DPF is evaluated, if the parent's flag is set, not only apply
    // the correction work to both children, but also apply the
    // (combined) parity bit to just the right child.  Then for
    // unequal nodes (where the flag bit is different), exactly one
    // of the four children (two for P0 and two for P1) will have
    // the parity bit applied, which will set the XOR of the lsb of
    // those four nodes to just L0^R0^L1^R1^our_parity^peer_parity
    // = 1 because everything cancels out except player (for which
    // one player is 0 and the other is 1).

    bool our_parity_bit = get_lsb(L) ^ get_lsb(R) ^ !!player;
    xor_lsb(R, our_parity_bit);

    NT CWL;
    bool peer_parity_bit;
    // Exchange the parities and do mpc_reconstruct_choice at the
    // same time (bundled into the same rounds)
    tio.queue_peer(&our_parity_bit, 1);
    yield();
    uint8_t peer_parity_byte;
    tio.recv_peer(&peer_parity_byte, 1);
    peer_parity_bit = peer_parity_byte & 1;
    mpc_reconstruct_choice(tio, yield, CWL, bs_choice, R, L);
    // run_coroutines(yield,
    //     [&tio, &our_parity_bit, &peer_parity_bit](yield_t &yield) {
    //         tio.queue_peer(&our_parity_bit, 1);
    //         yield();
    //         uint8_t peer_parity_byte;
    //         tio.recv_peer(&peer_parity_byte, 1);
    //         peer_parity_bit = peer_parity_byte & 1;
    //     },
    //     [&tio, &CWL, &L, &R, bs_choice](yield_t &yield) {
    //         mpc_reconstruct_choice(tio, yield, CWL, bs_choice, R, L);
    //     });
    cfbit = our_parity_bit ^ peer_parity_bit;
    CW = CWL;
    NT CWR = CWL;
    xor_lsb(CWR, cfbit);
    if (player < 2) {
        // The timing of each iteration of the inner loop is
        // comparable to the above, so just use the same
        // computations.  All of this could be tuned, of course.

        if constexpr (std::is_same_v<NT, DPFnode>) {
            finalize_nonleaf_layer(max_nthreads, level, curlevel,
                nextlevel, CWL, CWR);
        } else {
            // Recall there are four potentially useful vectors that
            // can come out of a DPF:
            // - (single-bit) bitwise unit vector
            // - additive-shared unit vector
            // - XOR-shared scaled unit vector
            // - additive-shared scaled unit vector
            //
            // (No single DPF should be used for both of the first
            // two or both of the last two, though, since they're
            // correlated; you _can_ use one of the first two and
            // one of the last two.)
            //
            // For each 128-bit leaf, the low bit is the flag bit,
            // and we're guaranteed that the flag bits (and indeed
            // the whole 128-bit value) for P0 and P1 are the same
            // for every leaf except the target, and that the flag
            // bits definitely differ for the target (and the other
            // 127 bits are independently random on each side).
            //
            // We divide the 128-bit leaf into a low 64-bit word and
            // a high 64-bit word.  We use the low word for the unit
            // vector and the high word for the scaled vector; this
            // choice is not arbitrary: the flag bit in the low word
            // means that the sum of all the low words (with P1's
            // low words negated) across both P0 and P1 is
            // definitely odd, so we can compute that sum's inverse
            // mod 2^64, and store it now during precomputation.  At
            // evaluation time for the additive-shared unit vector,
            // we will output this global inverse times the low word
            // of each leaf, which will make the sum of all of those
            // values 1.  (This technique replaces the protocol in
            // Appendix D of the Duoram paper.)
            //
            // For the scaled vector, we just have to compute shares
            // of what the scaled vector is a sharing _of_, but
            // that's just XORing or adding all of each party's
            // local high words; no communication needed.

            value_t low_sum;
            const size_t WIDTH = LI::W;
            std::array<value_t,WIDTH> high_sum;
            std::array<value_t,WIDTH> high_xor;
            finalize_leaf_layer(max_nthreads, level, curlevel,
                nextlevel, save_expansion, CWL, CWR, low_sum, high_sum,
                high_xor);

            if (player == 1) {
                low_sum = -low_sum;
                for(size_t j=0; j<WIDTH; ++j) {
                    high_sum[j] = -high_sum[j];
                }
            }
            for(size_t j=0; j<WIDTH; ++j) {
                li.scaled_sum[j].ashare = high_sum[j];
                li.scaled_xor[j].xshare = high_xor[j];
            }
            // Exchange low_sum and add them up
            tio.queue_peer(low_sum);
            yield();
            value_t peer_low_sum;
            tio.recv_peer(peer_low_sum);
            low_sum += peer_low_sum;
            // The low_sum had better be odd
            // assert(parity(low_sum) == 1);
            li.unit_sum_inverse = inverse_value_t(low_sum);
        }
    } else if constexpr (!std::is_same_v<NT, DPFnode>) {
        yield();
    }
}


// Construct a DPF with the given (XOR-shared) target location, and
// of the given depth, to be used for random-access memory reads and
// writes.  The DPF is construction collaboratively by P0 and P1,
// with the server P2 helping by providing various kinds of
// correlated randomness, such as MultTriples and AndTriples.
//
// This algorithm is based on Appendix C from the Duoram paper, with a
// small optimization noted below.
template <nbits_t WIDTH>
RDPF<WIDTH>::RDPF(MPCTIO &tio, yield_t &yield,
    const RegXS& target, nbits_t depth, bool incremental, bool save_expansion)
{
    int player = tio.player();
    size_t &aes_ops = tio.aes_ops();

    // Choose a random seed
    // arc4random_buf(&seed, sizeof(seed));
    DPFnode tmp;
    tmp = random_mpz(2*VALUE_BITS);

    // Ensure the flag bits (the lsb of each node) are different => p0 has lsb 0, p1 lsb 1 by default
    // Note the invariant of dpf construction, only on the path to i* the flag bits must be different (root always on path)
    seed = set_lsb(tmp, !!player);
    cfbits = 0;
    leaf_cfbits = 0;
    whichhalf = (player == 1);
    maxdepth = depth;
    curdepth = depth;

    // The root level is just the seed => following lines define the root
    nbits_t level = 0;
    DPFnode *curlevel = nullptr;
    auto *nextlevel = new DPFnode[1];
    nextlevel[0] = seed;

    li.resize(incremental ? depth : 1);

    // Prefetch the right number of nodeselecttriples
    tio.request_nodeselecttriples(yield, incremental ? 2*depth-1 : depth);

    // Construct each intermediate level
    while(level < depth) {
        LeafNode *leaflevel = NULL;
        if (player < 2) {
            delete[] curlevel;
            curlevel = nextlevel;
            nextlevel = nullptr;
            if (save_expansion && (incremental || level == depth-1)) {
                li[depth-1-level].expansion.resize(1<<(level+1));
                leaflevel = li[depth-1-level].expansion.data();
            }
            // This will build a leafNode or DPF node according to the level (also in non-incremental dpfs)
            else if (incremental || level == depth-1) {
                leaflevel = new LeafNode[1<<(level+1)];
            }
            if (level < depth-1) {
                nextlevel = new DPFnode[1<<(level+1)];  // sets the vector to 2^(level+1)
            }
        }
        // Invariant: curlevel has 2^level DPFnode elements; nextlevel
        // has 2^{level+1} DPFnode elements if we're not at the last
        // level, and leaflevel has 2^{level+1} LeafNode elements if we
        // are at a leaf level (the last level always, and all levels if
        // we are making an incremental RDPF).

        // The bit-shared choice bit is bit (depth-level-1) of the
        // XOR-shared target index
        RegBS bs_choice = target.bit(depth-level-1);

        // At each layer, we can create the next internal layer and the
        // leaf layer in parallel coroutines if we're making an
        // incremental RDPF.  If not, exactly one of these coroutines
        // will be created, and we just run that one.
        // std::vector<coro_t> coroutines;
        if (level < depth-1) {
            DPFnode CW;
            bool cfbit;
            // This field is ignored when we're not expanding to a leaf
            // level, but it needs to be an lvalue reference.
            int noleafinfo = 0;
            create_level(tio, yield, curlevel, nextlevel, player, level,
                depth, bs_choice, CW, cfbit, save_expansion, noleafinfo,
                aes_ops);
            cfbits |= (value_t(cfbit)<<level);
            if (player < 2) {
                cw.push_back(CW);
            }
        }
        if (incremental || level == depth-1) {
            LeafNode CW;
            bool cfbit;
            create_level(tio, yield, curlevel, leaflevel, player,
                level, depth, bs_choice, CW, cfbit, save_expansion,
                li[depth-level-1], aes_ops);
            leaf_cfbits |= (value_t(cfbit)<<(depth-level-1));
            li[depth-level-1].leaf_cw = CW;
        }
        // run_coroutines(yield, coroutines);

        if (!save_expansion) {
            delete[] leaflevel;
        }
        ++level;
    }

    delete[] curlevel;
    delete[] nextlevel;
}

// Get the leaf node for the given input
template <nbits_t WIDTH>
typename RDPF<WIDTH>::LeafNode
    RDPF<WIDTH>::leaf(address_t input, size_t &aes_ops) const
{
    // If we have a precomputed expansion, just use it
    if (li[maxdepth-curdepth].expansion.size()) {
        return li[maxdepth-curdepth].expansion[input];
    }

    DPFnode node = seed;
    for (nbits_t d=0;d<curdepth-1;++d) {
        bit_t dir = !!(input & (address_t(1)<<(curdepth-d-1)));
        node = descend(node, d, dir, aes_ops);
    }
    bit_t dir = (input & 1);
    return descend_to_leaf(node, curdepth-1, dir, aes_ops);
}

// Expand one leaf layer of the DPF if it's not already expanded
//
// This routine is slightly more efficient (except for incremental
// RDPFs) than repeatedly calling StreamEval::next(), but it uses a lot
// more memory.
template <nbits_t WIDTH>
void RDPF<WIDTH>::expand_leaf_layer(nbits_t li_index, size_t &aes_ops)
{
    nbits_t depth = maxdepth - li_index;
    size_t num_leaves = size_t(1)<<depth;  // = 2**depth
    if (li[li_index].expansion.size() == num_leaves) return;
    li[li_index].expansion.resize(num_leaves);
    address_t index = 0;
    address_t lastindex = 0;
    auto *path = new DPFnode[depth];
    path[0] = seed;
    for (nbits_t i=1;i<depth;++i) {
        path[i] = descend(path[i-1], i-1, 0, aes_ops);
    }
    li[maxdepth-depth].expansion[index++] =
        descend_to_leaf(path[depth-1], depth-1, 0, aes_ops);
    li[maxdepth-depth].expansion[index++] =
        descend_to_leaf(path[depth-1], depth-1, 1, aes_ops);
    while(index < num_leaves) {
        // Invariant: lastindex and index will both be even, and
        // index=lastindex+2
        uint64_t index_xor = index ^ lastindex;
        nbits_t how_many_1_bits = __builtin_popcount(index_xor);
        // If lastindex -> index goes for example from (in binary)
        // 010010110 -> 010011000, then index_xor will be
        // 000001110 and how_many_1_bits will be 3.
        // That indicates that path[depth-3] was a left child, and now
        // we need to change it to a right child by descending right
        // from path[depth-4], and then filling the path after that with
        // left children.
        path[depth-how_many_1_bits] =
            descend(path[depth-how_many_1_bits-1],
                depth-how_many_1_bits-1, 1, aes_ops);
        for (nbits_t i = depth-how_many_1_bits; i < depth-1; ++i) {
            path[i+1] = descend(path[i], i, 0, aes_ops);
        }
        lastindex = index;
        li[li_index].expansion[index++] =
            descend_to_leaf(path[depth-1], depth-1, 0, aes_ops);
        li[li_index].expansion[index++] =
            descend_to_leaf(path[depth-1], depth-1, 1, aes_ops);
    }

    delete[] path;
}

// Expand the DPF if it's not already expanded
//
// This routine is slightly more efficient (except for incremental
// RDPFs) than repeatedly calling StreamEval::next(), but it uses a lot
// more memory.
template <nbits_t WIDTH>
void RDPF<WIDTH>::expand(size_t &aes_ops)
{
    nbits_t num_leaf_layers = li.size();
    for (nbits_t li_index=0; li_index < num_leaf_layers; ++li_index) {
        expand_leaf_layer(li_index, aes_ops);
    }
}

// Construct three RDPFs of the given depth all with the same randomly
// generated target index.
template <nbits_t WIDTH>
RDPFTriple<WIDTH>::RDPFTriple(MPCTIO &tio, yield_t &yield,
    nbits_t depth, bool incremental, bool save_expansion)
{
    // Pick a random XOR share of the target
    xs_target.randomize(depth);
    for (int i=0;i<3;++i) {
        dpf[i] = RDPF<WIDTH>(tio, yield, xs_target, depth,
                    incremental, save_expansion);
        std::cout << "Finished dpf " << i << std::endl;
    }
    std::cout << "Before mpc_xs_to_as" << std::endl;
    mpc_xs_to_as(tio, yield, as_target, xs_target, depth, false);
    std::cout << "After mpc_xs_to_as" << std::endl;
}

template <nbits_t WIDTH>
typename RDPFTriple<WIDTH>::node RDPFTriple<WIDTH>::descend(
    const RDPFTriple<WIDTH>::node &parent,
    nbits_t parentdepth, bit_t whichchild,
    size_t &aes_ops) const
{
    // auto [P0, P1, P2] = parent;
    DPFnode P0 = std::get<0>(parent);
    DPFnode P1 = std::get<1>(parent);
    DPFnode P2 = std::get<2>(parent);

    DPFnode C0, C1, C2;
    C0 = dpf[0].descend(P0, parentdepth, whichchild, aes_ops);
    C1 = dpf[1].descend(P1, parentdepth, whichchild, aes_ops);
    C2 = dpf[2].descend(P2, parentdepth, whichchild, aes_ops);
    return std::make_tuple(C0,C1,C2);
}

template <nbits_t WIDTH>
typename RDPFTriple<WIDTH>::LeafNode RDPFTriple<WIDTH>::descend_to_leaf(
    const RDPFTriple<WIDTH>::node &parent,
    nbits_t parentdepth, bit_t whichchild,
    size_t &aes_ops) const
{
    auto [P0, P1, P2] = parent;
    typename RDPF<WIDTH>::LeafNode C0, C1, C2;
    C0 = dpf[0].descend_to_leaf(P0, parentdepth, whichchild, aes_ops);
    C1 = dpf[1].descend_to_leaf(P1, parentdepth, whichchild, aes_ops);
    C2 = dpf[2].descend_to_leaf(P2, parentdepth, whichchild, aes_ops);
    return std::make_tuple(C0,C1,C2);
}

template <nbits_t WIDTH>
typename RDPFPair<WIDTH>::node RDPFPair<WIDTH>::descend(
    const RDPFPair<WIDTH>::node &parent,
    nbits_t parentdepth, bit_t whichchild,
    size_t &aes_ops) const
{
    auto [P0, P1] = parent;
    DPFnode C0, C1;
    C0 = dpf[0].descend(P0, parentdepth, whichchild, aes_ops);
    C1 = dpf[1].descend(P1, parentdepth, whichchild, aes_ops);
    return std::make_tuple(C0,C1);
}

template <nbits_t WIDTH>
typename RDPFPair<WIDTH>::LeafNode RDPFPair<WIDTH>::descend_to_leaf(
    const RDPFPair<WIDTH>::node &parent,
    nbits_t parentdepth, bit_t whichchild,
    size_t &aes_ops) const
{
    auto [P0, P1] = parent;
    typename RDPF<WIDTH>::LeafNode C0, C1;
    C0 = dpf[0].descend_to_leaf(P0, parentdepth, whichchild, aes_ops);
    C1 = dpf[1].descend_to_leaf(P1, parentdepth, whichchild, aes_ops);
    return std::make_tuple(C0,C1);
}

template <nbits_t WIDTH>
typename RDPF2of3<WIDTH>::node RDPF2of3<WIDTH>::descend(
    const RDPF2of3<WIDTH>::node &parent,
    nbits_t parentdepth, bit_t whichchild,
    size_t &aes_ops) const
{
    auto [P0, P1] = parent;
    DPFnode C0, C1;
    C0 = dpf0.descend(P0, parentdepth, whichchild, aes_ops);
    C1 = dpf1.descend(P1, parentdepth, whichchild, aes_ops);
    return std::make_tuple(C0,C1);
}

template <nbits_t WIDTH>
typename RDPF2of3<WIDTH>::LeafNode RDPF2of3<WIDTH>::descend_to_leaf(
    const RDPF2of3<WIDTH>::node &parent,
    nbits_t parentdepth, bit_t whichchild,
    size_t &aes_ops) const
{
    auto [P0, P1] = parent;
    typename RDPF<WIDTH>::LeafNode C0, C1;
    C0 = dpf0.descend_to_leaf(P0, parentdepth, whichchild, aes_ops);
    C1 = dpf1.descend_to_leaf(P1, parentdepth, whichchild, aes_ops);
    return std::make_tuple(C0,C1);
}

#endif