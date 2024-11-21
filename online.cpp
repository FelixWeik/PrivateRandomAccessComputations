#include <bsd/stdlib.h> // arc4random_buf

#include "online.hpp"
#include "mpcops.hpp"
#include "rdpf.hpp"
#include "duoram.hpp"
#include "cdpf.hpp"
#include "cell.hpp"
#include "heap.hpp"
#include "shapes.hpp"
#include "bst.hpp"
#include "avl.hpp"
#include "heapsampler.hpp"

static void online_test(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    nbits_t nbits = VALUE_BITS;

    if (*args) {
        nbits = atoi(*args);
    }

    size_t as_memsize = 9;
    size_t xs_memsize = 3;

    MPCTIO tio(mpcio, 0);
    bool is_server = (mpcio.player == 2);

    RegAS *A = new RegAS[as_memsize];
    RegXS *AX = new RegXS[xs_memsize];
    value_t V;
    RegBS F0, F1, F2;
    RegBS FA, FO, FS;
    RegXS X;

    if (!is_server) {
        A[0].randomize();
        A[1].randomize();
        F0.randomize();
        A[4].randomize();
        F1.randomize();
        F2.randomize();
        A[6].randomize();
        A[7].randomize();
        X.randomize();
        AX[0].randomize();
        AX[1].randomize();
        arc4random_buf(&V, sizeof(V));
        printf("A:\n"); for (size_t i=0; i<as_memsize; ++i) printf("%3lu: %016lX\n", i, A[i].ashare);
        printf("AX:\n"); for (size_t i=0; i<xs_memsize; ++i) printf("%3lu: %016lX\n", i, AX[i].xshare);
        printf("V  : %016lX\n", V);
        printf("F0 : %01X\n", F0.bshare);
        printf("F1 : %01X\n", F1.bshare);
        printf("F2 : %01X\n", F2.bshare);
        printf("X  : %016lX\n", X.xshare);
    }
    std::vector<coro_t> coroutines;
    coroutines.emplace_back(
        [&tio, &A, nbits](yield_t &yield) {
            mpc_mul(tio, yield, A[2], A[0], A[1], nbits);
        });
    coroutines.emplace_back(
        [&tio, &A, V, nbits](yield_t &yield) {
            mpc_valuemul(tio, yield, A[3], V, nbits);
        });
    coroutines.emplace_back(
        [&tio, &A, &F0, nbits](yield_t &yield) {
            mpc_flagmult(tio, yield, A[5], F0, A[4], nbits);
        });
    coroutines.emplace_back(
        [&tio, &A, &F1, nbits](yield_t &yield) {
            mpc_oswap(tio, yield, A[6], A[7], F1, nbits);
        });
    coroutines.emplace_back(
        [&tio, &A, &X, nbits](yield_t &yield) {
            mpc_xs_to_as(tio, yield, A[8], X, nbits);
        });
    coroutines.emplace_back(
        [&tio, &AX, &F0, nbits](yield_t &yield) {
            mpc_select(tio, yield, AX[2], F0, AX[0], AX[1], nbits);
        });
    coroutines.emplace_back(
        [&tio, &FA, &F0, &F1](yield_t &yield) {
            mpc_and(tio, yield, FA, F0, F1);
        });
    coroutines.emplace_back(
        [&tio, &FO, &F0, &F1](yield_t &yield) {
            mpc_or(tio, yield, FO, F0, F1);
        });
    coroutines.emplace_back(
        [&tio, &FS, &F0, &F1, &F2](yield_t &yield) {
            mpc_select(tio, yield, FS, F2, F0, F1);
        });
    run_coroutines(tio, coroutines);
    if (!is_server) {
        printf("\n");
        printf("A:\n"); for (size_t i=0; i<as_memsize; ++i) printf("%3lu: %016lX\n", i, A[i].ashare);
        printf("AX:\n"); for (size_t i=0; i<xs_memsize; ++i) printf("%3lu: %016lX\n", i, AX[i].xshare);
    }

    // Check the answers
    if (mpcio.player == 1) {
        tio.queue_peer(A, as_memsize*sizeof(RegAS));
        tio.queue_peer(AX, xs_memsize*sizeof(RegXS));
        tio.queue_peer(&V, sizeof(V));
        tio.queue_peer(&F0, sizeof(RegBS));
        tio.queue_peer(&F1, sizeof(RegBS));
        tio.queue_peer(&F2, sizeof(RegBS));
        tio.queue_peer(&FA, sizeof(RegBS));
        tio.queue_peer(&FO, sizeof(RegBS));
        tio.queue_peer(&FS, sizeof(RegBS));
        tio.queue_peer(&X, sizeof(RegXS));
        tio.send();
    } else if (mpcio.player == 0) {
        RegAS *B = new RegAS[as_memsize];
        RegXS *BAX = new RegXS[xs_memsize];
        RegBS BF0, BF1, BF2;
        RegBS BFA, BFO, BFS;
        RegXS BX;
        value_t BV;
        value_t *S = new value_t[as_memsize];
        value_t *Y = new value_t[xs_memsize];
        bit_t SF0, SF1, SF2;
        bit_t SFA, SFO, SFS;
        value_t SX;
        tio.recv_peer(B, as_memsize*sizeof(RegAS));
        tio.recv_peer(BAX, xs_memsize*sizeof(RegXS));
        tio.recv_peer(&BV, sizeof(BV));
        tio.recv_peer(&BF0, sizeof(RegBS));
        tio.recv_peer(&BF1, sizeof(RegBS));
        tio.recv_peer(&BF2, sizeof(RegBS));
        tio.recv_peer(&BFA, sizeof(RegBS));
        tio.recv_peer(&BFO, sizeof(RegBS));
        tio.recv_peer(&BFS, sizeof(RegBS));
        tio.recv_peer(&BX, sizeof(RegXS));
        for(size_t i=0; i<as_memsize; ++i) S[i] = A[i].ashare+B[i].ashare;
        for(size_t i=0; i<xs_memsize; ++i) Y[i] = AX[i].xshare^BAX[i].xshare;
        SF0 = F0.bshare ^ BF0.bshare;
        SF1 = F1.bshare ^ BF1.bshare;
        SF2 = F2.bshare ^ BF2.bshare;
        SFA = FA.bshare ^ BFA.bshare;
        SFO = FO.bshare ^ BFO.bshare;
        SFS = FS.bshare ^ BFS.bshare;
        SX = X.xshare ^ BX.xshare;
        printf("S:\n"); for (size_t i=0; i<as_memsize; ++i) printf("%3lu: %016lX\n", i, S[i]);
        printf("Y:\n"); for (size_t i=0; i<xs_memsize; ++i) printf("%3lu: %016lX\n", i, Y[i]);
        printf("SF0: %01X\n", SF0);
        printf("SF1: %01X\n", SF1);
        printf("SF2: %01X\n", SF2);
        printf("SFA: %01X\n", SFA);
        printf("SFO: %01X\n", SFO);
        printf("SFS: %01X\n", SFS);
        printf("SX : %016lX\n", SX);
        printf("\n%016lx\n", S[0]*S[1]-S[2]);
        printf("%016lx\n", (V*BV)-S[3]);
        printf("%016lx\n", (SF0*S[4])-S[5]);
        printf("%016lx\n", S[8]-SX);
        delete[] B;
        delete[] S;
    }

    delete[] A;
    delete[] AX;
}

static void lamport_test(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    // Create a bunch of threads and send a bunch of data to the other
    // peer, and receive their data.  If an arg is specified, repeat
    // that many times.  The Lamport clock at the end should be just the
    // number of repetitions.  Subsequent args are the chunk size and
    // the number of chunks per message
    size_t niters = 1;
    size_t chunksize = 1<<20;
    size_t numchunks = 1;
    if (*args) {
        niters = atoi(*args);
        ++args;
    }
    if (*args) {
        chunksize = atoi(*args);
        ++args;
    }
    if (*args) {
        numchunks = atoi(*args);
        ++args;
    }

    int num_threads = opts.num_comm_threads;
    boost::asio::thread_pool pool(num_threads);
    for (int thread_num = 0; thread_num < num_threads; ++thread_num) {
        boost::asio::post(pool, [&mpcio, thread_num, niters, chunksize, numchunks] {
            MPCTIO tio(mpcio, thread_num);
            char *sendbuf = new char[chunksize];
            char *recvbuf = new char[chunksize*numchunks];
            for (size_t i=0; i<niters; ++i) {
                for (size_t chunk=0; chunk<numchunks; ++chunk) {
                    arc4random_buf(sendbuf, chunksize);
                    tio.queue_peer(sendbuf, chunksize);
                }
                tio.send();
                tio.recv_peer(recvbuf, chunksize*numchunks);
            }
            delete[] recvbuf;
            delete[] sendbuf;
        });
    }
    pool.join();
}

template <nbits_t WIDTH>
static void rdpf_test(MPCIO &mpcio,
    const PRACOptions &opts, char **args, bool incremental)
{
    nbits_t depth=6;
    size_t num_iters = 1;

    if (*args) {
        depth = atoi(*args);
        ++args;
    }
    if (*args) {
        num_iters = atoi(*args);
        ++args;
    }

    MPCTIO tio(mpcio, 0, opts.num_cpu_threads);
    run_coroutines(tio, [&tio, depth, num_iters, incremental] (yield_t &yield) {
        size_t &aes_ops = tio.aes_ops();
        nbits_t min_level = incremental ? 1 : depth;
        for (size_t iter=0; iter < num_iters; ++iter) {
            if (tio.player() == 2) {
                RDPFPair<WIDTH> dp = tio.rdpfpair<WIDTH>(yield, depth,
                    incremental);
                for (int i=0;i<2;++i) {
                    RDPF<WIDTH> &dpf = dp.dpf[i];
                    for (nbits_t level=min_level; level<=depth; ++level) {
                        if (incremental) {
                            printf("Level = %u\n\n", level);
                            dpf.depth(level);
                        }
                        for (address_t x=0;x<(address_t(1)<<level);++x) {
                            typename RDPF<WIDTH>::LeafNode leaf = dpf.leaf(x, aes_ops);
                            RegBS ub = dpf.unit_bs(leaf);
                            RegAS ua = dpf.unit_as(leaf);
                            typename RDPF<WIDTH>::RegXSW sx = dpf.scaled_xs(leaf);
                            typename RDPF<WIDTH>::RegASW sa = dpf.scaled_as(leaf);
                            printf("%04x %x %016lx", x, ub.bshare, ua.ashare);
                            for (nbits_t j=0;j<WIDTH;++j) {
                                printf(" %016lx %016lx", sx[j].xshare, sa[j].ashare);
                            }
                            printf("\n");
                        }
                        printf("\n");
                    }
                }
            } else {
                RDPFTriple<WIDTH> dt = tio.rdpftriple<WIDTH>(yield,
                    depth, incremental);
                for (int i=0;i<3;++i) {
                    RDPF<WIDTH> &dpf = dt.dpf[i];
                    for (nbits_t level=min_level; level<=depth; ++level) {
                        if (incremental) {
                            printf("Level = %u\n", level);
                            dt.depth(level);
                            RegXS tshare;
                            dt.get_target(tshare);
                            printf("Target share = %lx\n\n", tshare.share());
                        }
                        typename RDPF<WIDTH>::RegXSW peer_scaled_xor;
                        typename RDPF<WIDTH>::RegASW peer_scaled_sum;
                        if (tio.player() == 1) {
                            tio.iostream_peer() <<
                                dpf.li[depth-level].scaled_xor <<
                                dpf.li[depth-level].scaled_sum;
                        } else {
                            tio.iostream_peer() >> peer_scaled_xor >> peer_scaled_sum;
                            peer_scaled_sum += dpf.li[depth-level].scaled_sum;
                            peer_scaled_xor ^= dpf.li[depth-level].scaled_xor;
                        }
                        for (address_t x=0;x<(address_t(1)<<level);++x) {
                            typename RDPF<WIDTH>::LeafNode leaf = dpf.leaf(x, aes_ops);
                            RegBS ub = dpf.unit_bs(leaf);
                            RegAS ua = dpf.unit_as(leaf);
                            typename RDPF<WIDTH>::RegXSW sx = dpf.scaled_xs(leaf);
                            typename RDPF<WIDTH>::RegASW sa = dpf.scaled_as(leaf);
                            printf("%04x %x %016lx", x, ub.bshare, ua.ashare);
                            for (nbits_t j=0;j<WIDTH;++j) {
                                printf(" %016lx %016lx", sx[j].xshare, sa[j].ashare);
                            }
                            printf("\n");
                            if (tio.player() == 1) {
                                tio.iostream_peer() << ub << ua << sx << sa;
                            } else {
                                RegBS peer_ub;
                                RegAS peer_ua;
                                typename RDPF<WIDTH>::RegXSW peer_sx;
                                typename RDPF<WIDTH>::RegASW peer_sa;
                                tio.iostream_peer() >> peer_ub >> peer_ua >>
                                    peer_sx >> peer_sa;
                                ub ^= peer_ub;
                                ua += peer_ua;
                                sx ^= peer_sx;
                                sa += peer_sa;
                                bool is_nonzero = ub.bshare || ua.ashare;
                                for (nbits_t j=0;j<WIDTH;++j) {
                                    is_nonzero |= (sx[j].xshare || sa[j].ashare);
                                }
                                if (is_nonzero) {
                                    printf("**** %x %016lx", ub.bshare, ua.ashare);
                                    for (nbits_t j=0;j<WIDTH;++j) {
                                        printf(" %016lx %016lx", sx[j].xshare, sa[j].ashare);
                                    }
                                    printf("\nSCALE                  ");
                                    for (nbits_t j=0;j<WIDTH;++j) {
                                        printf(" %016lx %016lx",
                                            peer_scaled_xor[j].xshare,
                                            peer_scaled_sum[j].ashare);
                                    }
                                    printf("\n");
                                }
                            }
                        }
                        printf("\n");
                    }
                }
            }
        }
    });
}

static void rdpf_timing(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    nbits_t depth=6;

    if (*args) {
        depth = atoi(*args);
        ++args;
    }

    int num_threads = opts.num_comm_threads;
    boost::asio::thread_pool pool(num_threads);
    for (int thread_num = 0; thread_num < num_threads; ++thread_num) {
        boost::asio::post(pool, [&mpcio, thread_num, depth] {
            MPCTIO tio(mpcio, thread_num);
            run_coroutines(tio, [&tio, depth] (yield_t &yield) {
                size_t &aes_ops = tio.aes_ops();
                if (tio.player() == 2) {
                    RDPFPair<1> dp = tio.rdpfpair(yield, depth);
                    for (int i=0;i<2;++i) {
                        RDPF<1> &dpf = dp.dpf[i];
                        dpf.expand(aes_ops);
                        RDPF<1>::RegXSW scaled_xor;
                        for (address_t x=0;x<(address_t(1)<<depth);++x) {
                            RDPF<1>::LeafNode leaf = dpf.leaf(x, aes_ops);
                            RDPF<1>::RegXSW sx = dpf.scaled_xs(leaf);
                            scaled_xor ^= sx;
                        }
                        printf("%016lx\n%016lx\n", scaled_xor[0].xshare,
                            dpf.li[0].scaled_xor[0].xshare);
                        printf("\n");
                    }
                } else {
                    RDPFTriple<1> dt = tio.rdpftriple(yield, depth);
                    for (int i=0;i<3;++i) {
                        RDPF<1> &dpf = dt.dpf[i];
                        dpf.expand(aes_ops);
                        RDPF<1>::RegXSW scaled_xor;
                        for (address_t x=0;x<(address_t(1)<<depth);++x) {
                            RDPF<1>::LeafNode leaf = dpf.leaf(x, aes_ops);
                            RDPF<1>::RegXSW sx = dpf.scaled_xs(leaf);
                            scaled_xor ^= sx;
                        }
                        printf("%016lx\n%016lx\n", scaled_xor[0].xshare,
                            dpf.li[0].scaled_xor[0].xshare);
                        printf("\n");
                    }
                }
            });
        });
    }
    pool.join();
}

static value_t parallel_streameval_rdpf(MPCTIO &tio, const RDPF<1> &dpf,
    address_t start, int num_threads)
{
    RDPF<1>::RegXSW scaled_xor[num_threads];
    size_t aes_ops[num_threads];
    boost::asio::thread_pool pool(num_threads);
    address_t totsize = (address_t(1)<<dpf.depth());
    address_t threadstart = start;
    address_t threadchunk = totsize / num_threads;
    address_t threadextra = totsize % num_threads;
    for (int thread_num = 0; thread_num < num_threads; ++thread_num) {
        address_t threadsize = threadchunk + (address_t(thread_num) < threadextra);
        boost::asio::post(pool,
            [&tio, &dpf, &scaled_xor, &aes_ops, thread_num, threadstart, threadsize] {
//printf("Thread %d from %X for %X\n", thread_num, threadstart, threadsize);
                RDPF<1>::RegXSW local_xor;
                size_t local_aes_ops = 0;
                auto ev = StreamEval(dpf, threadstart, 0, local_aes_ops);
                for (address_t x=0;x<threadsize;++x) {
//if (x%0x10000 == 0) printf("%d", thread_num);
                    RDPF<1>::LeafNode leaf = ev.next();
                    local_xor ^= dpf.scaled_xs(leaf);
                }
                scaled_xor[thread_num] = local_xor;
                aes_ops[thread_num] = local_aes_ops;
//printf("Thread %d complete\n", thread_num);
            });
        threadstart = (threadstart + threadsize) % totsize;
    }
    pool.join();
    RDPF<1>::RegXSW res;
    for (int thread_num = 0; thread_num < num_threads; ++thread_num) {
        res ^= scaled_xor[thread_num];
        tio.aes_ops() += aes_ops[thread_num];
    }
    return res[0].xshare;
}

static void rdpfeval_timing(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    nbits_t depth=6;
    address_t start=0;

    if (*args) {
        depth = atoi(*args);
        ++args;
    }
    if (*args) {
        start = strtoull(*args, NULL, 16);
        ++args;
    }

    int num_threads = opts.num_cpu_threads;
    MPCTIO tio(mpcio, 0, num_threads);
    run_coroutines(tio, [&tio, depth, start, num_threads] (yield_t &yield) {
        if (tio.player() == 2) {
            RDPFPair<1> dp = tio.rdpfpair(yield, depth);
            for (int i=0;i<2;++i) {
                RDPF<1> &dpf = dp.dpf[i];
                value_t scaled_xor =
                    parallel_streameval_rdpf(tio, dpf, start, num_threads);
                printf("%016lx\n%016lx\n", scaled_xor,
                    dpf.li[0].scaled_xor[0].xshare);
                printf("\n");
            }
        } else {
            RDPFTriple<1> dt = tio.rdpftriple(yield, depth);
            for (int i=0;i<3;++i) {
                RDPF<1> &dpf = dt.dpf[i];
                value_t scaled_xor =
                    parallel_streameval_rdpf(tio, dpf, start, num_threads);
                printf("%016lx\n%016lx\n", scaled_xor,
                    dpf.li[0].scaled_xor[0].xshare);
                printf("\n");
            }
        }
    });
}

static void par_rdpfeval_timing(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    nbits_t depth=6;
    address_t start=0;

    if (*args) {
        depth = atoi(*args);
        ++args;
    }
    if (*args) {
        start = strtoull(*args, NULL, 16);
        ++args;
    }

    int num_threads = opts.num_cpu_threads;
    MPCTIO tio(mpcio, 0, num_threads);
    run_coroutines(tio, [&tio, depth, start, num_threads] (yield_t &yield) {
        if (tio.player() == 2) {
            RDPFPair<1> dp = tio.rdpfpair(yield, depth);
            for (int i=0;i<2;++i) {
                RDPF<1> &dpf = dp.dpf[i];
                nbits_t depth = dpf.depth();
                auto pe = ParallelEval(dpf, start, 0,
                    address_t(1)<<depth, num_threads, tio.aes_ops());
                RDPF<1>::RegXSW result, init;
                result = pe.reduce(init, [&dpf] (int thread_num,
                        address_t i, const RDPF<1>::LeafNode &leaf) {
                    return dpf.scaled_xs(leaf);
                });
                printf("%016lx\n%016lx\n", result[0].xshare,
                    dpf.li[0].scaled_xor[0].xshare);
                printf("\n");
            }
        } else {
            RDPFTriple<1> dt = tio.rdpftriple(yield, depth);
            for (int i=0;i<3;++i) {
                RDPF<1> &dpf = dt.dpf[i];
                nbits_t depth = dpf.depth();
                auto pe = ParallelEval(dpf, start, 0,
                    address_t(1)<<depth, num_threads, tio.aes_ops());
                RDPF<1>::RegXSW result, init;
                result = pe.reduce(init, [&dpf] (int thread_num,
                        address_t i, const RDPF<1>::LeafNode &leaf) {
                    return dpf.scaled_xs(leaf);
                });
                printf("%016lx\n%016lx\n", result[0].xshare,
                    dpf.li[0].scaled_xor[0].xshare);
                printf("\n");
            }
        }
    });
}

static void tupleeval_timing(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    nbits_t depth=6;
    address_t start=0;

    if (*args) {
        depth = atoi(*args);
        ++args;
    }
    if (*args) {
        start = atoi(*args);
        ++args;
    }

    int num_threads = opts.num_cpu_threads;
    MPCTIO tio(mpcio, 0, num_threads);
    run_coroutines(tio, [&tio, depth, start] (yield_t &yield) {
        size_t &aes_ops = tio.aes_ops();
        if (tio.player() == 2) {
            RDPFPair<1> dp = tio.rdpfpair(yield, depth);
            RDPF<1>::RegXSW scaled_xor0, scaled_xor1;
            auto ev = StreamEval(dp, start, 0, aes_ops, false);
            for (address_t x=0;x<(address_t(1)<<depth);++x) {
                auto [L0, L1] = ev.next();
                RDPF<1>::RegXSW sx0 = dp.dpf[0].scaled_xs(L0);
                RDPF<1>::RegXSW sx1 = dp.dpf[1].scaled_xs(L1);
                scaled_xor0 ^= sx0;
                scaled_xor1 ^= sx1;
            }
            printf("%016lx\n%016lx\n", scaled_xor0[0].xshare,
                dp.dpf[0].li[0].scaled_xor[0].xshare);
            printf("\n");
            printf("%016lx\n%016lx\n", scaled_xor1[0].xshare,
                dp.dpf[1].li[0].scaled_xor[0].xshare);
            printf("\n");
        } else {
            RDPFTriple<1> dt = tio.rdpftriple(yield, depth);
            RDPF<1>::RegXSW scaled_xor0, scaled_xor1, scaled_xor2;
            auto ev = StreamEval(dt, start, 0, aes_ops, false);
            for (address_t x=0;x<(address_t(1)<<depth);++x) {
                auto [L0, L1, L2] = ev.next();
                RDPF<1>::RegXSW sx0 = dt.dpf[0].scaled_xs(L0);
                RDPF<1>::RegXSW sx1 = dt.dpf[1].scaled_xs(L1);
                RDPF<1>::RegXSW sx2 = dt.dpf[2].scaled_xs(L2);
                scaled_xor0 ^= sx0;
                scaled_xor1 ^= sx1;
                scaled_xor2 ^= sx2;
            }
            printf("%016lx\n%016lx\n", scaled_xor0[0].xshare,
                dt.dpf[0].li[0].scaled_xor[0].xshare);
            printf("\n");
            printf("%016lx\n%016lx\n", scaled_xor1[0].xshare,
                dt.dpf[1].li[0].scaled_xor[0].xshare);
            printf("\n");
            printf("%016lx\n%016lx\n", scaled_xor2[0].xshare,
                dt.dpf[2].li[0].scaled_xor[0].xshare);
            printf("\n");
        }
    });
}

static void par_tupleeval_timing(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    nbits_t depth=6;
    address_t start=0;

    if (*args) {
        depth = atoi(*args);
        ++args;
    }
    if (*args) {
        start = atoi(*args);
        ++args;
    }

    int num_threads = opts.num_cpu_threads;
    MPCTIO tio(mpcio, 0, num_threads);
    run_coroutines(tio, [&tio, depth, start, num_threads] (yield_t &yield) {
        size_t &aes_ops = tio.aes_ops();
        if (tio.player() == 2) {
            RDPFPair<1> dp = tio.rdpfpair(yield, depth);
            auto pe = ParallelEval(dp, start, 0, address_t(1)<<depth,
                num_threads, aes_ops);
            RDPFPair<1>::RegXSWP result, init;
            result = pe.reduce(init, [&dp] (int thread_num, address_t i,
                    const RDPFPair<1>::LeafNode &leaf) {
                RDPFPair<1>::RegXSWP scaled;
                dp.scaled(scaled, leaf);
                return scaled;
            });
            printf("%016lx\n%016lx\n", std::get<0>(result)[0].xshare,
                dp.dpf[0].li[0].scaled_xor[0].xshare);
            printf("\n");
            printf("%016lx\n%016lx\n", std::get<1>(result)[0].xshare,
                dp.dpf[1].li[0].scaled_xor[0].xshare);
            printf("\n");
        } else {
            RDPFTriple<1> dt = tio.rdpftriple(yield, depth);
            auto pe = ParallelEval(dt, start, 0, address_t(1)<<depth,
                num_threads, aes_ops);
            RDPFTriple<1>::RegXSWT result, init;
            result = pe.reduce(init, [&dt] (int thread_num, address_t i,
                    const RDPFTriple<1>::LeafNode &leaf) {
                RDPFTriple<1>::RegXSWT scaled;
                dt.scaled(scaled, leaf);
                return scaled;
            });
            printf("%016lx\n%016lx\n", std::get<0>(result)[0].xshare,
                dt.dpf[0].li[0].scaled_xor[0].xshare);
            printf("\n");
            printf("%016lx\n%016lx\n", std::get<1>(result)[0].xshare,
                dt.dpf[1].li[0].scaled_xor[0].xshare);
            printf("\n");
            printf("%016lx\n%016lx\n", std::get<2>(result)[0].xshare,
                dt.dpf[2].li[0].scaled_xor[0].xshare);
            printf("\n");
        }
    });
}

// T is RegAS or RegXS for additive or XOR shared database respectively
template <typename T>
static void duoram_test(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    nbits_t depth=6;
    address_t share=arc4random();

    if (*args) {
        depth = atoi(*args);
        ++args;
    }
    if (*args) {
        share = atoi(*args);
        ++args;
    }
    share &= ((address_t(1)<<depth)-1);
    address_t len = (1<<depth);
    if (*args) {
        len = atoi(*args);
        ++args;
    }

    MPCTIO tio(mpcio, 0, opts.num_cpu_threads);
    run_coroutines(tio, [&tio, depth, share, len] (yield_t &yield) {
        // size_t &aes_ops = tio.aes_ops();
        Duoram<T> oram(tio.player(), len);  // T is RegXS or RegAS (defines how database is shared)
        auto A = oram.flat(tio, yield);
        RegAS aidx, aidx2, aidx3;
        aidx.ashare = share;
        aidx2.ashare = share + tio.player();
        aidx3.ashare = share + 1;
        T M;
        if (tio.player() == 0) {
            M.set(0xbabb0000);
        } else {
            M.set(0x0000a66e);
        }
        RegXS xidx;
        xidx.xshare = share;
        T N;
        if (tio.player() == 0) {
            N.set(0xdead0000);
        } else {
            N.set(0x0000beef);
        }
        RegXS oxidx;
        oxidx.xshare = share+3*tio.player();
        T O;
        if (tio.player() == 0) {
            O.set(0x31410000);
        } else {
            O.set(0x00005926);
        }
        // Writing and reading with additively shared indices
        printf("Additive Updating\n");
        A[aidx] += M;
        printf("Additive Reading\n");
        T Aa = A[aidx];
        // Writing and reading with XOR shared indices
        printf("XOR Updating\n");
        A[xidx] += N;
        printf("XOR Reading\n");
        T Ax = A[xidx];
        // Writing and reading with OblivIndex indices
        auto oidx = A.oblivindex(oxidx);
        printf("OblivIndex Updating\n");
        A[oidx] += O;
        printf("OblivIndex Reading\n");
        T Ox = A[oidx];
        // Writing and reading with explicit indices
        T Ae;
        if (depth > 2) {
            printf("Explicit Updating\n");
            A[5] += Aa;
            printf("Explicit Reading\n");
            Ae = A[6];
        }

        // Simultaneous independent reads
        printf("3 independent reading\n");
        std::vector<T> Av = A[std::array {
            aidx, aidx2, aidx3
        }];

        // Simultaneous independent updates
        T Aw1, Aw2, Aw3;
        Aw1.set(0x101010101010101 * tio.player());
        Aw2.set(0x202020202020202 * tio.player());
        Aw3.set(0x303030303030303 * tio.player());
        printf("3 independent updating\n");
        A[std::array { aidx, aidx2, aidx3 }] -=
            std::array { Aw1, Aw2, Aw3 };

        if (depth <= 10) {
            oram.dump();
            auto check = A.reconstruct();
            if (tio.player() == 0) {
                for (address_t i=0;i<len;++i) {
                    printf("%04x %016lx\n", i, check[i].share());
                }
            }
        }
        auto checkread = A.reconstruct(Aa);
        auto checkreade = A.reconstruct(Ae);
        auto checkreadx = A.reconstruct(Ax);
        auto checkreado = A.reconstruct(Ox);
        if (tio.player() == 0) {
            printf("Read AS value = %016lx\n", checkread.share());
            printf("Read AX value = %016lx\n", checkreadx.share());
            printf("Read Ex value = %016lx\n", checkreade.share());
            printf("Read OI value = %016lx\n", checkreado.share());
        }
        for (auto &v : Av) {
            auto checkv = A.reconstruct(v);
            if (tio.player() == 0) {
                printf("Read Av value = %016lx\n", checkv.share());
            }
        }
    });
}

// This measures the same things as the Duoram paper: dependent and
// independent reads, updates, writes, and interleaves
// T is RegAS or RegXS for additive or XOR shared database respectively
template <typename T>
static void duoram(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    nbits_t depth = 6;
    int items = 4;

    if (*args) {
        depth = atoi(*args);
        ++args;
    }
    if (*args) {
        items = atoi(*args);
        ++args;
    }

    MPCTIO tio(mpcio, 0, opts.num_cpu_threads);
    run_coroutines(tio, [&mpcio, &tio, depth, items] (yield_t &yield) {
        size_t size = size_t(1)<<depth;
        address_t mask = (depth < ADDRESS_MAX_BITS ?
            ((address_t(1)<<depth) - 1) : ~0);
        Duoram<T> oram(tio.player(), size);
        auto A = oram.flat(tio, yield);

        std::cout << "===== DEPENDENT UPDATES =====\n";
        mpcio.reset_stats();
        tio.reset_lamport();
        // Make a linked list of length items
        std::vector<T> list_indices;
        T prev_index, next_index;
        prev_index.randomize(depth);
        for (int i=0;i<items;++i) {
            next_index.randomize(depth);
            A[next_index] += prev_index;
            list_indices.push_back(next_index);
            prev_index = next_index;
        }
        T A_tmp = A[prev_index];  // has type Duoram<RegAS>::Shape::MemRefS<RegAS, RegAS, std::nullopt_t, Duoram<RegAS>::Flat, (unsigned char)1>
        tio.sync_lamport();
        mpcio.dump_stats(std::cout);

        std::cout << "\n===== DEPENDENT READS =====\n";
        mpcio.reset_stats();
        tio.reset_lamport();

        // Comparing functionality for testing
        auto A_valuet = static_cast<T>(A[prev_index]);
        A_tmp.test(A_valuet);

        // Read the linked list starting with prev_index
        T cur_index = prev_index;
        for (int i=0;i<items;++i) {
            cur_index = A[cur_index];
        }
        tio.sync_lamport();
        mpcio.dump_stats(std::cout);

        std::cout << "\n===== INDEPENDENT READS =====\n";
        mpcio.reset_stats();
        tio.reset_lamport();
        // Read all the entries in the list at once
        std::vector<T> read_outputs = A[list_indices];
        tio.sync_lamport();
        mpcio.dump_stats(std::cout);

        A_valuet = static_cast<T>(A[prev_index]);
        A_tmp.test(A_valuet);

        std::cout << "\n===== INDEPENDENT UPDATES =====\n";
        mpcio.reset_stats();
        tio.reset_lamport();
        // Make a vector of indices 1 larger than those in list_indices,
        // and a vector of values 1 larger than those in outputs
        std::vector<T> indep_indices, indep_values;
        T one;
        one.set(tio.player());  // Sets the shared value to 1
        for (int i=0;i<items;++i) {
            indep_indices.push_back(list_indices[i]+one);
            indep_values.push_back(read_outputs[i]+one);
        }
        // Update all the indices at once
        A[indep_indices] += indep_values;
        tio.sync_lamport();
        mpcio.dump_stats(std::cout);

        std::cout << "\n===== DEPENDENT WRITES =====\n";
        mpcio.reset_stats();
        tio.reset_lamport();
        T two;
        two.set(2*tio.player());  // Sets the shared value to 2
        // For each address addr that's number i from the end of the
        // linked list, write i+1 into location addr+2
        for (int i=0;i<items;++i) {
            T val;
            val.set((i+1)*tio.player());
            A[list_indices[i]+two] = val;
        }
        tio.sync_lamport();
        mpcio.dump_stats(std::cout);

        A_tmp.test(A_valuet);

        std::cout << "\n===== DEPENDENT INTERLEAVED =====\n";
        mpcio.reset_stats();
        tio.reset_lamport();
        T three;
        three.set(3*tio.player());  // Sets the shared value to 3
        // Follow the linked list and whenever A[addr]=val, set
        // A[addr+3]=val+3
        cur_index = prev_index;
        for (int i=0;i<items;++i) {
            T next_index = A[cur_index];
            A[cur_index+three] = next_index+three;
            cur_index = next_index;
        }
        tio.sync_lamport();
        mpcio.dump_stats(std::cout);

        A_valuet = static_cast<T>(A[prev_index]);
        A_tmp.test(A_valuet);


        std::cout << "\n";
        mpcio.reset_stats();
        tio.reset_lamport();

        if (depth <= 30) {
            auto check = A.reconstruct();
            auto head = A.reconstruct(prev_index);
            if (tio.player() == 0) {
                int width = (depth+3)/4;
                printf("Head of linked list: %0*lx\n\n", width,
                    head.share() & mask);
                std::cout << "Non-zero reconstructed database entries:\n";
                for (address_t i=0;i<size;++i) {
                    value_t share = check[i].share() & mask;
                    if (share) printf("%0*x: %0*lx\n", width, i, width, share);
                }
            }
        }
    });
}

// This measures just sequential (dependent) reads
// T is RegAS or RegXS for additive or XOR shared database respectively
template <typename T>
static void read_test(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    nbits_t depth = 6;
    int items = 4;

    if (*args) {
        depth = atoi(*args);
        ++args;
    }
    if (*args) {
        items = atoi(*args);
        ++args;
    }

    MPCTIO tio(mpcio, 0, opts.num_cpu_threads);
    run_coroutines(tio, [&mpcio, &tio, depth, items] (yield_t &yield) {
        size_t size = size_t(1)<<depth;
        Duoram<T> oram(tio.player(), size);
        auto A = oram.flat(tio, yield);

        std::cout << "\n===== SEQUENTIAL READS =====\n";
        T totval;
        for (int i=0;i<items;++i) {
            RegXS idx;
            idx.randomize(depth);
            T val = A[idx];
            totval += val;
        }
        printf("Total value read: %016lx\n", totval.share());
    });
}

static void cdpf_test(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    value_t query, target;
    int iters = 1;
    arc4random_buf(&query, sizeof(query));
    arc4random_buf(&target, sizeof(target));

    if (*args) {
        query = strtoull(*args, NULL, 16);
        ++args;
    }
    if (*args) {
        target = strtoull(*args, NULL, 16);
        ++args;
    }
    if (*args) {
        iters = atoi(*args);
        ++args;
    }

    int num_threads = opts.num_comm_threads;
    boost::asio::thread_pool pool(num_threads);
    for (int thread_num = 0; thread_num < num_threads; ++thread_num) {
        boost::asio::post(pool, [&mpcio, thread_num, query, target, iters] {
            MPCTIO tio(mpcio, thread_num);
            run_coroutines(tio, [&tio, query, target, iters] (yield_t &yield) {
                size_t &aes_ops = tio.aes_ops();
                for (int i=0;i<iters;++i) {
                    if (tio.player() == 2) {
                        tio.cdpf(yield);
                        auto [ dpf0, dpf1 ] = CDPF::generate(target, aes_ops);
                        DPFnode leaf0 = dpf0.leaf(query, aes_ops);
                        DPFnode leaf1 = dpf1.leaf(query, aes_ops);
                        printf("DPFXOR_{%016lx}(%016lx} = ", target, query);
                        dump_node(leaf0 ^ leaf1);
                    } else {
                        CDPF dpf = tio.cdpf(yield);
                        printf("ashare = %016lX\nxshare = %016lX\n",
                            dpf.as_target.ashare, dpf.xs_target.xshare);
                        DPFnode leaf = dpf.leaf(query, aes_ops);
                        printf("DPF(%016lx) = ", query);
                        dump_node(leaf);
                        if (tio.player() == 1) {
                            tio.iostream_peer() << leaf;
                        } else {
                            DPFnode peerleaf;
                            tio.iostream_peer() >> peerleaf;
                            printf("XOR = ");
                            dump_node(leaf ^ peerleaf);
                        }
                    }
                }
            });
        });
    }
    pool.join();
}

static int compare_test_one(MPCTIO &tio, yield_t &yield,
    value_t target, value_t x)
{
    int player = tio.player();
    size_t &aes_ops = tio.aes_ops();
    int res = 1;
    if (player == 2) {
        // Create a CDPF pair with the given target
        auto [dpf0, dpf1] = CDPF::generate(target, aes_ops);
        // Send it and a share of x to the computational parties
        RegAS x0, x1;
        x0.randomize();
        x1.set(x-x0.share());
        tio.iostream_p0() << dpf0 << x0;
        tio.iostream_p1() << dpf1 << x1;
    } else {
        CDPF dpf;
        RegAS xsh;
        tio.iostream_server() >> dpf >> xsh;
        auto [lt, eq, gt] = dpf.compare(tio, yield, xsh, aes_ops);
        RegBS eeq = dpf.is_zero(tio, yield, xsh, aes_ops);
        printf("%016lx %016lx %d %d %d %d ", target, x, lt.bshare,
            eq.bshare, gt.bshare, eeq.bshare);
        // Check the answer
        if (player == 1) {
            tio.iostream_peer() << xsh << lt << eq << gt << eeq;
        } else {
            RegAS peer_xsh;
            RegBS peer_lt, peer_eq, peer_gt, peer_eeq;
            tio.iostream_peer() >> peer_xsh >> peer_lt >> peer_eq >>
                peer_gt >> peer_eeq;
            lt ^= peer_lt;
            eq ^= peer_eq;
            gt ^= peer_gt;
            eeq ^= peer_eeq;
            xsh += peer_xsh;
            int lti = int(lt.bshare);
            int eqi = int(eq.bshare);
            int gti = int(gt.bshare);
            int eeqi = int(eeq.bshare);
            x = xsh.share();
            printf(": %d %d %d %d ", lti, eqi, gti, eeqi);
            bool signbit = (x >> 63);
            if (lti + eqi + gti != 1 || eqi != eeqi) {
                printf("INCONSISTENT");
                res = 0;
            } else if (x == 0 && eqi) {
                printf("=");
            } else if (!signbit && gti) {
                printf(">");
            } else if (signbit && lti) {
                printf("<");
            } else {
                printf("INCORRECT");
                res = 0;
            }
        }
        printf("\n");
    }
    return res;
}

static int compare_test_target(MPCTIO &tio, yield_t &yield,
    value_t target, value_t x)
{
    int res = 1;
    res &= compare_test_one(tio, yield, target, x);
    res &= compare_test_one(tio, yield, target, 0);
    res &= compare_test_one(tio, yield, target, 1);
    res &= compare_test_one(tio, yield, target, 15);
    res &= compare_test_one(tio, yield, target, 16);
    res &= compare_test_one(tio, yield, target, 17);
    res &= compare_test_one(tio, yield, target, -1);
    res &= compare_test_one(tio, yield, target, -15);
    res &= compare_test_one(tio, yield, target, -16);
    res &= compare_test_one(tio, yield, target, -17);
    res &= compare_test_one(tio, yield, target, (value_t(1)<<63));
    res &= compare_test_one(tio, yield, target, (value_t(1)<<63)+1);
    res &= compare_test_one(tio, yield, target, (value_t(1)<<63)-1);
    return res;
}

static void compare_test(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    value_t target, x;
    arc4random_buf(&target, sizeof(target));
    arc4random_buf(&x, sizeof(x));

    if (*args) {
        target = strtoull(*args, NULL, 16);
        ++args;
    }
    if (*args) {
        x = strtoull(*args, NULL, 16);
        ++args;
    }

    int num_threads = opts.num_comm_threads;
    boost::asio::thread_pool pool(num_threads);
    for (int thread_num = 0; thread_num < num_threads; ++thread_num) {
        boost::asio::post(pool, [&mpcio, thread_num, target, x] {
            MPCTIO tio(mpcio, thread_num);
            run_coroutines(tio, [&tio, target, x] (yield_t &yield) {
                int res = 1;
                res &= compare_test_target(tio, yield, target, x);
                res &= compare_test_target(tio, yield, 0, x);
                res &= compare_test_target(tio, yield, 1, x);
                res &= compare_test_target(tio, yield, 15, x);
                res &= compare_test_target(tio, yield, 16, x);
                res &= compare_test_target(tio, yield, 17, x);
                res &= compare_test_target(tio, yield, -1, x);
                res &= compare_test_target(tio, yield, -15, x);
                res &= compare_test_target(tio, yield, -16, x);
                res &= compare_test_target(tio, yield, -17, x);
                res &= compare_test_target(tio, yield, (value_t(1)<<63), x);
                res &= compare_test_target(tio, yield, (value_t(1)<<63)+1, x);
                res &= compare_test_target(tio, yield, (value_t(1)<<63)-1, x);
                if (tio.player() == 0) {
                    if (res == 1) {
                        printf("All tests passed!\n");
                    } else {
                        printf("TEST FAILURES\n");
                    }
                }
            });
        });
    }
    pool.join();
}

static void sort_test(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    nbits_t depth=6;

    if (*args) {
        depth = atoi(*args);
        ++args;
    }
    address_t len = (1<<depth);
    if (*args) {
        len = atoi(*args);
        ++args;
    }

    MPCTIO tio(mpcio, 0, opts.num_cpu_threads);
    run_coroutines(tio, [&tio, depth, len] (yield_t &yield) {
        address_t size = address_t(1)<<depth;
        // size_t &aes_ops = tio.aes_ops();
        Duoram<RegAS> oram(tio.player(), size);
        auto A = oram.flat(tio, yield);
        A.explicitonly(true);
        // Initialize the memory to random values in parallel
        std::vector<coro_t> coroutines;
        for (address_t i=0; i<size; ++i) {
            coroutines.emplace_back(
                [&A, i](yield_t &yield) {
                    auto Acoro = A.context(yield);
                    RegAS v;
                    v.randomize(62);
                    Acoro[i] += v;
                });
        }
        run_coroutines(yield, coroutines);
        A.bitonic_sort(0, len);
        if (depth <= 10) {
            oram.dump();
        }
        auto check = A.reconstruct();
        bool fail = false;
        if (tio.player() == 0) {
            for (address_t i=0;i<size;++i) {
                if (depth <= 10) {
                    printf("%04x %016lx\n", i, check[i].share());
                }
                if (i>0 && i<len &&
                    check[i].share() < check[i-1].share()) {
                    fail = true;
                }
            }
            if (fail) {
                printf("FAIL\n");
            } else {
                printf("PASS\n");
            }
        }
    });
}

static void pad_test(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    nbits_t depth=6;

    if (*args) {
        depth = atoi(*args);
        ++args;
    }
    address_t len = (1<<depth);
    if (*args) {
        len = atoi(*args);
        ++args;
    }

    MPCTIO tio(mpcio, 0, opts.num_cpu_threads);
    run_coroutines(tio, [&mpcio, &tio, depth, len] (yield_t &yield) {
        int player = tio.player();
        Duoram<RegAS> oram(player, len);
        auto A = oram.flat(tio, yield);
        // Initialize the ORAM in explicit mode
        A.explicitonly(true);
        for (address_t i=0; i<len; ++i) {
            RegAS v;
            v.set((player*0xffff+1)*i);
            A[i] = v;
        }
        A.explicitonly(false);
        // Obliviously add 0 to A[0], which reblinds the whole database
        RegAS z;
        A[z] += z;
        auto check = A.reconstruct();
        if (player == 0) {
            for (address_t i=0;i<len;++i) {
                if (depth <= 10) {
                    printf("%04x %016lx\n", i, check[i].share());
                }
            }
            printf("\n");
        }
        address_t maxsize = address_t(1)<<depth;
        Duoram<RegAS>::Pad P(A, tio, yield, maxsize);
        for (address_t i=0; i<maxsize; ++i) {
            RegAS v = P[i];
            if (depth <= 10) {
                value_t vval = mpc_reconstruct(tio, yield, v);
                printf("%04x %016lx %016lx\n", i, v.share(), vval);
            }
        }
        printf("\n");
        for (address_t i=0; i<maxsize; ++i) {
            value_t offset = 0xdeadbeef;
            if (player) {
                offset = -offset;
            }
            RegAS ind;
            ind.set(player*i+offset);
            RegAS v = P[ind];
            if (depth <= 10) {
                value_t vval = mpc_reconstruct(tio, yield, v);
                printf("%04x %016lx %016lx\n", i, v.share(), vval);
            }
        }
        printf("\n");
    });
}

// T is RegAS for basic bsearch, or RegXS for optimized bsearch
template<typename T,bool basic>
static void bsearch_test(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    value_t target;
    arc4random_buf(&target, sizeof(target));
    target >>= 1;
    nbits_t depth=6;
    bool is_presorted = true;

    // Use a random array (which we explicitly sort) instead of a
    // presorted array
    if (*args && !strcmp(args[0], "-r")) {
        is_presorted = false;
        ++args;
    }

    if (*args) {
        depth = atoi(*args);
        ++args;
    }
    address_t len = (1<<depth) - 1;
    int iters = 1;
    if (*args) {
        iters = atoi(*args);
        ++args;
    }
    if (is_presorted) {
        target %= (value_t(len) << 16);
    }
    if (*args) {
        target = strtoull(*args, NULL, 16);
        ++args;
    }

    MPCTIO tio(mpcio, 0, opts.num_cpu_threads);
    run_coroutines(tio, [&tio, &mpcio, depth, len, iters, target, is_presorted] (yield_t &yield) {
        RegAS tshare;
        std::cout << "\n===== SETUP =====\n";

        if (tio.player() == 2) {
            // Send shares of the target to the computational
            // players
            RegAS tshare0, tshare1;
            tshare0.randomize();
            tshare1.set(target-tshare0.share());
            tio.iostream_p0() << tshare0;
            tio.iostream_p1() << tshare1;
            printf("Using target = %016lx\n", target);
            yield();
        } else {
            // Get the share of the target
            yield();
            tio.iostream_server() >> tshare;
        }

        tio.sync_lamport();
        mpcio.dump_stats(std::cout);

        std::cout << "\n===== " << (is_presorted ? "CREATE" : "SORT RANDOM")
            << " DATABASE =====\n";
        mpcio.reset_stats();
        tio.reset_lamport();
        // If is_presorted is true, create a database of presorted
        // values.  If is_presorted is false, create a database of
        // random values and explicitly sort it.
        Duoram<RegAS> oram(tio.player(), len);
        auto A = oram.flat(tio, yield);

        // Initialize the memory to sorted or random values, depending
        // on the is_presorted flag
        if (is_presorted) {
            A.init([](size_t i) {
                return value_t(i) << 16;
            });
        } else {
            A.explicitonly(true);
            for (address_t i=0; i<len; ++i) {
                RegAS v;
                v.randomize(62);
                A[i] = v;
            }
            A.explicitonly(false);
            A.bitonic_sort(0, len);
        }

        tio.sync_lamport();
        mpcio.dump_stats(std::cout);

        std::cout << "\n===== BINARY SEARCH =====\n";
        mpcio.reset_stats();
        tio.reset_lamport();
        // Binary search for the target
        T tindex;
        for (int i=0; i<iters; ++i) {
            if constexpr (basic) {
                tindex = A.basic_binary_search(tshare);
            } else {
                tindex = A.binary_search(tshare);
            }
        }

        // Don't spend time reconstructing the database to check the
        // answer if the database is huge
        if (depth > 25) {
            return;
        }

        tio.sync_lamport();
        mpcio.dump_stats(std::cout);

        std::cout << "\n===== CHECK ANSWER =====\n";
        mpcio.reset_stats();
        tio.reset_lamport();
        // Check the answer
        size_t size = size_t(1) << depth;
        value_t checkindex = mpc_reconstruct(tio, yield, tindex);
        value_t checktarget = mpc_reconstruct(tio, yield, tshare);
        auto check = A.reconstruct();
        bool fail = false;
        if (tio.player() == 0) {
            for (address_t i=0;i<len;++i) {
                if (depth <= 10) {
                    printf("%c%04x %016lx\n",
                        (i == checkindex ? '*' : ' '),
                        i, check[i].share());
                }
                if (i>0 && i<len &&
                    check[i].share() < check[i-1].share()) {
                    fail = true;
                }
                if (i == checkindex) {
                    // check[i] should be >= target, and check[i-1]
                    // should be < target
                    if ((i < len && check[i].share() < checktarget) ||
                        (i > 0 && check[i-1].share() >= checktarget)) {
                        fail = true;
                    }
                }
            }
            if (checkindex == len && check[len-1].share() >= checktarget) {
                fail = true;
            }

            printf("Target = %016lx\n", checktarget);
            printf("Found index = %02lx\n", checkindex);
            if (checkindex > size) {
                fail = true;
            }
            if (fail) {
                printf("FAIL\n");
            } else {
                printf("PASS\n");
            }
        }
    });
}

template <typename T>
static void related(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    nbits_t depth = 5;

    // The depth of the (complete) binary tree
    if (*args) {
        depth = atoi(*args);
        ++args;
    }
    // The layer at which to choose a random parent node (and its two
    // children along with it)
    nbits_t layer = depth-1;
    if (*args) {
        layer = atoi(*args);
        ++args;
    }
    assert(layer < depth);

    MPCTIO tio(mpcio, 0, opts.num_cpu_threads);
    run_coroutines(tio, [&mpcio, &tio, depth, layer] (yield_t &yield) {
        size_t size = size_t(1)<<(depth+1);
        Duoram<T> oram(tio.player(), size);
        auto A = oram.flat(tio, yield);

        // Initialize A with words with sequential top and bottom halves
        // (just so we can more easily eyeball the right answers)
        A.init([] (size_t i) { return i * 0x100000001; } );

        // We use this layout for the tree:
        // A[0] is unused
        // A[1] is the root (layer 0)
        // A[2..3] is layer 1
        // A[4..7] is layer 2
        // ...
        // A[(1<<j)..((2<<j)-1)] is layer j
        //
        // So the parent of x is at location (x/2) and the children of x
        // are at locations 2*x and 2*x+1

        // Pick a random index _within_ the given layer (i.e., the
        // offset from the beginning of the layer, not the absolute
        // location in A)
        RegXS idx;
        idx.randomize(layer);
        // Create the OblivIndex. RegXS is the type of the common index
        // (idx), 3 is the maximum number of related updates to support
        // (which equals the width of the underlying RDPF, currently
        // maximum 5), layer is the depth of the underlying RDPF (the
        // bit length of idx).
        typename Duoram<T>::template OblivIndex<RegXS,3> oidx(tio, yield, idx, layer);

        // This is the (known) layer containing the (unknown) parent
        // node
        typename Duoram<T>::Flat P(A, tio, yield, 1<<layer, 1<<layer);
        // This is the layer below that one, containing all possible
        // children
        typename Duoram<T>::Flat C(A, tio, yield, 2<<layer, 2<<layer);
        // These are the subsets of C containing the left children and
        // the right children respectively
        typename Duoram<T>::Stride L(C, tio, yield, 0, 2);
        typename Duoram<T>::Stride R(C, tio, yield, 1, 2);

        T parent, left, right;

        // Do three related reads.  In this version, only one DPF will
        // be used, but it will still be _evaluated_ three times.
        parent = P[oidx];
        left = L[oidx];
        right = R[oidx];

        // The operation is just a simple rotation: the value in the
        // parent moves to the left child, the left child moves to the
        // right child, and the right child becomes the parent

        // Do three related updates.  As above, only one (wide) DPF will
        // be used (the same one as for the reads in fact), but it will
        // still be _evaluated_ three more times.
        P[oidx] += right-parent;
        L[oidx] += parent-left;
        R[oidx] += left-right;

        // Check the answer
        auto check = A.reconstruct();
        if (depth <= 10) {
            oram.dump();
            if (tio.player() == 0) {
                for (address_t i=0;i<size;++i) {
                    printf("%04x %016lx\n", i, check[i].share());
                }
            }
        }
        value_t pval = mpc_reconstruct(tio, yield, parent);
        value_t lval = mpc_reconstruct(tio, yield, left);
        value_t rval = mpc_reconstruct(tio, yield, right);
        printf("parent = %016lx\nleft   = %016lx\nright  = %016lx\n",
            pval, lval, rval);
    });
}

template <typename T>
static void path(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    nbits_t depth = 5;

    // The depth of the (complete) binary tree
    if (*args) {
        depth = atoi(*args);
        ++args;
    }
    // The target node
    size_t target_node = 3 << (depth-1);
    if (*args) {
        target_node = atoi(*args);
        ++args;
    }

    MPCTIO tio(mpcio, 0, opts.num_cpu_threads);
    run_coroutines(tio, [&mpcio, &tio, depth, target_node] (yield_t &yield) {
        size_t size = size_t(1)<<(depth+1);
        Duoram<T> oram(tio.player(), size);
        auto A = oram.flat(tio, yield);

        // Initialize A with words with sequential top and bottom halves
        // (just so we can more easily eyeball the right answers)
        A.init([] (size_t i) { return i * 0x100000001; } );

        // We use this layout for the tree:
        // A[0] is unused
        // A[1] is the root (layer 0)
        // A[2..3] is layer 1
        // A[4..7] is layer 2
        // ...
        // A[(1<<j)..((2<<j)-1)] is layer j
        //
        // So the parent of x is at location (x/2) and the children of x
        // are at locations 2*x and 2*x+1

        // Create a Path from the root to the target node
        typename Duoram<T>::Path P(A, tio, yield, target_node);

        // Re-initialize that path to something recognizable
        P.init([] (size_t i) { return 0xff + i * 0x1000000010000; } );

        // ORAM update along that path
        RegXS idx;
        idx.set(tio.player() * arc4random_uniform(P.size()));
        T val;
        val.set(tio.player() * 0xaaaa00000000);
        P[idx] += val;

        // Binary search along that path
        T lookup;
        lookup.set(tio.player() * 0x3000000000000);
        RegXS foundidx = P.binary_search(lookup);

        // Check the answer
        auto check = A.reconstruct();
        if (depth <= 10) {
            oram.dump();
            if (tio.player() == 0) {
                for (address_t i=0;i<size;++i) {
                    printf("%04x %016lx\n", i, check[i].share());
                }
            }
        }
        value_t found = mpc_reconstruct(tio, yield, foundidx);
        printf("foundidx = %lu\n", found);
    });
}

void online_main(MPCIO &mpcio, const PRACOptions &opts, char **args)
{
    if (!*args) {
        std::cerr << "Mode is required as the first argument when not preprocessing.\n";
        return;
    } else if (!strcmp(*args, "test")) {
        ++args;
        online_test(mpcio, opts, args);
    } else if (!strcmp(*args, "lamporttest")) {
        ++args;
        lamport_test(mpcio, opts, args);
    } else if (!strcmp(*args, "rdpftest")) {
        ++args;
        rdpf_test<1>(mpcio, opts, args, false);
    } else if (!strcmp(*args, "rdpftest2")) {
        ++args;
        rdpf_test<2>(mpcio, opts, args, false);
    } else if (!strcmp(*args, "rdpftest3")) {
        ++args;
        rdpf_test<3>(mpcio, opts, args, false);
    } else if (!strcmp(*args, "rdpftest4")) {
        ++args;
        rdpf_test<4>(mpcio, opts, args, false);
    } else if (!strcmp(*args, "rdpftest5")) {
        ++args;
        rdpf_test<5>(mpcio, opts, args, false);
    } else if (!strcmp(*args, "irdpftest")) {
        ++args;
        rdpf_test<1>(mpcio, opts, args, true);
    } else if (!strcmp(*args, "irdpftest2")) {
        ++args;
        rdpf_test<2>(mpcio, opts, args, true);
    } else if (!strcmp(*args, "irdpftest3")) {
        ++args;
        rdpf_test<3>(mpcio, opts, args, true);
    } else if (!strcmp(*args, "irdpftest4")) {
        ++args;
        rdpf_test<4>(mpcio, opts, args, true);
    } else if (!strcmp(*args, "irdpftest5")) {
        ++args;
        rdpf_test<5>(mpcio, opts, args, true);
    } else if (!strcmp(*args, "rdpftime")) {
        ++args;
        rdpf_timing(mpcio, opts, args);
    } else if (!strcmp(*args, "evaltime")) {
        ++args;
        rdpfeval_timing(mpcio, opts, args);
    } else if (!strcmp(*args, "parevaltime")) {
        ++args;
        par_rdpfeval_timing(mpcio, opts, args);
    } else if (!strcmp(*args, "tupletime")) {
        ++args;
        tupleeval_timing(mpcio, opts, args);
    } else if (!strcmp(*args, "partupletime")) {
        ++args;
        par_tupleeval_timing(mpcio, opts, args);
    } else if (!strcmp(*args, "duotest")) {
        ++args;
        if (opts.use_xor_db) {
            duoram_test<RegXS>(mpcio, opts, args);
        } else {
            duoram_test<RegAS>(mpcio, opts, args);
        }
    } else if (!strcmp(*args, "read")) {
        ++args;
        if (opts.use_xor_db) {
            read_test<RegXS>(mpcio, opts, args);
        } else {
            read_test<RegAS>(mpcio, opts, args);
        }
    } else if (!strcmp(*args, "cdpftest")) {
        ++args;
        cdpf_test(mpcio, opts, args);
    } else if (!strcmp(*args, "cmptest")) {
        ++args;
        compare_test(mpcio, opts, args);
    } else if (!strcmp(*args, "sorttest")) {
        ++args;
        sort_test(mpcio, opts, args);
    } else if (!strcmp(*args, "padtest")) {
        ++args;
        pad_test(mpcio, opts, args);
    } else if (!strcmp(*args, "bbsearch")) {
        ++args;
        bsearch_test<RegAS,true>(mpcio, opts, args);
    } else if (!strcmp(*args, "bsearch")) {
        ++args;
        bsearch_test<RegXS,false>(mpcio, opts, args);
    } else if (!strcmp(*args, "duoram")) {
        ++args;
        if (opts.use_xor_db) {
            duoram<RegXS>(mpcio, opts, args);
        } else {
            duoram<RegAS>(mpcio, opts, args);
        }
    } else if (!strcmp(*args, "related")) {
        ++args;
        if (opts.use_xor_db) {
            related<RegXS>(mpcio, opts, args);
        } else {
            related<RegAS>(mpcio, opts, args);
        }
    } else if (!strcmp(*args, "path")) {
        ++args;
        path<RegAS>(mpcio, opts, args);
    } else if (!strcmp(*args, "cell")) {
        ++args;
        cell(mpcio, opts, args);
    } else if (!strcmp(*args, "bst")) {
        ++args;
        bst(mpcio, opts, args);
    } else if (!strcmp(*args, "avl")) {
        ++args;
        avl(mpcio, opts, args);
    } else if (!strcmp(*args, "avl_tests")) {
        ++args;
        avl_tests(mpcio, opts, args);
    } else if (!strcmp(*args, "heap")) {
        ++args;
        Heap(mpcio, opts, args);
    } else if (!strcmp(*args, "heapsampler")) {
        ++args;
        heapsampler_test(mpcio, opts, args);
    } else if (!strcmp(*args, "weightedcoin")) {
        ++args;
        weighted_coin_test(mpcio, opts, args);
    } else {
        std::cerr << "Unknown mode " << *args << "\n";
    }
}
