#ifndef MPCOPS_TCC
#define MPCOPS_TCC

template <size_t LWIDTH>
void mpc_reconstruct_choice(MPCTIO &tio, yield_t &yield,
    std::array<DPFnode,LWIDTH> &z, RegBS f,
    const std::array<DPFnode,LWIDTH> &x,
    const std::array<DPFnode,LWIDTH> &y)
{
    for (size_t j=0;j<LWIDTH;++j) {
        DPFnode x_cpy, y_cpy;
        x_cpy = x.at(j);
        y_cpy = y.at(j);
        mpc_reconstruct_choice(tio, yield, z[j],
                    f, x_cpy, y_cpy);
    }
}

#endif