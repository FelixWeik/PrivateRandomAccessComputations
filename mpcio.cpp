#include <sys/time.h>      // getrusage
#include <sys/resource.h>  // getrusage
#include "mpcio.hpp"
#include "rdpf.hpp"
#include "bitutils.hpp"
#include "coroutine.hpp"

void MPCSingleIO::async_send_from_msgqueue()
{
#ifdef SEND_LAMPORT_CLOCKS
    std::vector<boost::asio::const_buffer> tosend;
    tosend.push_back(boost::asio::buffer(messagequeue.front().header));
    tosend.push_back(boost::asio::buffer(messagequeue.front().message));
#endif
    boost::asio::async_write(sock,
#ifdef SEND_LAMPORT_CLOCKS
        tosend,
#else
        boost::asio::buffer(messagequeue.front()),
#endif
        [&](boost::system::error_code ec, std::size_t amt){
            messagequeuelock.lock();
            messagequeue.pop();
            if (messagequeue.size() > 0) {
                async_send_from_msgqueue();
            }
            messagequeuelock.unlock();
        });
}

size_t MPCSingleIO::queue(const void *data, size_t len, lamport_t lamport)
{
    // Is this a new message?
    size_t newmsg = 0;

    dataqueue.append((const char *)data, len);

    // If this is the first queue() since the last explicit send(),
    // which we'll know because message_lamport will be nullopt, set
    // message_lamport to the current Lamport clock.  Note that the
    // boolean test tests whether message_lamport is nullopt, not
    // whether its value is zero.
    if (!message_lamport) {
        message_lamport = lamport;
        newmsg = 1;
    }

#ifdef VERBOSE_COMMS
    struct timeval tv;
    gettimeofday(&tv, NULL);
    printf("%lu.%06lu: Queue %s.%d len=%lu lamp=%u: ", tv.tv_sec,
        tv.tv_usec, dest.c_str(), thread_num, len,
        message_lamport.value());
    for (size_t i=0;i<len;++i) {
        printf("%02x", ((const unsigned char*)data)[i]);
    }
    printf("\n");
#endif

    // If we already have some full packets worth of data, may as
    // well send it.
    if (dataqueue.size() > 28800) {
        std::cout << "Kuckuck" << std::endl;
    }
    send(true);


    return newmsg;
}

void MPCSingleIO::send(bool implicit_send)
{
    size_t thissize = dataqueue.size();
    // Ignore spurious calls to send(), except for resetting
    // message_lamport if this was an explicit send().
    if (thissize == 0) {
#ifdef SEND_LAMPORT_CLOCKS
        // If this was an explicit send(), reset the message_lamport so
        // that it gets updated at the next queue().
        if (!implicit_send) {
            message_lamport.reset();
        }
#endif
        return;
    }

#ifdef RECORD_IOTRACE
    iotrace.push_back(thissize);
#endif

    messagequeuelock.lock();
    // Move the current message to send into the message queue (this
    // moves a pointer to the data, not copying the data itself)
#ifdef SEND_LAMPORT_CLOCKS
    messagequeue.emplace(std::move(dataqueue),
        message_lamport.value());
    // If this was an explicit send(), reset the message_lamport so
    // that it gets updated at the next queue().
    if (!implicit_send) {
        message_lamport.reset();
    }
#else
    messagequeue.emplace(std::move(dataqueue));
#endif
    // If this is now the first thing in the message queue, launch
    // an async_write to write it
    if (messagequeue.size() == 1) {
        async_send_from_msgqueue();
    }
    messagequeuelock.unlock();
}

mpz_class MPCSingleIO::recv(lamport_t& lamport) {
    // Header lesen (Datenlänge + Lamport-Zeitstempel)
    char hdr[sizeof(uint32_t) + sizeof(lamport_t)];
    boost::asio::read(sock, boost::asio::buffer(hdr, sizeof(hdr)));

    // Header-Informationen extrahieren
    uint32_t datalen;
    lamport_t recv_lamport;
    std::memcpy(&datalen, hdr, sizeof(datalen));
    std::memcpy(&recv_lamport, hdr + sizeof(datalen), sizeof(lamport_t));

    lamport_t new_lamport = recv_lamport + 1;
    if (lamport < new_lamport) {
        lamport = new_lamport;
    }

    // Daten empfangen
    std::vector<char> buffer(datalen);
    boost::asio::read(sock, boost::asio::buffer(buffer.data(), datalen));

    // Deserialisieren
    return deserialize_from_binary(buffer.data(), datalen);
}


size_t MPCSingleIO::recv(void *data, size_t len, lamport_t &lamport)
{
#ifdef VERBOSE_COMMS
    struct timeval tv;
    gettimeofday(&tv, NULL);
    size_t orig_len = len;
    printf("%lu.%06lu: Recv %s.%d len=%lu lamp=%u ", tv.tv_sec,
        tv.tv_usec, dest.c_str(), thread_num, len, lamport);
#endif

#ifdef SEND_LAMPORT_CLOCKS
    char *cdata = (char *)data;
    size_t res = 0;

    while (len > 0) {
        while (recvdataremain == 0) {
            // Read a new header
            char hdr[sizeof(uint32_t) + sizeof(lamport_t)];
            uint32_t datalen;
            lamport_t recv_lamport;
            boost::asio::read(sock, boost::asio::buffer(hdr, sizeof(hdr)));
            memmove(&datalen, hdr, sizeof(datalen));
            memmove(&recv_lamport, hdr+sizeof(datalen), sizeof(lamport_t));
            lamport_t new_lamport = recv_lamport + 1;
            if (lamport < new_lamport) {
                lamport = new_lamport;
            }
            if (datalen > 0) {
                recvdata.resize(datalen, '\0');
                boost::asio::read(sock, boost::asio::buffer(recvdata));
                recvdataremain = datalen;
            }
        }
        size_t amttoread = len;
        if (amttoread > recvdataremain) {
            amttoread = recvdataremain;
        }
        memmove(cdata, recvdata.data()+recvdata.size()-recvdataremain,
            amttoread);
        cdata += amttoread;
        len -= amttoread;
        recvdataremain -= amttoread;
        res += amttoread;
    }
#else
    size_t res = boost::asio::read(sock, boost::asio::buffer(data, len));
#endif
#ifdef VERBOSE_COMMS
    gettimeofday(&tv, NULL);
    printf("nlamp=%u %lu.%06lu: ", lamport, tv.tv_sec, tv.tv_usec);
    for (size_t i=0;i<orig_len;++i) {
        printf("%02x", ((const unsigned char*)data)[i]);
    }
    printf("\n");
#endif
#ifdef RECORD_IOTRACE
    iotrace.push_back(-(ssize_t(res)));
#endif
    return res;
}

#ifdef RECORD_IOTRACE
void MPCSingleIO::dumptrace(std::ostream &os, const char *label)
{
    if (label) {
        os << label << " ";
    }
    os << "IO trace:";
    for (auto& s: iotrace) {
        os << " " << s;
    }
    os << "\n";
}
#endif

void MPCIO::reset_stats()
{
    msgs_sent.clear();
    msg_bytes_sent.clear();
    aes_ops.clear();
    for (size_t i=0; i<num_threads; ++i) {
        msgs_sent.push_back(0);
        msg_bytes_sent.push_back(0);
        aes_ops.push_back(0);
    }
    steady_start = boost::chrono::steady_clock::now();
    cpu_start = boost::chrono::process_cpu_clock::now();
}

// Report the memory usage

void MPCIO::dump_memusage(std::ostream &os)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    os << "Mem: " << ru.ru_maxrss << " KiB\n";
}

void MPCIO::dump_stats(std::ostream &os)
{
    size_t tot_msgs_sent = 0;
    size_t tot_msg_bytes_sent = 0;
    size_t tot_aes_ops = 0;
    for (auto& n : msgs_sent) {
        tot_msgs_sent += n;
    }
    for (auto& n : msg_bytes_sent) {
        tot_msg_bytes_sent += n;
    }
    for (auto& n : aes_ops) {
        tot_aes_ops += n;
    }
    auto steady_elapsed =
        boost::chrono::steady_clock::now() - steady_start;
    auto cpu_elapsed =
        boost::chrono::process_cpu_clock::now() - cpu_start;

    os << tot_msgs_sent << " messages sent\n";
    os << tot_msg_bytes_sent << " message bytes sent\n";
    os << lamport << " Lamport clock (latencies)\n";
    os << tot_aes_ops << " local AES operations\n";
    os << boost::chrono::duration_cast
        <boost::chrono::milliseconds>(steady_elapsed) <<
        " wall clock time\n";
    os << cpu_elapsed << " {real;user;system}\n";
    dump_memusage(os);
}

// TVA is a tuple of vectors of arrays of PreCompStorage
template <nbits_t WIDTH, typename TVA>
static void rdpfstorage_init(TVA &storage, unsigned player,
    ProcessingMode mode, unsigned num_threads, bool incremental)
{
    auto &VA = std::get<WIDTH-1>(storage);
    VA.resize(num_threads);
    char prefix[12];
    strcpy(prefix, incremental ? "irdpf" : "rdpf");
    if (WIDTH > 1) {
        sprintf(prefix+strlen(prefix), "%d_", WIDTH);
    }
    for (unsigned i=0; i<num_threads; ++i) {
        for (unsigned depth=1; depth<=ADDRESS_MAX_BITS; ++depth) {
            VA[i][depth-1].init(player, mode, prefix, i, depth, WIDTH);
        }
    }
}

// TVA is a tuple of vectors of arrays of PreCompStorage
template <nbits_t WIDTH, typename TVA>
static void rdpfstorage_dumpstats(std::ostream &os, TVA &storage,
    size_t thread_num, bool incremental)
{
    auto &VA = std::get<WIDTH-1>(storage);
    for (nbits_t depth=1; depth<=ADDRESS_MAX_BITS; ++depth) {
        size_t cnt = VA[thread_num][depth-1].get_stats();
        if (cnt > 0) {
            os << (incremental ? " i" : " r") << int(depth);
            if (WIDTH > 1) {
                os << "." << int(WIDTH);
            }
            os << ":" << cnt;
        }
    }
}

// TVA is a tuple of vectors of arrays of PreCompStorage
template <nbits_t WIDTH, typename TVA>
static void rdpfstorage_resetstats(TVA &storage, size_t thread_num)
{
    auto &VA = std::get<WIDTH-1>(storage);
    for (nbits_t depth=1; depth<=ADDRESS_MAX_BITS; ++depth) {
        VA[thread_num][depth-1].reset_stats();
    }
}

MPCPeerIO::MPCPeerIO(unsigned player, ProcessingMode mode,
        std::deque<tcp::socket> &peersocks,
        std::deque<tcp::socket> &serversocks) :
    MPCIO(player, mode, peersocks.size())
{
    unsigned num_threads = unsigned(peersocks.size());
    for (unsigned i=0; i<num_threads; ++i) {
        multtriples.emplace_back(player, mode, "mults", i);
    }
    for (unsigned i=0; i<num_threads; ++i) {
        halftriples.emplace_back(player, mode, "halves", i);
    }
    for (unsigned i=0; i<num_threads; ++i) {
        andtriples.emplace_back(player, mode, "ands", i);
    }
    for (unsigned i=0; i<num_threads; ++i) {
        valselecttriples.emplace_back(player, mode, "selects", i);
    }
    rdpfstorage_init<1>(rdpftriples, player, mode, num_threads, false);
    rdpfstorage_init<2>(rdpftriples, player, mode, num_threads, false);
    rdpfstorage_init<3>(rdpftriples, player, mode, num_threads, false);
    rdpfstorage_init<4>(rdpftriples, player, mode, num_threads, false);
    rdpfstorage_init<5>(rdpftriples, player, mode, num_threads, false);
    rdpfstorage_init<1>(irdpftriples, player, mode, num_threads, true);
    rdpfstorage_init<2>(irdpftriples, player, mode, num_threads, true);
    rdpfstorage_init<3>(irdpftriples, player, mode, num_threads, true);
    rdpfstorage_init<4>(irdpftriples, player, mode, num_threads, true);
    rdpfstorage_init<5>(irdpftriples, player, mode, num_threads, true);
    for (unsigned i=0; i<num_threads; ++i) {
        // cdpfs.emplace_back(player, mode, "cdpf", i);
    }
    for (unsigned i=0; i<num_threads; ++i) {
        peerios.emplace_back(std::move(peersocks[i]), "peer", i);
    }
    for (unsigned i=0; i<num_threads; ++i) {
        serverios.emplace_back(std::move(serversocks[i]), "srv", i);
    }
}

void MPCPeerIO::dump_precomp_stats(std::ostream &os)
{
    for (size_t i=0; i<multtriples.size(); ++i) {
        size_t cnt;
        if (i > 0) {
            os << " ";
        }
        os << "T" << i;
        cnt = multtriples[i].get_stats();
        if (cnt > 0) {
            os << " m:" << cnt;
        }
        cnt = halftriples[i].get_stats();
        if (cnt > 0) {
            os << " h:" << cnt;
        }
        cnt = andtriples[i].get_stats();
        if (cnt > 0) {
            os << " a:" << cnt;
        }
        cnt = valselecttriples[i].get_stats();
        if (cnt > 0) {
            os << " s:" << cnt;
        }
        rdpfstorage_dumpstats<1>(os, rdpftriples, i, false);
        rdpfstorage_dumpstats<2>(os, rdpftriples, i, false);
        rdpfstorage_dumpstats<3>(os, rdpftriples, i, false);
        rdpfstorage_dumpstats<4>(os, rdpftriples, i, false);
        rdpfstorage_dumpstats<5>(os, rdpftriples, i, false);
        rdpfstorage_dumpstats<1>(os, irdpftriples, i, true);
        rdpfstorage_dumpstats<2>(os, irdpftriples, i, true);
        rdpfstorage_dumpstats<3>(os, irdpftriples, i, true);
        rdpfstorage_dumpstats<4>(os, irdpftriples, i, true);
        rdpfstorage_dumpstats<5>(os, irdpftriples, i, true);
        // cnt = cdpfs[i].get_stats();
        if (cnt > 0) {
            // os << " c:" << cnt;
        }
    }
    os << "\n";
}

void MPCPeerIO::reset_precomp_stats()
{
    for (size_t i=0; i<multtriples.size(); ++i) {
        multtriples[i].reset_stats();
        halftriples[i].reset_stats();
        andtriples[i].reset_stats();
        valselecttriples[i].reset_stats();
        rdpfstorage_resetstats<1>(rdpftriples, i);
        rdpfstorage_resetstats<2>(rdpftriples, i);
        rdpfstorage_resetstats<3>(rdpftriples, i);
        rdpfstorage_resetstats<4>(rdpftriples, i);
        rdpfstorage_resetstats<5>(rdpftriples, i);
        rdpfstorage_resetstats<1>(irdpftriples, i);
        rdpfstorage_resetstats<2>(irdpftriples, i);
        rdpfstorage_resetstats<3>(irdpftriples, i);
        rdpfstorage_resetstats<4>(irdpftriples, i);
        rdpfstorage_resetstats<5>(irdpftriples, i);
    }
}

void MPCPeerIO::dump_stats(std::ostream &os)
{
    MPCIO::dump_stats(os);
    os << "Precomputed values used: ";
    dump_precomp_stats(os);
}

MPCServerIO::MPCServerIO(ProcessingMode mode,
        std::deque<tcp::socket> &p0socks,
        std::deque<tcp::socket> &p1socks) :
    MPCIO(2, mode, p0socks.size())
{
    rdpfstorage_init<1>(rdpfpairs, player, mode, num_threads, false);
    rdpfstorage_init<2>(rdpfpairs, player, mode, num_threads, false);
    rdpfstorage_init<3>(rdpfpairs, player, mode, num_threads, false);
    rdpfstorage_init<4>(rdpfpairs, player, mode, num_threads, false);
    rdpfstorage_init<5>(rdpfpairs, player, mode, num_threads, false);
    rdpfstorage_init<1>(irdpfpairs, player, mode, num_threads, true);
    rdpfstorage_init<2>(irdpfpairs, player, mode, num_threads, true);
    rdpfstorage_init<3>(irdpfpairs, player, mode, num_threads, true);
    rdpfstorage_init<4>(irdpfpairs, player, mode, num_threads, true);
    rdpfstorage_init<5>(irdpfpairs, player, mode, num_threads, true);
    for (unsigned i=0; i<num_threads; ++i) {
        p0ios.emplace_back(std::move(p0socks[i]), "p0", i);
    }
    for (unsigned i=0; i<num_threads; ++i) {
        p1ios.emplace_back(std::move(p1socks[i]), "p1", i);
    }
}

void MPCServerIO::dump_precomp_stats(std::ostream &os)
{
    for (size_t i=0; i<std::get<0>(rdpfpairs).size(); ++i) {
        if (i > 0) {
            os << " ";
        }
        os << "T" << i;
        rdpfstorage_dumpstats<1>(os, rdpfpairs, i, false);
        rdpfstorage_dumpstats<2>(os, rdpfpairs, i, false);
        rdpfstorage_dumpstats<3>(os, rdpfpairs, i, false);
        rdpfstorage_dumpstats<4>(os, rdpfpairs, i, false);
        rdpfstorage_dumpstats<5>(os, rdpfpairs, i, false);
        rdpfstorage_dumpstats<1>(os, irdpfpairs, i, true);
        rdpfstorage_dumpstats<2>(os, irdpfpairs, i, true);
        rdpfstorage_dumpstats<3>(os, irdpfpairs, i, true);
        rdpfstorage_dumpstats<4>(os, irdpfpairs, i, true);
        rdpfstorage_dumpstats<5>(os, irdpfpairs, i, true);
    }
    os << "\n";
}

void MPCServerIO::reset_precomp_stats()
{
    for (size_t i=0; i<std::get<0>(rdpfpairs).size(); ++i) {
        rdpfstorage_resetstats<1>(rdpfpairs, i);
        rdpfstorage_resetstats<2>(rdpfpairs, i);
        rdpfstorage_resetstats<3>(rdpfpairs, i);
        rdpfstorage_resetstats<4>(rdpfpairs, i);
        rdpfstorage_resetstats<5>(rdpfpairs, i);
        rdpfstorage_resetstats<1>(irdpfpairs, i);
        rdpfstorage_resetstats<2>(irdpfpairs, i);
        rdpfstorage_resetstats<3>(irdpfpairs, i);
        rdpfstorage_resetstats<4>(irdpfpairs, i);
        rdpfstorage_resetstats<5>(irdpfpairs, i);
    }
}

void MPCServerIO::dump_stats(std::ostream &os)
{
    MPCIO::dump_stats(os);
    os << "Precomputed values used: ";
    dump_precomp_stats(os);
}

MPCTIO::MPCTIO(MPCIO &mpcio, int thread_num, int num_threads) :
        thread_num(thread_num), local_cpu_nthreads(num_threads),
        communication_nthreads(num_threads),
        thread_lamport(mpcio.lamport), mpcio(mpcio),
#ifdef VERBOSE_COMMS
        round_num(0),
#endif
        last_andtriple_bits_remaining(0),
        remaining_nodesselecttriples(0)
{
    if (mpcio.player < 2) {
        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);
        peer_iostream.emplace(mpcpio.peerios[thread_num],
            thread_lamport, mpcpio.msgs_sent[thread_num],
            mpcpio.msg_bytes_sent[thread_num]);
        server_iostream.emplace(mpcpio.serverios[thread_num],
            thread_lamport, mpcpio.msgs_sent[thread_num],
            mpcpio.msg_bytes_sent[thread_num]);
    } else {
        MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);
        p0_iostream.emplace(mpcsrvio.p0ios[thread_num],
            thread_lamport, mpcsrvio.msgs_sent[thread_num],
            mpcsrvio.msg_bytes_sent[thread_num]);
        p1_iostream.emplace(mpcsrvio.p1ios[thread_num],
            thread_lamport, mpcsrvio.msgs_sent[thread_num],
            mpcsrvio.msg_bytes_sent[thread_num]);
    }
}

// Sync our per-thread lamport clock with the master one in the
// mpcio.  You only need to call this explicitly if your MPCTIO
// outlives your thread (in which case call it after the join), or
// if your threads do interthread communication amongst themselves
// (in which case call it in the sending thread before the send, and
// call it in the receiving thread after the receive).
void MPCTIO::sync_lamport()
{
    // Update the mpcio Lamport time to be max of the thread Lamport
    // time and what we thought it was before.  We use this
    // compare_exchange construction in order to atomically
    // do the comparison, computation, and replacement
    lamport_t old_lamport = mpcio.lamport;
    lamport_t new_lamport = thread_lamport;
    do {
        if (new_lamport < old_lamport) {
            new_lamport = old_lamport;
        }
    // The next line atomically checks if lamport still has
    // the value old_lamport; if so, it changes its value to
    // new_lamport and returns true (ending the loop).  If
    // not, it sets old_lamport to the current value of
    // lamport, and returns false (continuing the loop so
    // that new_lamport can be recomputed based on this new
    // value).
    } while (!mpcio.lamport.compare_exchange_weak(
        old_lamport, new_lamport));
    thread_lamport = new_lamport;
}

// Only call this if you can be sure that there are no outstanding
// messages in flight, you can call it on all existing MPCTIOs, and
// you really want to reset the Lamport clock in the midding of a
// run.
void MPCTIO::reset_lamport()
{
    // Reset both our own Lamport clock and the parent MPCIO's
    thread_lamport = 0;
    mpcio.lamport = 0;
}

// Queue up data to the peer or to the server

void MPCTIO::queue_peer(const void *data, size_t len)
{
    if (mpcio.player < 2) {
        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);

        std::vector<char> data_copy(len);
        std::memcpy(data_copy.data(), data, len);

        size_t newmsg = mpcpio.peerios[thread_num].queue(data_copy.data(), len, thread_lamport);
        mpcpio.msgs_sent[thread_num] += newmsg;
        mpcpio.msg_bytes_sent[thread_num] += len;
    }
}

void MPCTIO::queue_peer(const HalfTriple& data) {
    if (mpcio.player < 2) {
        size_t len = 0;
        char* to_send = serialize_halftriple(data, len);

        MPCPeerIO& mpcpio = static_cast<MPCPeerIO&>(mpcio);

        size_t msg = mpcpio.peerios[thread_num].queue(reinterpret_cast<const char*>(&len), sizeof(len), thread_lamport);
        mpcpio.msgs_sent[thread_num] += msg;

        std::vector<char> data_copy(len);
        std::memcpy(data_copy.data(), to_send, len);

        size_t newmsg = mpcpio.peerios[thread_num].queue(data_copy.data(), len, thread_lamport);
        mpcpio.msgs_sent[thread_num] += newmsg;
        mpcpio.msg_bytes_sent[thread_num] += len;

        delete[] to_send;
    }
}

void MPCTIO::queue_peer(const mpz_class &data) {
    if (mpcio.player < 2) {
        size_t len = 0;
        auto to_send = serialize_to_binary(data, len);

        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);

        size_t msg = mpcpio.peerios[thread_num].queue(reinterpret_cast<const char*>(&len), sizeof(len), thread_lamport);
        mpcpio.msgs_sent[thread_num] += msg;

        std::vector<char> data_copy(len);
        std::memcpy(data_copy.data(), to_send, len);

        size_t newmsg = mpcpio.peerios[thread_num].queue(data_copy.data(), len, thread_lamport);
        mpcpio.msgs_sent[thread_num] += newmsg;
        mpcpio.msg_bytes_sent[thread_num] += len;
        delete[] to_send;
    }
}

void MPCTIO::queue_server(const mpz_class &data) {
    if (mpcio.player < 2) {
        size_t len = 0;
        auto to_send = serialize_to_binary(data, len);

        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);

        size_t msg = mpcpio.serverios[thread_num].queue(reinterpret_cast<const char*>(&len), sizeof(len), thread_lamport);
        mpcpio.msgs_sent[thread_num] += msg;

        std::vector<char> data_copy(len);
        std::memcpy(data_copy.data(), to_send, len);

        size_t newmsg = mpcpio.serverios[thread_num].queue(data_copy.data(), len, thread_lamport);
        mpcpio.msgs_sent[thread_num] += newmsg;
        mpcpio.msg_bytes_sent[thread_num] += len;
        delete[] to_send;
    }
}


void MPCTIO::queue_server(const void *data, size_t len)
{
    if (mpcio.player < 2) {
        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);
        size_t newmsg = mpcpio.serverios[thread_num].queue(data, len, thread_lamport);
        mpcpio.msgs_sent[thread_num] += newmsg;
        mpcpio.msg_bytes_sent[thread_num] += len;
    }
}

// Receive data from the peer or to the server

size_t MPCTIO::recv_peer(HalfTriple& data) {
    if (mpcio.player < 2) {
        MPCPeerIO& mpcpio = static_cast<MPCPeerIO&>(mpcio);

        size_t len = 0;
        size_t res = mpcpio.peerios[thread_num].recv(&len, sizeof(len), thread_lamport);

        std::vector<char> buffer(len);
        res = mpcpio.peerios[thread_num].recv(buffer.data(), len, thread_lamport);

        if (res > 0) {
            data = deserialize_halftriple(buffer.data(), len);
        }
        return res;
    }

    return 0;
}

size_t MPCTIO::recv_peer(void *data, size_t len = VALUE_BITS) {
    if (mpcio.player < 2) {
        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);
        return mpcpio.peerios[thread_num].recv(data, len, thread_lamport);
    }
    return 0;
}

size_t MPCTIO::recv_peer(mpz_class &data) {
    if (mpcio.player < 2) {
        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);

        size_t len = 0;
        size_t res = mpcpio.peerios[thread_num].recv(&len, sizeof(len), thread_lamport);

        if (len > 200) {
            len = 1;
        }

        std::vector<char> buffer(len);
        res = mpcpio.peerios[thread_num].recv(buffer.data(), len, thread_lamport);

        if (res > 0) {
            data = deserialize_from_binary(buffer.data(), len);
        }
        return res;
    }

    return 0;
}

size_t MPCTIO::recv_server(std::tuple<mpz_class, mpz_class> &data) {
    if (mpcio.player < 2) {
        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);

        size_t len = 0;
        size_t res = mpcpio.serverios[thread_num].recv(&len, sizeof(len), thread_lamport);

        std::vector<char> buffer(len);
        res = mpcpio.serverios[thread_num].recv(buffer.data(), len, thread_lamport);

        if (res > 0) {
            data = deserialize_halftriple(buffer.data(), len);
        }
        return res;
    }

    return 0;
}


size_t MPCTIO::recv_server(mpz_class &data) {
    if (mpcio.player < 2) {
        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);

        size_t len = 0;
        size_t res = mpcpio.serverios[thread_num].recv(&len, sizeof(len), thread_lamport);

        std::vector<char> buffer(len);
        res = mpcpio.serverios[thread_num].recv(buffer.data(), len, thread_lamport);

        if (res > 0) {
            data = deserialize_from_binary(buffer.data(), len);
        }
        return res;
    }

    return 0;
}



size_t MPCTIO::recv_server(void *data, size_t len)
{
    if (mpcio.player < 2) {
        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);
        return mpcpio.serverios[thread_num].recv(data, len, thread_lamport);
    }
    return 0;
}

// Queue up data to p0 or p1
void MPCTIO::queue_p0(const HalfTriple& data) {
    if (mpcio.player == 2) {
        size_t len = 0;
        char* to_send = serialize_halftriple(data, len);

        MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);

        size_t msg = mpcsrvio.p0ios[thread_num].queue(reinterpret_cast<const char*>(&len), sizeof(len), thread_lamport);
        mpcsrvio.msgs_sent[thread_num] += msg;

        std::vector<char> data_copy(len);
        std::memcpy(data_copy.data(), to_send, len);

        size_t newmsg = mpcsrvio.p0ios[thread_num].queue(data_copy.data(), len, thread_lamport);
        mpcsrvio.msgs_sent[thread_num] += newmsg;
        mpcsrvio.msg_bytes_sent[thread_num] += len;

        delete[] to_send;
    }
}

void MPCTIO::queue_p0(const mpz_class &data) {
    if (mpcio.player == 2) {
        size_t len = 0;
        auto to_send = serialize_to_binary(data, len);

        MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);

        size_t msg = mpcsrvio.p0ios[thread_num].queue(reinterpret_cast<const char*>(&len), sizeof(len), thread_lamport);
        mpcsrvio.msgs_sent[thread_num] += msg;

        std::vector<char> data_copy(len);
        std::memcpy(data_copy.data(), to_send, len);

        size_t newmsg = mpcsrvio.p0ios[thread_num].queue(data_copy.data(), len, thread_lamport);
        mpcsrvio.msgs_sent[thread_num] += newmsg;
        mpcsrvio.msg_bytes_sent[thread_num] += len;
    }
}


void MPCTIO::queue_p0(const void *data, size_t len)
{
    if (mpcio.player == 2) {
        MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);
        size_t newmsg = mpcsrvio.p0ios[thread_num].queue(data, len, thread_lamport);
        mpcsrvio.msgs_sent[thread_num] += newmsg;
        mpcsrvio.msg_bytes_sent[thread_num] += len;
    }
}

void MPCTIO::queue_p1(const HalfTriple& data) {
    if (mpcio.player == 2) {
        size_t len = 0;
        char* to_send = serialize_halftriple(data, len);

        MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);

        size_t msg = mpcsrvio.p1ios[thread_num].queue(reinterpret_cast<const char*>(&len), sizeof(len), thread_lamport);
        mpcsrvio.msgs_sent[thread_num] += msg;

        std::vector<char> data_copy(len);
        std::memcpy(data_copy.data(), to_send, len);

        size_t newmsg = mpcsrvio.p1ios[thread_num].queue(data_copy.data(), len, thread_lamport);
        mpcsrvio.msgs_sent[thread_num] += newmsg;
        mpcsrvio.msg_bytes_sent[thread_num] += len;

        delete[] to_send;
    }
}

void MPCTIO::queue_p1(const mpz_class &data) {
    if (mpcio.player == 2) {
        size_t len = 0;
        auto to_send = serialize_to_binary(data, len);

        MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);

        size_t msg = mpcsrvio.p1ios[thread_num].queue(reinterpret_cast<const char*>(&len), sizeof(len), thread_lamport);
        mpcsrvio.msgs_sent[thread_num] += msg;

        std::vector<char> data_copy(len);
        std::memcpy(data_copy.data(), to_send, len);

        size_t newmsg = mpcsrvio.p1ios[thread_num].queue(data_copy.data(), len, thread_lamport);
        mpcsrvio.msgs_sent[thread_num] += newmsg;
        mpcsrvio.msg_bytes_sent[thread_num] += len;
    }
}


void MPCTIO::queue_p1(const void *data, size_t len)
{
    if (mpcio.player == 2) {
        MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);
        size_t newmsg = mpcsrvio.p1ios[thread_num].queue(data, len, thread_lamport);
        mpcsrvio.msgs_sent[thread_num] += newmsg;
        mpcsrvio.msg_bytes_sent[thread_num] += len;
    }
}

// Receive data from p0 or p1

size_t MPCTIO::recv_p0(std::tuple<mpz_class, mpz_class> &data) {
    if (mpcio.player == 2) {
        MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);

        size_t len = 0;
        size_t res = mpcsrvio.p0ios[thread_num].recv(&len, sizeof(len), thread_lamport);

        std::vector<char> buffer(len);
        res = mpcsrvio.p0ios[thread_num].recv(buffer.data(), len, thread_lamport);

        if (res > 0) {
            data = deserialize_halftriple(buffer.data(), len);
        }
        return res;
    }

    return 0;
}


size_t MPCTIO::recv_p0(mpz_class &data) {
    if (mpcio.player == 2) {
        MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);

        size_t len = 0;
        size_t res = mpcsrvio.p0ios[thread_num].recv(&len, sizeof(len), thread_lamport);

        std::vector<char> buffer(len);
        res = mpcsrvio.p0ios[thread_num].recv(buffer.data(), len, thread_lamport);

        if (res > 0) {
            data = deserialize_from_binary(buffer.data(), len);
        }
        return res;
    }

    return 0;
}


size_t MPCTIO::recv_p0(void *data, size_t len)
{
    if (mpcio.player == 2) {
        MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);
        return mpcsrvio.p0ios[thread_num].recv(data, len, thread_lamport);
    }
    return 0;
}

size_t MPCTIO::recv_p1(std::tuple<mpz_class, mpz_class> &data) {
    if (mpcio.player == 2) {
        MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);

        size_t len = 0;
        size_t res = mpcsrvio.p1ios[thread_num].recv(&len, sizeof(len), thread_lamport);

        std::vector<char> buffer(len);
        res = mpcsrvio.p1ios[thread_num].recv(buffer.data(), len, thread_lamport);

        if (res > 0) {
            data = deserialize_halftriple(buffer.data(), len);
        }
        return res;
    }

    return 0;
}


size_t MPCTIO::recv_p1(mpz_class &data) {
    if (mpcio.player == 2) {
        MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);

        // Zuerst die Größe der Nachricht empfangen
        size_t len = 0;
        size_t res = mpcsrvio.p1ios[thread_num].recv(&len, sizeof(len), thread_lamport);

        std::vector<char> buffer(len);
        res = mpcsrvio.p1ios[thread_num].recv(buffer.data(), len, thread_lamport);

        if (res > 0) {
            data = deserialize_from_binary(buffer.data(), len);
        }
        return res;
    }

    return 0;
}

size_t MPCTIO::recv_p1(void *data, size_t len)
{
    if (mpcio.player == 2) {
        MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);
        return mpcsrvio.p1ios[thread_num].recv(data, len, thread_lamport);
    }
    return 0;
}

// Send all queued data for this thread

void MPCTIO::send()
{
#ifdef VERBOSE_COMMS
    struct timeval tv;
    gettimeofday(&tv, NULL);
    printf("%lu.%06lu: Thread %u sending round %lu\n", tv.tv_sec,
        tv.tv_usec, thread_num, ++round_num);
#endif
    if (mpcio.player < 2) {
        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);
        mpcpio.peerios[thread_num].send();
        mpcpio.serverios[thread_num].send();
    } else {
        MPCServerIO &mpcsrvio = static_cast<MPCServerIO&>(mpcio);
        mpcsrvio.p0ios[thread_num].send();
        mpcsrvio.p1ios[thread_num].send();
    }
}

// Functions to get precomputed values.  If we're in the online
// phase, get them from PreCompStorage.  If we're in the
// preprocessing or online-only phase, read them from the server.
MultTriple MPCTIO::multtriple(yield_t &yield)
{
    MultTriple val;
    if (mpcio.player < 2) {
        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);
        if (mpcpio.mode != MODE_ONLINE) {
            yield();
            recv_server(&val, sizeof(val));
            mpcpio.multtriples[thread_num].inc();
        } else {
            mpcpio.multtriples[thread_num].get(val);
        }
    } else if (mpcio.mode != MODE_ONLINE) {
        // Create multiplication triples (X0,Y0,Z0),(X1,Y1,Z1) such that
        // (X0*Y1 + Y0*X1) = (Z0+Z1)
        value_t X0, Y0, Z0, X1, Y1, Z1;
        // arc4random_buf(&X0, sizeof(X0));
        random_mpz(X0);
        // arc4random_buf(&Y0, sizeof(Y0));
        random_mpz(Y0);
        // arc4random_buf(&Z0, sizeof(Z0));
        random_mpz(Z0);
        // arc4random_buf(&X1, sizeof(X1));
        random_mpz(X1);
        // arc4random_buf(&Y1, sizeof(Y1));
        random_mpz(Y1);
        Z1 = X0 * Y1 + X1 * Y0 - Z0;
        MultTriple T0, T1;
        T0 = std::make_tuple(X0, Y0, Z0);
        T1 = std::make_tuple(X1, Y1, Z1);
        queue_p0(&T0, sizeof(T0));
        queue_p1(&T1, sizeof(T1));
        yield();
    }
    return val;
}

// When halftriple() is used internally to another preprocessing
// operation, don't tally it, so that it doesn't appear sepearately in
// the stats from the preprocessing operation that invoked it
HalfTriple MPCTIO::halftriple(yield_t &yield, bool tally)
{
    yield();
    HalfTriple val;
    if (mpcio.player < 2) {
        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);
        if (mpcpio.mode != MODE_ONLINE) {
            value_t X0, Z0, Y1, Z1;
            // arc4random_buf(&X0, sizeof(X0));
            random_mpz(X0);
            // arc4random_buf(&Z0, sizeof(Z0));
            random_mpz(Z0);
            // arc4random_buf(&Y1, sizeof(Y1));
            random_mpz(Y1);
            Z1 = X0 * Y1 - Z0;

            HalfTriple H0, H1;
            H0 = std::make_tuple(X0, Z0);
            H1 = std::make_tuple(Y1, Z1);
            val = player() == 1 ? H0 : H1;  // Zeit für Betrug
            //
            // HalfTriple X0;
            // recv_server(X0);  //TODO hier gehts weiter lieber Felix
            //
            // yield();

            if (tally) {
                mpcpio.halftriples[thread_num].inc();
            }
        } else {
            mpcpio.halftriples[thread_num].get(val);
        }
    } else if (mpcio.mode != MODE_ONLINE) {
        // Create half-triples (X0,Z0),(Y1,Z1) such that
        // X0*Y1 = Z0 + Z1
        value_t X0, Z0, Y1, Z1;
        // arc4random_buf(&X0, sizeof(X0));
        random_mpz(X0);
        // arc4random_buf(&Z0, sizeof(Z0));
        random_mpz(Z0);
        // arc4random_buf(&Y1, sizeof(Y1));
        random_mpz(Y1);
        Z1 = X0 * Y1 - Z0;

        HalfTriple H0, H1;
        H0 = std::make_tuple(X0, Z0);
        H1 = std::make_tuple(Y1, Z1);
        // queue_p0(H0);
        // queue_p1(H1);
        // yield();

        // queue_p0(&H0, sizeof(H0));
        // queue_p1(&H1, sizeof(H1));

        value_t val0, val1;
        val0 = 0;
        val1 = 0;
        val = std::make_tuple(val0, val1);

    }
    return val;
}

MultTriple MPCTIO::andtriple(yield_t &yield)
{
    AndTriple val;
    if (mpcio.player < 2) {
        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);
        if (mpcpio.mode != MODE_ONLINE) {
            yield();
            recv_server(&val, sizeof(val));
            mpcpio.andtriples[thread_num].inc();
        } else {
            mpcpio.andtriples[thread_num].get(val);
        }
    } else if (mpcio.mode != MODE_ONLINE) {
        // Create AND triples (X0,Y0,Z0),(X1,Y1,Z1) such that
        // (X0&Y1 ^ Y0&X1) = (Z0^Z1)
        value_t X0, Y0, Z0, X1, Y1, Z1;
        // arc4random_buf(&X0, sizeof(X0));
        random_mpz(X0);
        // arc4random_buf(&Y0, sizeof(Y0));
        random_mpz(Y0);
        // arc4random_buf(&Z0, sizeof(Z0));
        random_mpz(Z0);
        // arc4random_buf(&X1, sizeof(X1));
        random_mpz(X1);
        // arc4random_buf(&Y1, sizeof(Y1));
        random_mpz(Y1);
        Z1 = (X0 & Y1) ^ (X1 & Y0) ^ Z0;
        AndTriple T0, T1;
        // T0 = std::make_tuple(X0, Y0, Z0);
        // T1 = std::make_tuple(X1, Y1, Z1);
        queue_p0(X0);
        queue_p0(Y0);
        queue_p0(Z0);

        queue_p0(X1);
        queue_p0(Y1);
        queue_p0(Z1);

        // queue_p0(T0);
        // queue_p1(&T1, sizeof(T1));
        yield();
    }
    return val;
}

void MPCTIO::request_nodeselecttriples(yield_t &yield, size_t num)
{
    if (mpcio.player < 2) {
        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);
        if (mpcpio.mode != MODE_ONLINE) {
            yield();
            for (size_t i=0; i<num; ++i) {
                bit_t X;
                recv_server(&X, sizeof(X));
                // bit_t X = Xbyte & 1;

                DPFnode z, y;
                recv_server(y);
                recv_server(z);

                // Direktes Emplace ohne temporäres Objekt
                queued_nodeselecttriples.emplace_back(X, std::move(y), std::move(z));
            }
            remaining_nodesselecttriples += num;
        } else {
            std::cerr << "Attempted to read SelectTriple<DPFnode> in online phase\n";
        }
    } else if (mpcio.mode != MODE_ONLINE) {
        for (size_t i=0; i<num; ++i) {
            // Create triples (X0,Y0,Z0),(X1,Y1,Z1) such that
            // (X0*Y1 ^ Y0*X1) = (Z0^Z1)
            bit_t X0, X1;
            DPFnode Y0, Z0, Y1, Z1;
            DPFnode tmp0, tmpAnd0;

            // X0 = arc4random() & 1;
            random_mpz(tmp0, 2*VALUE_BITS);
            mpz_and(tmpAnd0.get_mpz_t(), tmp0.get_mpz_t(), DPFnode(1).get_mpz_t());
            X0 = (mpz_cmp_ui(tmpAnd0.get_mpz_t(), 0) != 0) ? true : false;

            // arc4random_buf(&Y0, sizeof(Y0));
            random_mpz(Y0, 2*VALUE_BITS);

            // arc4random_buf(&Z0, sizeof(Z0));
            random_mpz(Z0, 2*VALUE_BITS);

            DPFnode tmp1, tmpAnd1;
            // X1 = arc4random() & 1;
            random_mpz(tmp1, 2*VALUE_BITS);
            mpz_and(tmpAnd1.get_mpz_t(), tmp1.get_mpz_t(), DPFnode(1).get_mpz_t());
            X1 = (mpz_cmp_ui(tmpAnd1.get_mpz_t(), 0) != 0);

            // arc4random_buf(&Y1, sizeof(Y1));
            random_mpz(Y1, 2*VALUE_BITS);

            // mpz_init_set(X0ext.get_mpz_t(), 0);
            // mpz_init_set(X1ext.get_mpz_t(), 0);
            // Sign-extend X0 and X1 (so that 0 -> 0000...0 and
            // 1 -> 1111...1)
            DPFnode X0ext;
            X0ext = X0 == 0 ? 1 : 0;
            DPFnode X1ext;
            X1ext = X1 == 0 ? 1 : 0;
            Z1 = ((X0ext ^ Y1) ^ (X1ext ^ Y0)) ^ Z0;


            queue_p0(&X0, sizeof(X0));
            queue_p0(Y0);
            queue_p0(Z0);
            queue_p1(&X1, sizeof(X1));
            queue_p1(Y1);
            queue_p1(Z1);
        }
        yield();
        remaining_nodesselecttriples += num;
    }
}

SelectTriple<DPFnode> MPCTIO::nodeselecttriple(yield_t &yield)
{
    //TODO es wird immer das selbe selectriple verwendet weil pop nicht mehr aufgerufen wird
    SelectTriple<DPFnode> val;
    if (remaining_nodesselecttriples == 0) {
        request_nodeselecttriples(yield, 1);
    }
    if (mpcio.player < 2) {
        auto &mpcpio = static_cast<MPCPeerIO&>(mpcio);
        if (mpcpio.mode != MODE_ONLINE) {

            // val = queued_nodeselecttriples.front();
            std::cout << "size of dequeue: " << queued_nodeselecttriples.size() << std::endl;
            std::cout << "size of dequeue 2: " << remaining_nodesselecttriples<< std::endl;
            // queued_nodeselecttriples.pop_front();
            // --remaining_nodesselecttriples;
        } else {
            std::cerr << "Attempted to read SelectTriple<DPFnode> in online phase\n";
        }
    } else if (mpcio.mode != MODE_ONLINE) {
        --remaining_nodesselecttriples;
    }
    return val;
}

SelectTriple<value_t> MPCTIO::valselecttriple(yield_t &yield)
{
    SelectTriple<value_t> val;
    if (mpcio.player < 2) {
        MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);
        if (mpcpio.mode != MODE_ONLINE) {
            uint8_t Xbyte;
            yield();
            recv_server(&Xbyte, sizeof(Xbyte));
            val.X = Xbyte & 1;
            recv_server(&val.Y, sizeof(val.Y));
            recv_server(&val.Z, sizeof(val.Z));
            mpcpio.valselecttriples[thread_num].inc();
        } else {
            mpcpio.valselecttriples[thread_num].get(val);
        }
    } else if (mpcio.mode != MODE_ONLINE) {
        // Create triples (X0,Y0,Z0),(X1,Y1,Z1) such that
        // (X0*Y1 ^ Y0*X1) = (Z0^Z1)
        bit_t X0, X1;
        value_t Y0, Z0, Y1, Z1;
        value_t tmp0, tmp1;
        // arc4random_buf(&Y0, sizeof(Y0));
        random_mpz(Y0, sizeof(Y0));
        // arc4random_buf(&Z0, sizeof(Z0));
        random_mpz(Z0, sizeof(Z0));
        // X0 = arc4random() & 1;
        random_mpz(tmp0, sizeof(tmp0));
        X0 = (mpz_cmp_ui(tmp0.get_mpz_t(), 0) != 0) ? 1 : 0;

        // X1 = arc4random() & 1;
        random_mpz(tmp1, sizeof(tmp1));
        X1 = (mpz_cmp_ui(tmp1.get_mpz_t(), 0) != 0) ? 1 : 0;
        // arc4random_buf(&Y1, sizeof(Y1));
        random_mpz(Y1, sizeof(Y1));

        value_t X0ext, X1ext;
        // Sign-extend X0 and X1 (so that 0 -> 0000...0 and
        // 1 -> 1111...1)
        X0ext = -value_t(X0);
        X1ext = -value_t(X1);
        Z1 = ((X0ext & Y1) ^ (X1ext & Y0)) ^ Z0;
        queue_p0(&X0, sizeof(X0));
        queue_p0(&Y0, sizeof(Y0));
        queue_p0(&Z0, sizeof(Z0));
        queue_p1(&X1, sizeof(X1));
        queue_p1(&Y1, sizeof(Y1));
        queue_p1(&Z1, sizeof(Z1));
        yield();
    }
    return val;
}

SelectTriple<bit_t> MPCTIO::bitselecttriple(yield_t &yield)
{
    // Do we need to fetch a new AND triple?
    if (last_andtriple_bits_remaining == 0) {
        last_andtriple = andtriple(yield);
        last_andtriple_bits_remaining = 8*sizeof(value_t);
    }
    --last_andtriple_bits_remaining;
    value_t mask = value_t(1) << last_andtriple_bits_remaining;
    SelectTriple<bit_t> val;
    val.X = !!value_t(std::get<0>(last_andtriple) & mask);
    val.Y = !!value_t(std::get<1>(last_andtriple) & mask);
    val.Z = !!value_t(std::get<2>(last_andtriple) & mask);
    return val;
}

// CDPF MPCTIO::cdpf(yield_t &yield)
// {
//     CDPF val;
//     if (mpcio.player < 2) {
//         MPCPeerIO &mpcpio = static_cast<MPCPeerIO&>(mpcio);
//         if (mpcpio.mode != MODE_ONLINE) {
//             yield();
//             iostream_server() >> val;
//             mpcpio.cdpfs[thread_num].inc();
//         } else {
//             mpcpio.cdpfs[thread_num].get(val);
//         }
//     } else if (mpcio.mode != MODE_ONLINE) {
//         auto [ cdpf0, cdpf1 ] = CDPF::generate(aes_ops());
//         iostream_p0() << cdpf0;
//         iostream_p1() << cdpf1;
//         yield();
//     }
//     return val;
// }

// The port number for the P1 -> P0 connection
static const unsigned short port_p1_p0 = 2115;

// The port number for the P2 -> P0 connection
static const unsigned short port_p2_p0 = 2116;

// The port number for the P2 -> P1 connection
static const unsigned short port_p2_p1 = 2117;

void mpcio_setup_computational(unsigned player,
    boost::asio::io_context &io_context,
    const char *p0addr,  // can be NULL when player=0
    int num_threads,
    std::deque<tcp::socket> &peersocks,
    std::deque<tcp::socket> &serversocks)
{
    if (player == 0) {
        // Listen for connections from P1 and from P2
        tcp::acceptor acceptor_p1(io_context,
            tcp::endpoint(tcp::v4(), port_p1_p0));
        tcp::acceptor acceptor_p2(io_context,
            tcp::endpoint(tcp::v4(), port_p2_p0));

        peersocks.clear();
        serversocks.clear();
        for (int i=0;i<num_threads;++i) {
            peersocks.emplace_back(io_context);
            serversocks.emplace_back(io_context);
        }
        for (int i=0;i<num_threads;++i) {
            tcp::socket peersock = acceptor_p1.accept();
            // Read 2 bytes from the socket, which will be the thread
            // number
            unsigned short thread_num;
            boost::asio::read(peersock,
                boost::asio::buffer(&thread_num, sizeof(thread_num)));
            if (thread_num >= num_threads) {
                std::cerr << "Received bad thread number from peer\n";
            } else {
                peersocks[thread_num] = std::move(peersock);
            }
        }
        for (int i=0;i<num_threads;++i) {
            tcp::socket serversock = acceptor_p2.accept();
            // Read 2 bytes from the socket, which will be the thread
            // number
            unsigned short thread_num;
            boost::asio::read(serversock,
                boost::asio::buffer(&thread_num, sizeof(thread_num)));
            if (thread_num >= num_threads) {
                std::cerr << "Received bad thread number from server\n";
            } else {
                serversocks[thread_num] = std::move(serversock);
            }
        }
    } else if (player == 1) {
        // Listen for connections from P2, make num_threads connections to P0
        tcp::acceptor acceptor_p2(io_context,
            tcp::endpoint(tcp::v4(), port_p2_p1));

        tcp::resolver resolver(io_context);
        boost::system::error_code err;
        peersocks.clear();
        serversocks.clear();
        for (int i=0;i<num_threads;++i) {
            serversocks.emplace_back(io_context);
        }
        for (unsigned short thread_num = 0; thread_num < num_threads; ++thread_num) {
            tcp::socket peersock(io_context);
            while(1) {
                boost::asio::connect(peersock,
                    resolver.resolve(p0addr, std::to_string(port_p1_p0)), err);
                if (!err) break;
                std::cerr << "Connection to p0 refused, will retry.\n";
                sleep(1);
            }
            // Write 2 bytes to the socket indicating which thread
            // number this socket is for
            boost::asio::write(peersock,
                boost::asio::buffer(&thread_num, sizeof(thread_num)));
            peersocks.push_back(std::move(peersock));
        }
        for (int i=0;i<num_threads;++i) {
            tcp::socket serversock = acceptor_p2.accept();
            // Read 2 bytes from the socket, which will be the thread
            // number
            unsigned short thread_num;
            boost::asio::read(serversock,
                boost::asio::buffer(&thread_num, sizeof(thread_num)));
            if (thread_num >= num_threads) {
                std::cerr << "Received bad thread number from server\n";
            } else {
                serversocks[thread_num] = std::move(serversock);
            }
        }
    } else {
        std::cerr << "Invalid player number passed to mpcio_setup_computational\n";
    }

    // Read the start signal from P2
    char ack[1];
    boost::asio::read(serversocks[0], boost::asio::buffer(ack, 1));

    // Send the start ack to P2
    boost::asio::write(serversocks[0], boost::asio::buffer("", 1));
}

void mpcio_setup_server(boost::asio::io_context &io_context,
    const char *p0addr, const char *p1addr, int num_threads,
    std::deque<tcp::socket> &p0socks,
    std::deque<tcp::socket> &p1socks)
{
    // Make connections to P0 and P1
    tcp::resolver resolver(io_context);
    boost::system::error_code err;
    p0socks.clear();
    p1socks.clear();
    for (unsigned short thread_num = 0; thread_num < num_threads; ++thread_num) {
        tcp::socket p0sock(io_context);
        while(1) {
            boost::asio::connect(p0sock,
                resolver.resolve(p0addr, std::to_string(port_p2_p0)), err);
            if (!err) break;
            std::cerr << "Connection to p0 refused, will retry.\n";
            sleep(1);
        }
        // Write 2 bytes to the socket indicating which thread
        // number this socket is for
        boost::asio::write(p0sock,
            boost::asio::buffer(&thread_num, sizeof(thread_num)));
        p0socks.push_back(std::move(p0sock));
    }
    for (unsigned short thread_num = 0; thread_num < num_threads; ++thread_num) {
        tcp::socket p1sock(io_context);
        while(1) {
            boost::asio::connect(p1sock,
                resolver.resolve(p1addr, std::to_string(port_p2_p1)), err);
            if (!err) break;
            std::cerr << "Connection to p1 refused, will retry.\n";
            sleep(1);
        }
        // Write 2 bytes to the socket indicating which thread
        // number this socket is for
        boost::asio::write(p1sock,
            boost::asio::buffer(&thread_num, sizeof(thread_num)));
        p1socks.push_back(std::move(p1sock));
    }

    // Send the start signal to P0 and P1
    boost::asio::write(p0socks[0], boost::asio::buffer("", 1));
    boost::asio::write(p1socks[0], boost::asio::buffer("", 1));

    // Read the start ack from P0 and P1
    char ack[1];
    boost::asio::read(p0socks[0], boost::asio::buffer(ack, 1));
    boost::asio::read(p1socks[0], boost::asio::buffer(ack, 1));
}
