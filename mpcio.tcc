// T is the type being stored
// N is a type whose "name" static member is a string naming the type
//   so that we can report something useful to the user if they try
//   to read a type that we don't have any more values for
template<typename T, typename N>
PreCompStorage<T,N>::PreCompStorage(unsigned player, ProcessingMode mode,
        const char *filenameprefix, unsigned thread_num) :
        name(N::name), depth(0)
{
    init(player, mode, filenameprefix, thread_num);
}

template<typename T, typename N>
void PreCompStorage<T,N>::init(unsigned player, ProcessingMode mode,
        const char *filenameprefix, unsigned thread_num, nbits_t depth,
        nbits_t width)
{
    if (mode != MODE_ONLINE) return;
    std::string filename(filenameprefix);
    char suffix[20];
    if (depth) {
        this->depth = depth;
        this->width = width;
        sprintf(suffix, "%02d.p%d.t%u", depth, player%10, thread_num);
    } else {
        sprintf(suffix, ".p%d.t%u", player%10, thread_num);
    }
    filename.append(suffix);
    storage.open(filename);
    // It's OK if not every file exists; so don't worry about checking
    // for errors here.  We'll report an error in get() if we actually
    // try to use a value for which we don't have a precomputed file.
    count = 0;
}

template<typename T, typename N>
void PreCompStorage<T,N>::get(T& nextval)
{
    storage >> nextval;
    if (!storage.good()) {
        std::cerr << "Failed to read precomputed value from " << name << std::endl;
        std::cout << "stream rdstate: "
         << storage.rdstate() << std::endl;
        if (depth) {
            std::cerr << (int)depth;
        }
        if (width > 1) {
            std::cerr << "." << (int)width;
        }
        std::cerr << " storage\n";
        exit(1);
    }
    ++count;
}

// Only computational peers call this; the server should be calling
// rdpfpair() at the same time
template <nbits_t WIDTH>
RDPFTriple<WIDTH> MPCTIO::rdpftriple(yield_t &yield, nbits_t depth,
    bool incremental, bool keep_expansion)
{
    assert(mpcio.player < 2);
    RDPFTriple<WIDTH> val;

    MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);
    if (mpcio.mode == MODE_ONLINE) {
        if (incremental) {
            std::get<WIDTH-1>(mpcpio.irdpftriples)
                [thread_num][depth-1].get(val);
        } else {
            std::get<WIDTH-1>(mpcpio.rdpftriples)
                [thread_num][depth-1].get(val);
        }
    } else {
        val = RDPFTriple<WIDTH>(*this, yield, depth,
            incremental, keep_expansion);
        iostream_server() <<
            val.dpf[(mpcio.player == 0) ? 1 : 2];
        if (incremental) {
            std::get<WIDTH-1>(mpcpio.irdpftriples)
                [thread_num][depth-1].inc();
        } else {
            std::get<WIDTH-1>(mpcpio.rdpftriples)
                [thread_num][depth-1].inc();
        }
        yield();
    }
    return val;
}

// Only the server calls this; the computational peers should be calling
// rdpftriple() at the same time
template <nbits_t WIDTH>
RDPFPair<WIDTH> MPCTIO::rdpfpair(yield_t &yield, nbits_t depth,
    bool incremental)
{
    assert(mpcio.player == 2);
    RDPFPair<WIDTH> val;

    MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);
    if (mpcio.mode == MODE_ONLINE) {
        if (incremental) {
            std::get<WIDTH-1>(mpcsrvio.irdpfpairs)
                [thread_num][depth-1].get(val);
        } else {
            std::get<WIDTH-1>(mpcsrvio.rdpfpairs)
                [thread_num][depth-1].get(val);
        }
    } else {
        RDPFTriple<WIDTH> trip(*this, yield, depth, incremental, true);
        yield();
        iostream_p0() >> val.dpf[0];
        iostream_p1() >> val.dpf[1];
        if (incremental) {
            std::get<WIDTH-1>(mpcsrvio.irdpfpairs)
                [thread_num][depth-1].inc();
        } else {
            std::get<WIDTH-1>(mpcsrvio.rdpfpairs)
                [thread_num][depth-1].inc();
        }
    }
    return val;
}
