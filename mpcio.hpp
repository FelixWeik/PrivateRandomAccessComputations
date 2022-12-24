#ifndef __MCPIO_HPP__
#define __MCPIO_HPP__

#include <iostream>
#include <fstream>
#include <vector>
#include <array>
#include <deque>
#include <queue>
#include <string>
#include <atomic>
#include <optional>
#include <bsd/stdlib.h> // arc4random_buf

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/chrono.hpp>

#include "types.hpp"

using boost::asio::ip::tcp;

// Classes to represent stored precomputed data (e.g., multiplication triples)

template<typename T, typename N>
class PreCompStorage {
public:
    PreCompStorage() : name(N::name), depth(0), count(0) {}
    PreCompStorage(unsigned player, bool preprocessing,
        const char *filenameprefix, unsigned thread_num);
    void init(unsigned player, bool preprocessing,
        const char *filenameprefix, unsigned thread_num, nbits_t depth = 0);
    void get(T& nextval);

    inline size_t get_stats() { return count; }
    inline void reset_stats() { count = 0; }
private:
    std::ifstream storage;
    std::string name;
    nbits_t depth;
    size_t count;
};

// If we want to send Lamport clocks in messages, define this.  It adds
// an 8-byte header to each message (length and Lamport clock), so it
// has a small network cost.  We always define and pass the Lamport
// clock member of MPCIO to the IO functions for simplicity, but they're
// ignored if this isn't defined
#define SEND_LAMPORT_CLOCKS
using lamport_t = uint32_t;
using atomic_lamport_t = std::atomic<lamport_t>;
using opt_lamport_t = std::optional<lamport_t>;
#ifdef SEND_LAMPORT_CLOCKS
struct MessageWithHeader {
    std::string header;
    std::string message;

    MessageWithHeader(std::string &&msg, lamport_t lamport) :
        message(std::move(msg)) {
            char hdr[sizeof(uint32_t) + sizeof(lamport_t)];
            uint32_t msglen = uint32_t(message.size());
            memmove(hdr, &msglen, sizeof(msglen));
            memmove(hdr+sizeof(msglen), &lamport, sizeof(lamport));
            header.assign(hdr, sizeof(hdr));
    }
};
#endif

// A class to wrap a socket to another MPC party.  This wrapping allows
// us to do some useful logging, and perform async_writes transparently
// to the application.

class MPCSingleIO {
    tcp::socket sock;
    size_t totread, totwritten;
#ifdef RECORD_IOTRACE
    std::vector<ssize_t> iotrace;
#endif

    // To avoid blocking if both we and our peer are trying to send
    // something very large, and neither side is receiving, we will send
    // with async_write.  But this has a number of implications:
    // - The data to be sent has to be copied into this MPCSingleIO,
    //   since asio::buffer pointers are not guaranteed to remain valid
    //   after the end of the coroutine that created them
    // - We have to keep a queue of messages to be sent, in case
    //   coroutines call send() before the previous message has finished
    //   being sent
    // - This queue may be accessed from the async_write thread as well
    //   as the work thread that uses this MPCSingleIO directly (there
    //   should be only one of the latter), so we need some locking

    // This is where we accumulate data passed in queue()
    std::string dataqueue;

    // When send() is called, the above dataqueue is appended to this
    // messagequeue, and the dataqueue is reset.  If messagequeue was
    // empty before this append, launch async_write to write the first
    // thing in the messagequeue.  When async_write completes, it will
    // delete the first thing in the messagequeue, and see if there are
    // any more elements.  If so, it will start another async_write.
    // The invariant is that there is an async_write currently running
    // iff messagequeue is nonempty.
#ifdef SEND_LAMPORT_CLOCKS
    std::queue<MessageWithHeader> messagequeue;
#else
    std::queue<std::string> messagequeue;
#endif

    // If a single message is broken into chunks in order to get the
    // first part of it out on the wire while the rest of it is still
    // being computed, we want the Lamport clock of all the chunks to be
    // that of when the message is first created.  This value will be
    // nullopt when there has been no queue() since the last explicit
    // send() (as opposed to the implicit send() called by queue()
    // itself if it wants to get a chunk on its way), and will be set to
    // the current lamport clock when that first queue() after each
    // explicit send() happens.
    opt_lamport_t message_lamport;

#ifdef SEND_LAMPORT_CLOCKS
    // If Lamport clocks are being sent, then the data stream is divided
    // into chunks, each with a header containing the length of the
    // chunk and the Lamport clock.  So when we read, we'll read a whole
    // chunk, and store it here.  Then calls to recv() will read pieces
    // of this buffer until it has all been read, and then read the next
    // header and chunk.
    std::string recvdata;
    size_t recvdataremain;
#endif

    // Never touch the above messagequeue without holding this lock (you
    // _can_ touch the strings it contains, though, if you looked one up
    // while holding the lock).
    boost::mutex messagequeuelock;

    // Asynchronously send the first message from the message queue.
    // * The messagequeuelock must be held when this is called! *
    // This method may be called from either thread (the work thread or
    // the async_write handler thread).
    void async_send_from_msgqueue();

public:
    MPCSingleIO(tcp::socket &&sock) :
        sock(std::move(sock)), totread(0), totwritten(0) {}

    // Returns 1 if a new message is started, 0 otherwise
    size_t queue(const void *data, size_t len, lamport_t lamport);

    void send(bool implicit_send = false);

    size_t recv(void *data, size_t len, lamport_t &lamport);

#ifdef RECORD_IOTRACE
    void dumptrace(std::ostream &os, const char *label = NULL);

    void resettrace() {
        iotrace.clear();
    }
#endif
};

// A base class to represent all of a computation peer or server's IO,
// either to other parties or to local storage (the computation and
// server cases are separate subclasses below).

struct MPCIO {
    int player;
    bool preprocessing;
    size_t num_threads;
    atomic_lamport_t lamport;
    std::vector<size_t> msgs_sent;
    std::vector<size_t> msg_bytes_sent;
    std::vector<size_t> aes_ops;
    boost::chrono::steady_clock::time_point steady_start;
    boost::chrono::process_cpu_clock::time_point cpu_start;

    MPCIO(int player, bool preprocessing, size_t num_threads) :
        player(player), preprocessing(preprocessing),
        num_threads(num_threads), lamport(0)
    {
        reset_stats();
    }

    void reset_stats();

    static void dump_memusage(std::ostream &os);

    void dump_stats(std::ostream &os);
};

// A class to represent all of a computation peer's IO, either to other
// parties or to local storage

struct MPCPeerIO : public MPCIO {
    // We use a deque here instead of a vector because you can't have a
    // vector of a type without a copy constructor (tcp::socket is the
    // culprit), but you can have a deque of those for some reason.
    std::deque<MPCSingleIO> peerios;
    std::deque<MPCSingleIO> serverios;
    std::vector<PreCompStorage<MultTriple, MultTripleName>> triples;
    std::vector<PreCompStorage<HalfTriple, HalfTripleName>> halftriples;
    std::vector<PreCompStorage<CDPF, CDPFName>> cdpfs;
    // The outer vector is (like above) one item per thread
    // The inner array is indexed by DPF depth (depth d is at entry d-1)
    std::vector<std::array<PreCompStorage<RDPFTriple, RDPFTripleName>,ADDRESS_MAX_BITS>> rdpftriples;

    MPCPeerIO(unsigned player, bool preprocessing,
            std::deque<tcp::socket> &peersocks,
            std::deque<tcp::socket> &serversocks);

    void dump_precomp_stats(std::ostream &os);

    void reset_precomp_stats();

    void dump_stats(std::ostream &os);
};

// A class to represent all of the server party's IO, either to
// computational parties or to local storage

struct MPCServerIO : public MPCIO {
    std::deque<MPCSingleIO> p0ios;
    std::deque<MPCSingleIO> p1ios;
    // The outer vector is (like above) one item per thread
    // The inner array is indexed by DPF depth (depth d is at entry d-1)
    std::vector<std::array<PreCompStorage<RDPFPair, RDPFPairName>,ADDRESS_MAX_BITS>> rdpfpairs;

    MPCServerIO(bool preprocessing,
            std::deque<tcp::socket> &p0socks,
            std::deque<tcp::socket> &p1socks);

    void dump_precomp_stats(std::ostream &os);

    void reset_precomp_stats();

    void dump_stats(std::ostream &os);
};

class MPCSingleIOStream {
    MPCSingleIO &sio;
    lamport_t &lamport;
    size_t &msgs_sent;
    size_t &msg_bytes_sent;

public:
    MPCSingleIOStream(MPCSingleIO &sio, lamport_t &lamport,
            size_t &msgs_sent, size_t &msg_bytes_sent) :
        sio(sio), lamport(lamport), msgs_sent(msgs_sent),
        msg_bytes_sent(msg_bytes_sent) {}

    MPCSingleIOStream& write(const char *data, std::streamsize len) {
        size_t newmsg = sio.queue(data, len, lamport);
        msgs_sent += newmsg;
        msg_bytes_sent += len;
        return *this;
    }

    MPCSingleIOStream& read(char *data, std::streamsize len) {
        sio.recv(data, len, lamport);
        return *this;
    }
};

// A handle to one thread's sockets and streams in a MPCIO

class MPCTIO {
    int thread_num;
    lamport_t thread_lamport;
    MPCIO &mpcio;
    std::optional<MPCSingleIOStream> peer_iostream;
    std::optional<MPCSingleIOStream> server_iostream;
    std::optional<MPCSingleIOStream> p0_iostream;
    std::optional<MPCSingleIOStream> p1_iostream;

public:
    MPCTIO(MPCIO &mpcio, int thread_num);

    // Sync our per-thread lamport clock with the master one in the
    // mpcio.  You only need to call this explicitly if your MPCTIO
    // outlives your thread (in which case call it after the join), or
    // if your threads do interthread communication amongst themselves
    // (in which case call it in the sending thread before the send, and
    // call it in the receiving thread after the receive).
    void sync_lamport();

    // The normal case, where the MPCIO is created inside the thread,
    // and so destructed when the thread ends, is handled automatically
    // here.
    ~MPCTIO() {
        sync_lamport();
    }

    // Computational peers use these functions:

    // Queue up data to the peer or to the server

    void queue_peer(const void *data, size_t len);
    void queue_server(const void *data, size_t len);

    // Receive data from the peer or to the server

    size_t recv_peer(void *data, size_t len);
    size_t recv_server(void *data, size_t len);

    // Or get these MPCSingleIOStreams
    MPCSingleIOStream& iostream_peer() { return peer_iostream.value(); }
    MPCSingleIOStream& iostream_server() { return server_iostream.value(); }

    // The server uses these functions:

    // Queue up data to p0 or p1

    void queue_p0(const void *data, size_t len);
    void queue_p1(const void *data, size_t len);

    // Receive data from p0 or p1

    size_t recv_p0(void *data, size_t len);
    size_t recv_p1(void *data, size_t len);

    // Or get these MPCSingleIOStreams
    MPCSingleIOStream& iostream_p0() { return p0_iostream.value(); }
    MPCSingleIOStream& iostream_p1() { return p1_iostream.value(); }

    // Everyone can use the remaining functions.

    // Send all queued data for this thread

    void send();

    // Functions to get precomputed values.  If we're in the online
    // phase, get them from PreCompStorage.  If we're in the
    // preprocessing phase, read them from the server.

    MultTriple triple();
    HalfTriple halftriple();
    SelectTriple selecttriple();

    // These ones only work during the online phase
    // Computational peers call:
    RDPFTriple rdpftriple(nbits_t depth);
    // The server calls:
    RDPFPair rdpfpair(nbits_t depth);
    // Anyone can call:
    CDPF cdpf();

    // Accessors

    inline int player() { return mpcio.player; }
    inline bool preprocessing() { return mpcio.preprocessing; }
    inline bool is_server() { return mpcio.player == 2; }
    inline size_t& aes_ops() { return mpcio.aes_ops[thread_num]; }
    inline size_t msgs_sent() { return mpcio.msgs_sent[thread_num]; }
};

// Set up the socket connections between the two computational parties
// (P0 and P1) and the server party (P2).  For each connection, the
// lower-numbered party does the accept() and the higher-numbered party
// does the connect().

// Computational parties call this version with player=0 or 1

void mpcio_setup_computational(unsigned player,
    boost::asio::io_context &io_context,
    const char *p0addr,  // can be NULL when player=0
    int num_threads,
    std::deque<tcp::socket> &peersocks,
    std::deque<tcp::socket> &serversocks);

// Server calls this version

void mpcio_setup_server(boost::asio::io_context &io_context,
    const char *p0addr, const char *p1addr, int num_threads,
    std::deque<tcp::socket> &p0socks,
    std::deque<tcp::socket> &p1socks);

#endif
