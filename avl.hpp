#ifndef __AVL_HPP__
#define __AVL_HPP__

#include <math.h>
#include <stdio.h>
#include <string>
#include "types.hpp"
#include "duoram.hpp"
#include "cdpf.hpp"
#include "mpcio.hpp"
#include "options.hpp"
#include "bst.hpp"

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"

/*
  For AVL tree we'll treat the pointers fields as:
  < L_ptr (31 bits), R_ptr (31 bits), bal_L (1 bit), bal_R (1 bit)>
  Where L_ptr and R_ptr are pointers to the left and right child respectively,
  and bal_L and bal_R are the balance bits.

  Consequently AVL has its own versions of extract and set pointers for its children.
*/

#define AVL_PTR_SIZE 31

inline int AVL_TTL(size_t n) {
    double logn = log2(n);
    double TTL = 1.44 * logn;
    return (int(ceil(TTL)));
}

inline RegXS getAVLLeftPtr(RegXS pointer){
    return ((pointer&(0xFFFFFFFF00000000))>>33);
}

inline RegXS getAVLRightPtr(RegXS pointer){
    return ((pointer&(0x00000001FFFFFFFF))>>2);
}

inline void setAVLLeftPtr(RegXS &pointer, RegXS new_ptr){
    pointer&=(0x00000001FFFFFFFF);
    pointer+=(new_ptr<<33);
}

inline void setAVLRightPtr(RegXS &pointer, RegXS new_ptr){
    pointer&=(0xFFFFFFFE00000003);
    pointer+=(new_ptr<<2);
}

inline RegBS getLeftBal(RegXS pointer){
    RegBS bal_l;
    bool bal_l_bit = ((pointer.share() & (0x0000000000000002))>>1) & 1;
    bal_l.set(bal_l_bit);
    return bal_l;
}

inline RegBS getRightBal(RegXS pointer){
    RegBS bal_r;
    bool bal_r_bit = (pointer.share() & (0x0000000000000001)) & 1;
    bal_r.set(bal_r_bit);
    return bal_r;
}

inline void setLeftBal(RegXS &pointer, RegBS bal_l){
    value_t temp_ptr = pointer.share();
    temp_ptr&=(0xFFFFFFFFFFFFFFFD);
    temp_ptr^=((value_t)(bal_l.share()<<1));
    pointer.set(temp_ptr);
}

inline void setRightBal(RegXS &pointer, RegBS bal_r){
    value_t temp_ptr = pointer.share();
    temp_ptr&=(0xFFFFFFFFFFFFFFFE);
    temp_ptr^=((value_t)(bal_r.share()));
    pointer.set(temp_ptr);
}

inline void dumpAVL(Node n) {
    RegBS left_bal, right_bal;
    left_bal = getLeftBal(n.pointers);
    right_bal = getRightBal(n.pointers);
    printf("[%016lx %016lx %d %d %016lx]", n.key.share(), n.pointers.share(),
          left_bal.share(), right_bal.share(), n.value.share());
}

struct avl_del_return {
    // Flag to indicate if the key this deletion targets requires a successor swap
    RegBS F_ss;
    // Pointers to node to be deleted that would be replaced by successor node
    RegXS N_d;
    // Pointers to successor node that would replace deleted node
    RegXS N_s;
    // F_r: Flag for updating child pointer with returned pointer
    RegBS F_r;
    RegXS ret_ptr;
};

struct avl_insert_return {
  RegXS gp_node; // grandparent node
  RegXS p_node; // parent node
  RegXS c_node; // child node

  // Direction bits: 0 = Left, 1 = Right
  RegBS dir_gpp; // Direction bit from grandparent to parent node
  RegBS dir_pc; // Direction bit from p_node to c_node
  RegBS dir_cn; // Direction bit from c_node to new_node

  RegBS imbalance;
};

class AVL {
  private:
    Duoram<Node> oram;
    RegXS root;

    size_t num_items = 0;
    size_t MAX_SIZE;

    std::vector<RegXS> empty_locations;

    std::tuple<RegBS, RegBS, RegXS, RegBS> insert(MPCTIO &tio, yield_t &yield, RegXS ptr,
        RegXS ins_addr, RegAS ins_key, Duoram<Node>::Flat &A, int TTL, RegBS isDummy, 
        avl_insert_return *ret);

    void rotate(MPCTIO &tio, yield_t &yield, RegXS &gp_pointers, RegXS p_ptr,
        RegXS &p_pointers, RegXS c_ptr, RegXS &c_pointers, RegBS dir_gpp,
        RegBS dir_pc, RegBS isNotDummy, RegBS F_gp);

    std::tuple<RegBS, RegBS, RegBS, RegBS> updateBalanceIns(MPCTIO &tio, yield_t &yield,
        RegBS bal_l, RegBS bal_r, RegBS bal_upd, RegBS child_dir);

    std::tuple<bool, RegBS> del(MPCTIO &tio, yield_t &yield, RegXS ptr, RegAS del_key,
        Duoram<Node>::Flat &A, RegBS F_af, RegBS F_fs, int TTL,
        avl_del_return &ret_struct);

    std::tuple<RegBS, RegBS, RegBS, RegBS> updateBalanceDel(MPCTIO &tio, yield_t &yield,
        RegBS bal_l, RegBS bal_r, RegBS bal_upd, RegBS child_dir);

    bool lookup(MPCTIO &tio, yield_t &yield, RegXS ptr, RegAS key,
        Duoram<Node>::Flat &A, int TTL, RegBS isDummy, Node *ret_node);

  public:
    AVL(int num_players, size_t size) : oram(num_players, size) {
        this->MAX_SIZE = size;
    };

    void init(){
        num_items=0;
        empty_locations.clear();
    }

    size_t numEmptyLocations(){
        return(empty_locations.size());
    };

    void insert(MPCTIO &tio, yield_t &yield, const Node &node);

    // Deletes the first node that matches del_key
    bool del(MPCTIO &tio, yield_t &yield, RegAS del_key);

    // Returns the first node that matches key
    bool lookup(MPCTIO &tio, yield_t &yield, RegAS key, Node *ret_node);

    // Display and correctness check functions
    void pretty_print(MPCTIO &tio, yield_t &yield);
    void pretty_print(const std::vector<Node> &R, value_t node,
        const std::string &prefix, bool is_left_child, bool is_right_child);
    void check_avl(MPCTIO &tio, yield_t &yield);
    std::tuple<bool, bool, address_t> check_avl(const std::vector<Node> &R,
        value_t node, value_t min_key, value_t max_key);
    void print_oram(MPCTIO &tio, yield_t &yield);

    // For test functions ONLY:
    Duoram<Node>* get_oram() {
        return &oram;
    };

    RegXS get_root() {
        return root;
    };
};

void avl(MPCIO &mpcio, const PRACOptions &opts, char **args);
void avl_tests(MPCIO &mpcio, const PRACOptions &opts, char **args);

#endif
