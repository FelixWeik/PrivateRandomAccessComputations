#ifndef __BST_HPP__
#define __BST_HPP__

#include "types.hpp"
#include "duoram.hpp"
#include "mpcio.hpp"
#include "options.hpp"

// #define BST_DEBUG

// Some simple utility functions:
bool reconstruct_RegBS(MPCTIO &tio, yield_t &yield, RegBS flag);

struct Node {
    RegAS key;
    RegXS pointers;
    RegXS value;

// Field-access macros so we can write A[i].NODE_KEY instead of
// A[i].field(&Node::key)

#define NODE_KEY field(&Node::key)
#define NODE_POINTERS field(&Node::pointers)
#define NODE_VALUE field(&Node::value)

    // For debugging and checking answers
    void dump() const {
        printf("[%016lx %016lx %016lx]", key.share(), pointers.share(),
            value.share());
    }

    // You'll need to be able to create a random element, and do the
    // operations +=, +, -=, - (binary and unary).  Note that for
    // XOR-shared fields, + and - are both really XOR.

    inline void randomize() {
        key.randomize();
        pointers.randomize();
        value.randomize();
    }

    inline Node &operator+=(const Node &rhs) {
        this->key += rhs.key;
        this->pointers += rhs.pointers;
        this->value += rhs.value;
        return *this;
    }

    inline Node operator+(const Node &rhs) const {
        Node res = *this;
        res += rhs;
        return res;
    }

    inline Node &operator-=(const Node &rhs) {
        this->key -= rhs.key;
        this->pointers -= rhs.pointers;
        this->value -= rhs.value;
        return *this;
    }

    inline Node operator-(const Node &rhs) const {
        Node res = *this;
        res -= rhs;
        return res;
    }

    inline Node operator-() const {
        Node res;
        res.key = -this->key;
        res.pointers = -this->pointers;
        res.value = -this->value;
        return res;
    }

    // Multiply each field by the local share of the corresponding field
    // in the argument
    inline Node mulshare(const Node &rhs) const {
        Node res = *this;
        res.key.mulshareeq(rhs.key);
        res.pointers.mulshareeq(rhs.pointers);
        res.value.mulshareeq(rhs.value);
        return res;
    }

    // You need a method to turn a leaf node of a DPF into a share of a
    // unit element of your type.  Typically set each RegAS to
    // dpf.unit_as(leaf) and each RegXS or RegBS to dpf.unit_bs(leaf).
    // Note that RegXS will extend a RegBS of 1 to the all-1s word, not
    // the word with value 1.  This is used for ORAM reads, where the
    // same DPF is used for all the fields.
    template <nbits_t WIDTH>
    inline void unit(const RDPF<WIDTH> &dpf,
        typename RDPF<WIDTH>::LeafNode leaf) {
        key = dpf.unit_as(leaf);
        pointers = dpf.unit_bs(leaf);
        value = dpf.unit_bs(leaf);
    }

    // Perform an update on each of the fields, using field-specific
    // MemRefs constructed from the Shape shape and the index idx
    template <typename Sh, typename U>
    inline static void update(Sh &shape, yield_t &shyield, U idx,
            const Node &M) {
        run_coroutines(shyield,
            [&shape, &idx, &M] (yield_t &yield) {
                Sh Sh_coro = shape.context(yield);
                Sh_coro[idx].NODE_KEY += M.key;
            },
            [&shape, &idx, &M] (yield_t &yield) {
                Sh Sh_coro = shape.context(yield);
                Sh_coro[idx].NODE_POINTERS += M.pointers;
            },
            [&shape, &idx, &M] (yield_t &yield) {
                Sh Sh_coro = shape.context(yield);
                Sh_coro[idx].NODE_VALUE += M.value;
            });
    }
};

/*
    A function to perform key comparsions for BST traversal.
    Inputs: k1 = key of node in the tree, k2 = insertion/deletion/lookup key.
    Evaluates (k2-k1), and combines the lt and eq flag into one (flag to go
    left), and keeps the gt flag as is (flag to go right) during traversal.
    Returns the shared bit flags lteq (go left) and gt (go right).
*/
std::tuple<RegBS, RegBS> compare_keys(MPCTIO &tio, yield_t &yield, RegAS k1, RegAS k2);

// I/O operations (for sending over the network)

template <typename T>
T& operator>>(T& is, Node &x)
{
    is >> x.key >> x.pointers >> x.value;
    return is;
}

template <typename T>
T& operator<<(T& os, const Node &x)
{
    os << x.key << x.pointers << x.value;
    return os;
}

// This macro will define I/O on tuples of two or three of the node type

DEFAULT_TUPLE_IO(Node)

struct del_return {
    // Flag to indicate if the key this deletion targets requires a successor swap
    RegBS F_ss;
    // Pointers to node to delete and successor node that would replace
    // deleted node
    RegXS N_d;
    RegXS N_s;
    // Flag for updating child pointer with returned pointer
    RegBS F_r;
    RegXS ret_ptr;
};

class BST {
  private:
    Duoram<Node> oram;
    RegXS root;

    size_t num_items = 0;
    size_t MAX_SIZE;

    std::vector<RegXS> empty_locations;

    std::tuple<RegXS, RegBS> insert(MPCTIO &tio, yield_t &yield, RegXS ptr,
        RegAS insertion_key, Duoram<Node>::Flat &A, int TTL, RegBS isDummy);
    void insert(MPCTIO &tio, yield_t &yield, const Node &node, Duoram<Node>::Flat &A);

    bool del(MPCTIO &tio, yield_t &yield, RegXS ptr, RegAS del_key,
        Duoram<Node>::Flat &A, RegBS F_af, RegBS F_fs, int TTL,
        del_return &ret_struct);

    RegBS lookup(MPCTIO &tio, yield_t &yield, RegXS ptr, RegAS key,
        Duoram<Node>::Flat &A, int TTL, RegBS isDummy, Node *ret_node);

    void pretty_print(const std::vector<Node> &R, value_t node,
        const std::string &prefix, bool is_left_child, bool is_right_child);

    std::tuple<bool, address_t> check_bst(const std::vector<Node> &R,
        value_t node, value_t min_key, value_t max_key);

  public:
    BST(int num_players, size_t size) : oram(num_players, size) {
        this->MAX_SIZE = size;
    };


    // Inserts the provided node into the BST
    void insert(MPCTIO &tio, yield_t &yield, Node &node);

    // Deletes the first node that matches del_key from the BST.
    // If an item with del_key does not exist in the tree, it results in an
    // explicit (non-oblivious) failure.
    bool del(MPCTIO &tio, yield_t &yield, RegAS del_key);

    // Returns the first node that matches key in the BST
    RegBS lookup(MPCTIO &tio, yield_t &yield, RegAS key, Node *ret_node);

    // Display and correctness check functions

    // Print the BST
    void pretty_print(MPCTIO &tio, yield_t &yield);

    // Check BST correctness
    void check_bst(MPCTIO &tio, yield_t &yield);

    // Debugging Functions

    #ifdef BST_DEBUG

        // Print the underlying ORAM state
        void print_oram(MPCTIO &tio, yield_t &yield);

        // Check the number of empty locations in ORAM
        // (Locations freed up after a delete operation, reusable for next insert.)
        size_t numEmptyLocations(){
            return(empty_locations.size());
        };

    #endif
};

void bst(MPCIO &mpcio,
    const PRACOptions &opts, char **args);

#endif
