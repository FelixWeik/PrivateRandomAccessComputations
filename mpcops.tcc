#ifndef MPCOPS_TCC
#define MPCOPS_TCC

template <size_t LWIDTH>
void mpc_reconstruct_choice(MPCTIO &tio, yield_t &yield,
    std::array<DPFnode,LWIDTH> &z, RegBS f,
    const std::array<DPFnode,LWIDTH> &x,
    const std::array<DPFnode,LWIDTH> &y)
{
    std::vector<coro_t> coroutines;
    for (size_t j=0;j<LWIDTH;++j) {
        coroutines.emplace_back(
            [&tio, &z, f, &x, &y, j](yield_t &yield) {
                mpc_reconstruct_choice(tio, yield, z[j],
                    f, x[j], y[j]);
            });
    }
    run_coroutines(yield, coroutines);
}

#endif