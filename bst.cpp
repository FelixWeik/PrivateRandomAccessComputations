#include <functional>

#include "bst.hpp"

#ifdef BST_DEBUG
    void BST::print_oram(MPCTIO &tio, yield_t &yield) {
        auto A = oram.flat(tio, yield);
        auto R = A.reconstruct();

        for(size_t i=0;i<R.size();++i) {
            printf("\n%04lx ", i);
            R[i].dump();
        }
        printf("\n");
    }
#endif

// Helper function to reconstruct shared RegBS
bool reconstruct_RegBS(MPCTIO &tio, yield_t &yield, RegBS flag) {
    RegBS reconstructed_flag;
    if (tio.player() < 2) {
        RegBS peer_flag;
        tio.queue_peer(&flag, 1);
        tio.queue_server(&flag, 1);
        yield();
        tio.recv_peer(&peer_flag, 1);
        reconstructed_flag = flag;
        reconstructed_flag ^= peer_flag;
    } else {
        RegBS p0_flag, p1_flag;
        yield();
        tio.recv_p0(&p0_flag, 1);
        tio.recv_p1(&p1_flag, 1);
        reconstructed_flag = p0_flag;
        reconstructed_flag ^= p1_flag;
    }
    return reconstructed_flag.bshare;
}

/*
    A function to assign a new random 8-bit key to a node, and resets its
    pointers to zeroes. The node is assigned a new random 64-bit value.
*/
static void randomize_node(Node &a) {
    a.key.randomize(8);
    a.pointers.set(0);
    a.value.randomize();
}

/*
    A function to perform key comparsions for BST traversal.
    Inputs: k1 = key of node in the tree, k2 = insertion/deletion/lookup key.
    Evaluates (k2-k1), and combines the lt and eq flag into one (flag to go
    left), and keeps the gt flag as is (flag to go right) during traversal.

*/
std::tuple<RegBS, RegBS> compare_keys(MPCTIO &tio, yield_t &yield, RegAS k1,
        RegAS k2) {
    CDPF cdpf = tio.cdpf(yield);
    auto [lt, eq, gt] = cdpf.compare(tio, yield, k2 - k1, tio.aes_ops());
    RegBS lteq = lt^eq;
    return {lteq, gt};
}

// Assuming pointer of 64 bits is split as:
// - 32 bits Left ptr (L)
// - 32 bits Right ptr (R)
// The pointers are stored as: (L << 32) | R

inline RegXS extractLeftPtr(RegXS pointer){
    return ((pointer&(0xFFFFFFFF00000000))>>32);
}

inline RegXS extractRightPtr(RegXS pointer){
    return (pointer&(0x00000000FFFFFFFF));
}

inline void setLeftPtr(RegXS &pointer, RegXS new_ptr){
    pointer&=(0x00000000FFFFFFFF);
    pointer+=(new_ptr<<32);
}

inline void setRightPtr(RegXS &pointer, RegXS new_ptr){
    pointer&=(0xFFFFFFFF00000000);
    pointer+=(new_ptr);
}


// Pretty-print a reconstructed BST, rooted at node. is_left_child and
// is_right_child indicate whether node is a left or right child of its
// parent.  They cannot both be true, but the root of the tree has both
// of them false.
void BST::pretty_print(const std::vector<Node> &R, value_t node,
    const std::string &prefix = "", bool is_left_child = false,
    bool is_right_child = false)
{
    if (node == value_t(0)) {
        // NULL pointer
        if (is_left_child) {
            printf("%s\xE2\x95\xA7\n", prefix.c_str()); // ╧
        } else if (is_right_child) {
            printf("%s\xE2\x95\xA4\n", prefix.c_str()); // ╤
        } else {
            printf("%s\xE2\x95\xA2\n", prefix.c_str()); // ╢
        }
        return;
    }
    const Node &n = R[node];
    value_t left_ptr = extractLeftPtr(n.pointers).xshare;
    value_t right_ptr = extractRightPtr(n.pointers).xshare;
    std::string rightprefix(prefix), leftprefix(prefix),
        nodeprefix(prefix);
    if (is_left_child) {
        rightprefix.append("\xE2\x94\x82"); // │
        leftprefix.append(" ");
        nodeprefix.append("\xE2\x94\x94"); // └
    } else if (is_right_child) {
        rightprefix.append(" ");
        leftprefix.append("\xE2\x94\x82"); // │
        nodeprefix.append("\xE2\x94\x8C"); // ┌
    } else {
        rightprefix.append(" ");
        leftprefix.append(" ");
        nodeprefix.append("\xE2\x94\x80"); // ─
    }
    pretty_print(R, right_ptr, rightprefix, false, true);
    printf("%s\xE2\x94\xA4", nodeprefix.c_str()); // ┤
    n.dump();
    printf("\n");
    pretty_print(R, left_ptr, leftprefix, true, false);
}

void BST::pretty_print(MPCTIO &tio, yield_t &yield) {
    RegXS peer_root;
    RegXS reconstructed_root = root;
    if (tio.player() == 1) {
        tio.queue_peer(&root, sizeof(root));
        yield();
    } else {
        RegXS peer_root;
        yield();
        if(tio.player()==0) {
           tio.recv_peer(&peer_root, sizeof(peer_root));
        }
        reconstructed_root += peer_root;
    }

    auto A = oram.flat(tio, yield);
    auto R = A.reconstruct();
    if(tio.player()==0) {
        pretty_print(R, reconstructed_root.xshare);
    }
}

// Check the BST invariant of the tree (that all keys to the left are
// less than or equal to this key, all keys to the right are strictly
// greater, and this is true recursively).  Returns a
// tuple<bool,address_t>, where the bool says whether the BST invariant
// holds, and the address_t is the height of the tree (which will be
// useful later when we check AVL trees).
std::tuple<bool, address_t> BST::check_bst(const std::vector<Node> &R,
    value_t node, value_t min_key = 0, value_t max_key = ~0)
{
    //printf("node = %ld\n", node);
    if (node == value_t(0)) {
        return { true, 0 };
    }
    const Node &n = R[node];
    value_t key = n.key.ashare;
    value_t left_ptr = extractLeftPtr(n.pointers).xshare;
    value_t right_ptr = extractRightPtr(n.pointers).xshare;
    auto [leftok, leftheight ] = check_bst(R, left_ptr, min_key, key);
    auto [rightok, rightheight ] = check_bst(R, right_ptr, key+value_t(1), max_key);
    address_t height = leftheight;
    if (rightheight > height) {
        height = rightheight;
    }
    height += 1;
    //printf("node = %ld, leftok = %d, rightok = %d\n", node, leftok, rightok);
    return { leftok && rightok && key >= min_key && key <= max_key,
        height };
}


void BST::check_bst(MPCTIO &tio, yield_t &yield) {
    auto A = oram.flat(tio, yield);
    auto R = A.reconstruct();

    RegXS rec_root = this->root;
    if (tio.player() == 1) {
        tio.queue_peer(&(this->root), sizeof(this->root));
        yield();
    } else {
        RegXS peer_root;
        yield();
        if(tio.player()==0) {
            tio.recv_peer(&peer_root, sizeof(peer_root));
        }
        rec_root+= peer_root;
    }
    if (tio.player() == 0) {
      auto [ ok, height ] = check_bst(R, rec_root.xshare);
      printf("BST structure %s\nBST height = %u\n",
          ok ? "ok" : "NOT OK", height);
    }
}

/*
    The recursive insert() call, invoked by the wrapper insert() function.

    Takes as input the pointer to the current node in tree traversal (ptr),
    the key to be inserted (insertion_key), the underlying Duoram as a
    flat (A), and the Time-To_live TTL, and a shared flag (isDummy) which
    tracks if the operation is dummy/real.

    Returns a tuple <ptr, dir> where
    ptr: the pointer to the node where the insertion should happen
    dir: the bit indicating whether the new node should be inserted as the
         left/right child.

*/
std::tuple<RegXS, RegBS> BST::insert(MPCTIO &tio, yield_t &yield, RegXS ptr,
    RegAS insertion_key, Duoram<Node>::Flat &A, int TTL, RegBS isDummy) {
    if(TTL==0) {
        RegBS zero;
        return {ptr, zero};
    }

    RegBS isNotDummy = isDummy ^ (!tio.player());
    Node cnode = A[ptr];
    // Compare key
    auto [lteq, gt] = compare_keys(tio, yield, cnode.key, insertion_key);

    // Depending on [lteq, gt] select the next ptr/index as
    // upper 32 bits of cnode.pointers if lteq
    // lower 32 bits of cnode.pointers if gt
    RegXS left = extractLeftPtr(cnode.pointers);
    RegXS right = extractRightPtr(cnode.pointers);

    RegXS next_ptr;
    mpc_select(tio, yield, next_ptr, gt, left, right, 32);

    CDPF dpf = tio.cdpf(yield);
    size_t &aes_ops = tio.aes_ops();
    // F_z: Check if this is last node on path
    RegBS F_z = dpf.is_zero(tio, yield, next_ptr, aes_ops);
    RegBS F_i;

    // F_i: If this was last node on path (F_z) && isNotDummy:
    //          insert new_node here.
    mpc_and(tio, yield, F_i, (isNotDummy), F_z);

    isDummy^=F_i;
    auto [wptr, direction] = insert(tio, yield, next_ptr, insertion_key, A, TTL-1, isDummy);

    RegXS ret_ptr;
    RegBS ret_direction;
    // If we insert here (F_i), return the ptr to this node as wptr
    // and update direction to the direction taken by compare_keys
    run_coroutines(tio, [&tio, &ret_ptr, F_i, wptr, ptr](yield_t &yield)
        { mpc_select(tio, yield, ret_ptr, F_i, wptr, ptr);},
        [&tio, &ret_direction, F_i, direction, gt](yield_t &yield)
        //ret_direction = direction + F_i (direction - gt)
        { mpc_and(tio, yield, ret_direction, F_i, direction^gt);});

    ret_direction^=direction;

    return {ret_ptr, ret_direction};
}


/*
    The wrapper insert() operation invoked by the main insert call
    BST::insert(tio, yield, Node& new_node);

    Takes as input the new node (node), the underlying Duoram as a flat (A).
*/
void BST::insert(MPCTIO &tio, yield_t &yield, const Node &node, Duoram<Node>::Flat &A) {
    bool player0 = tio.player()==0;
    // If there are no items in tree. Make this new item the root.
    if (num_items==0) {
        A[1] = node;
        // Set root to a secret sharing of the constant value 1
        root.set(1*tio.player());
        num_items++;
        //printf("num_items == %ld!\n", num_items);
        return;
    } else {
        // Insert node into next free slot in the ORAM
        int new_id;
        RegXS insert_address;
        int TTL = num_items++;
        bool insertAtEmptyLocation = (empty_locations.size() > 0);
        if(insertAtEmptyLocation) {
            insert_address = empty_locations.back();
            empty_locations.pop_back();
            A[insert_address] = node;
        } else {
            new_id = 1 + num_items;
            A[new_id] = node;
            insert_address.set(new_id * tio.player());
        }

        RegBS isDummy;
        //Do a recursive insert
        auto [wptr, direction] = insert(tio, yield, root, node.key, A, TTL, isDummy);

        //Complete the insertion by reading wptr and updating its pointers
        RegXS pointers = A[wptr].NODE_POINTERS;
        RegXS left_ptr = extractLeftPtr(pointers);
        RegXS right_ptr = extractRightPtr(pointers);
        RegXS new_right_ptr, new_left_ptr;

        RegBS not_direction = direction;
        if (player0) {
            not_direction^=1;
        }

        run_coroutines(tio,
            [&tio, &new_right_ptr, direction, right_ptr, insert_address](yield_t &yield)
            { mpc_select(tio, yield, new_right_ptr, direction, right_ptr, insert_address);},
            [&tio, &new_left_ptr, not_direction, left_ptr, insert_address](yield_t &yield)
            { mpc_select(tio, yield, new_left_ptr, not_direction, left_ptr, insert_address);});

        setLeftPtr(pointers, new_left_ptr);
        setRightPtr(pointers, new_right_ptr);
        A[wptr].NODE_POINTERS = pointers;
    }
}

/*
    Insert a new node into the BST.
    Takes as input the new node (node).
*/
void BST::insert(MPCTIO &tio, yield_t &yield, Node &node) {
    auto A = oram.flat(tio, yield);

    insert(tio, yield, node, A);
    /*
    // To visualize database and tree after each insert:
    auto R = A.reconstruct();
    if (tio.player() == 0) {
        for(size_t i=0;i<R.size();++i) {
            printf("\n%04lx ", i);
            R[i].dump();
        }
        printf("\n");
    }
    pretty_print(R, 1);
    */
}

RegBS BST::lookup(MPCTIO &tio, yield_t &yield, RegXS ptr, RegAS key, Duoram<Node>::Flat &A,
    int TTL, RegBS isDummy, Node *ret_node) {
    if(TTL==0) {
        // If we found the key, then isDummy will be true
        return isDummy;
    }


    Node cnode = A[ptr];
    // Compare key
    CDPF cdpf = tio.cdpf(yield);
    auto [lt, eq, gt] = cdpf.compare(tio, yield, key - cnode.key, tio.aes_ops());

    // Depending on [lteq, gt] select the next ptr/index as
    // upper 32 bits of cnode.pointers if lteq
    // lower 32 bits of cnode.pointers if gt
    RegXS left = extractLeftPtr(cnode.pointers);
    RegXS right = extractRightPtr(cnode.pointers);

    RegXS next_ptr;

    RegBS F_found;
    // If we haven't found the key yet, and the lookup matches the current node key,
    // then we found the node to return
    RegBS isNotDummy = isDummy ^ (!tio.player());

    // Note: This logic returns the last matched key and value.
    // Returning the first one incurs an additional round.
    std::vector<coro_t> coroutines;
    coroutines.emplace_back(
        [&tio, &next_ptr, gt, left, right](yield_t &yield)
        { mpc_select(tio, yield, next_ptr, gt, left, right, 32);});
    coroutines.emplace_back(
        [&tio, &F_found, isNotDummy, eq](yield_t &yield)
        { mpc_and(tio, yield, F_found, isNotDummy, eq);});
    coroutines.emplace_back(
        [&tio, &ret_node, eq, cnode](yield_t &yield)
        { mpc_select(tio, yield, ret_node->key, eq, ret_node->key, cnode.key);});
    coroutines.emplace_back(
        [&tio, &ret_node, eq, cnode](yield_t &yield)
        { mpc_select(tio, yield, ret_node->value, eq, ret_node->value, cnode.value);});
    coroutines.emplace_back(
        [&tio, &isDummy, eq](yield_t &yield)
        { mpc_or(tio, yield, isDummy, isDummy, eq);});
    run_coroutines(tio, coroutines);

    #ifdef BST_DEBUG
        size_t ckey = mpc_reconstruct(tio, yield, cnode.key);
        size_t lkey = mpc_reconstruct(tio, yield, key);
        bool rec_lt = mpc_reconstruct(tio, yield, lt);
        bool rec_eq = mpc_reconstruct(tio, yield, eq);
        bool rec_gt = mpc_reconstruct(tio, yield, gt);
        bool rec_found = mpc_reconstruct(tio, yield, isDummy);
        bool rec_f_found = mpc_reconstruct(tio, yield, F_found);
        printf("rec_lt = %d, rec_eq = %d, rec_gt = %d\n", rec_lt, rec_eq, rec_gt);
        printf("rec_isDummy/found = %d ,rec_f_found = %d, cnode.key = %ld, lookup key = %ld\n", rec_found, rec_f_found, ckey, lkey);
    #endif

    RegBS found = lookup(tio, yield, next_ptr, key, A, TTL-1, isDummy, ret_node);

    return found;
}

RegBS BST::lookup(MPCTIO &tio, yield_t &yield, RegAS key, Node *ret_node) {
    auto A = oram.flat(tio, yield);

    RegBS isDummy;

    RegBS found = lookup(tio, yield, root, key, A, num_items, isDummy, ret_node);
    /*
    // To visualize database and tree after each lookup:
    auto R = A.reconstruct();
    if (tio.player() == 0) {
        for(size_t i=0;i<R.size();++i) {
            printf("\n%04lx ", i);
            R[i].dump();
        }
        printf("\n");
    }
    pretty_print(R, 1);
    */
    return found;
}


/*
    The recursive del() call, invoked by the wrapper del() function.

    Takes as input the pointer to the current node in tree traversal (ptr),
    the key to be deleted (del_key), the underlying Duoram as a
    flat (A), Flags af (already found) and fs (find successor), and the
    Time-To_live TTL. Finally, a return structure ret_struct that tracks
    the location of the successor node and the node to delete, in order
    to perform the actual deletion after the recursive traversal. This
    is required in the case of a deletion that requires a successor swap
    (i.e., when the node to delete has both children).

    Returns success/fail bit.
*/

bool BST::del(MPCTIO &tio, yield_t &yield, RegXS ptr, RegAS del_key,
     Duoram<Node>::Flat &A, RegBS af, RegBS fs, int TTL,
    del_return &ret_struct) {
    bool player0 = tio.player()==0;
    //printf("TTL = %d\n", TTL);
    if(TTL==0) {
        //Reconstruct and return af
        bool success = reconstruct_RegBS(tio, yield, af);
        //printf("Reconstructed flag = %d\n", success);
        if(player0) {
            ret_struct.F_r^=1;
        }
        return success;
    } else {
        // s1: shares of 1 bit, s0: shares of 0 bit
        RegBS s1, s0;
        s1.set(tio.player()==1);

        Node node = A[ptr];
        RegXS left = extractLeftPtr(node.pointers);
        RegXS right = extractRightPtr(node.pointers);

        CDPF cdpf = tio.cdpf(yield);
        size_t &aes_ops = tio.aes_ops();
        RegBS l0, r0, lt, eq, gt;
        // l0: Is left child 0
        // r0: Is right child 0
        run_coroutines(tio,
            [&tio, &l0, left, &aes_ops, &cdpf](yield_t &yield)
            { l0 = cdpf.is_zero(tio, yield, left, aes_ops);},
            [&tio, &r0, right, &aes_ops, &cdpf](yield_t &yield)
            { r0 = cdpf.is_zero(tio, yield, right, aes_ops);},
            [&tio, &lt, &eq, &gt, del_key, node, &cdpf](yield_t &yield)
            // Compare Key
            { auto [a, b, c] = cdpf.compare(tio, yield, del_key - node.key, tio.aes_ops());
              lt = a; eq = b; gt = c;});

        /*
        // Reconstruct and Debug Block 0
        bool lt_rec, eq_rec, gt_rec;
        lt_rec = mpc_reconstruct(tio, yield, lt);
        eq_rec = mpc_reconstruct(tio, yield, eq);
        gt_rec = mpc_reconstruct(tio, yield, gt);
        size_t del_key_rec, node_key_rec;
        del_key_rec = mpc_reconstruct(tio, yield, del_key);
        node_key_rec = mpc_reconstruct(tio, yield, node.key);
        printf("node.key = %ld, del_key= %ld\n", node_key_rec, del_key_rec);
        printf("cdpf.compare results: lt = %d, eq = %d, gt = %d\n", lt_rec, eq_rec, gt_rec);
        */

        // c is the direction bit for next_ptr
        // (c=0: go left or c=1: go right)
        RegBS c = gt;
        // lf = local found. We found the key to delete in this level.
        RegBS lf = eq;

        // F_{X}: Flags that indicate the number of children this node has
        // F_0: no children, F_1: one child, F_2: both children
        RegBS F_0, F_1, F_2;
        // F_1 = l0 \xor r0
        F_1 = l0 ^ r0;

        // We set next ptr based on c, but we need to handle three
        // edge cases where we do not go by just the comparison result
        RegXS next_ptr;
        RegBS c_prime;
        // Case 1: found the node here (lf): we traverse down the lone child path.
        // or we are finding successor (fs) and there is no left child.
        RegBS F_c1, F_c2, F_c3, F_c4;

        // Case 1: lf & F_1
        run_coroutines(tio,
            [&tio, &F_c1, lf, F_1](yield_t &yield)
            { mpc_and(tio, yield, F_c1, lf, F_1);},
            [&tio, &F_0, l0, r0] (yield_t &yield)
            // F_0 = l0 & r0
            { mpc_and(tio, yield, F_0, l0, r0);});

        // F_2 = !(F_0 + F_1) (Only 1 of F_0, F_1, and F_2 can be true)
        F_2 = F_0 ^ F_1;
        if(player0)
            F_2^=1;

        /*
        // Reconstruct and Debug Block 1
        bool F_0_rec, F_1_rec, F_2_rec, c_prime_rec;
        F_0_rec = mpc_reconstruct(tio, yield, F_0);
        F_1_rec = mpc_reconstruct(tio, yield, F_1);
        F_2_rec = mpc_reconstruct(tio, yield, F_2);
        c_prime_rec = mpc_reconstruct(tio, yield, c_prime);
        printf("F_0 = %d, F_1 = %d, F_2 = %d, c_prime = %d\n", F_0_rec, F_1_rec, F_2_rec, c_prime_rec);
        */

        run_coroutines(tio,
            [&tio, &c_prime, F_c1, c, l0](yield_t &yield)
            // Set c_prime for Case 1
            { mpc_select(tio, yield, c_prime, F_c1, c, l0);},
            [&tio, &F_c2, lf, F_2](yield_t &yield)
            // Case 2: found the node here (lf) and node has both children (F_2)
            // In find successor case, so find inorder successor
            // (Go right and then find leftmost child.)
            { mpc_and(tio, yield, F_c2, lf, F_2);});

        /*
        // Reconstruct and Debug Block 2
        bool F_c2_rec, s1_rec;
        F_c2_rec = mpc_reconstruct(tio, yield, F_c2);
        s1_rec = mpc_reconstruct(tio, yield, s1);
        c_prime_rec = mpc_reconstruct(tio, yield, c_prime);
        printf("c_prime = %d, F_c2 = %d, s1 = %d\n", c_prime_rec, F_c2_rec, s1_rec);
        */

        run_coroutines(tio,
            [&tio, &c_prime, F_c2, s1](yield_t &yield)
            { mpc_select(tio, yield, c_prime, F_c2, c_prime, s1);},
            [&tio, &F_c3, fs, F_2](yield_t &yield)
            // Case 3: finding successor (fs) and node has both children (F_2)
            // Go left.
            { mpc_and(tio, yield, F_c3, fs, F_2);});

        run_coroutines(tio,
            [&tio, &c_prime, F_c3, s0](yield_t &yield)
            { mpc_select(tio, yield, c_prime, F_c3, c_prime, s0);},
            // Case 4: finding successor (fs) and node has no more left children (l0)
            // This is the successor node then.
            // Go right (since no more left)
            [&tio, &F_c4, fs, l0] (yield_t &yield)
            { mpc_and(tio, yield, F_c4, fs, l0);});

        mpc_select(tio, yield, c_prime, F_c4, c_prime, l0);

        RegBS af_prime, fs_prime;
        run_coroutines(tio,
            [&tio, &next_ptr, c_prime, left, right](yield_t &yield)
            // Set next_ptr
            { mpc_select(tio, yield, next_ptr, c_prime, left, right, 32);},
            [&tio, &af_prime, af, lf](yield_t &yield)
            { mpc_or(tio, yield, af_prime, af, lf);},
            [&tio, &fs_prime, fs, F_c2](yield_t &yield)
            // If in Case 2, set fs. We are now finding successor
            { mpc_or(tio, yield, fs_prime, fs, F_c2);});

        // If in Case 4. Successor found here already. Toggle fs off
        fs_prime=fs_prime^F_c4;

        bool key_found = del(tio, yield, next_ptr, del_key, A, af_prime, fs_prime, TTL-1, ret_struct);

        // If we didn't find the key, we can end here.
        if(!key_found) {
            return 0;
        }

        //printf("TTL = %d\n", TTL);
        RegBS F_rs_right, F_rs_left, not_c_prime=c_prime;
        if(player0) {
            not_c_prime^=1;
        }
        // Flag here should be direction (c_prime) and F_r i.e. we need to swap return ptr in,
        // F_r needs to be returned in ret_struct
        run_coroutines(tio,
            [&tio, &F_rs_right, c_prime, ret_struct](yield_t &yield)
            { mpc_and(tio, yield, F_rs_right, c_prime, ret_struct.F_r);},
            [&tio, &F_rs_left, not_c_prime, left, ret_struct](yield_t &yield)
            { mpc_and(tio, yield, F_rs_left, not_c_prime, ret_struct.F_r);});

        run_coroutines(tio,
            [&tio, &right, F_rs_right, ret_struct](yield_t &yield)
            { mpc_select(tio, yield, right, F_rs_right, right, ret_struct.ret_ptr);},
            [&tio, &left, F_rs_left, ret_struct](yield_t &yield)
            { mpc_select(tio, yield, left, F_rs_left, left, ret_struct.ret_ptr);});

        /*
        // Reconstruct and Debug Block 3
        bool F_rs_rec, F_ls_rec;
        size_t ret_ptr_rec;
        F_rs_rec = mpc_reconstruct(tio, yield, F_rs);
        F_ls_rec = mpc_reconstruct(tio, yield, F_rs);
        ret_ptr_rec = mpc_reconstruct(tio, yield, ret_struct.ret_ptr);
        printf("F_rs_rec = %d, F_ls_rec = %d, ret_ptr_rec = %ld\n", F_rs_rec, F_ls_rec, ret_ptr_rec);
        */
        RegXS new_ptr;
        setLeftPtr(new_ptr, left);
        setRightPtr(new_ptr, right);
        A[ptr].NODE_POINTERS = new_ptr;

        // Update the return structure
        RegBS F_nd, F_ns, F_r, not_af = af, not_F_2 = F_2;
        if(player0) {
            not_af^=1;
            not_F_2^=1;
        }
        // F_ns = fs & l0
        // Finding successor flag & no more left child = F_c4
        F_ns = F_c4;

        run_coroutines(tio,
            [&tio, &ret_struct, F_c2](yield_t &yield)
            { mpc_or(tio, yield, ret_struct.F_ss, ret_struct.F_ss, F_c2);},
            [&tio, &F_nd, lf, not_af](yield_t &yield)
            { mpc_and(tio, yield, F_nd, lf, not_af);});


        // F_r = F_d.(!F_2)
        // If we have to delete here, and it doesn't have two children we have to
        // update child pointer in parent with the returned pointer
        mpc_and(tio, yield, F_r, F_nd, not_F_2);
        mpc_or(tio, yield, F_r, F_r, F_ns);
        ret_struct.F_r = F_r;

        run_coroutines(tio,
            [&tio, &ret_struct, F_nd, ptr](yield_t &yield)
            { mpc_select(tio, yield, ret_struct.N_d, F_nd, ret_struct.N_d, ptr);},
            [&tio, &ret_struct, F_ns, ptr](yield_t &yield)
            { mpc_select(tio, yield, ret_struct.N_s, F_ns, ret_struct.N_s, ptr);},
            [&tio, &ret_struct, F_r, ptr](yield_t &yield)
            { mpc_select(tio, yield, ret_struct.ret_ptr, F_r, ptr, ret_struct.ret_ptr);});

        //We don't empty the key and value of the node with del_key in the ORAM
        return 1;
    }
}

/*
    The main del() function.
    Trying to delete an item that does not exist in the tree will result in
    an explicit (non-oblivious) failure.

    Takes as input the key to delete (del_key).
    Returns success/fail bit.
*/

bool BST::del(MPCTIO &tio, yield_t &yield, RegAS del_key) {
    if(num_items==0)
        return 0;
    if(num_items==1) {
        //Delete root
        auto A = oram.flat(tio, yield);
        Node zero;
        empty_locations.emplace_back(root);
        A[root] = zero;
        num_items--;
        return 1;
    } else {
        int TTL = num_items;
        // Flags for already found (af) item to delete and find successor (fs)
        // if this deletion requires a successor swap
        RegBS af;
        RegBS fs;
        del_return ret_struct;
        auto A = oram.flat(tio, yield);
        int success = del(tio, yield, root, del_key, A, af, fs, TTL, ret_struct);
        if(!success){
            return 0;
        }
        else{
            num_items--;
            /*
            printf("In delete's swap portion\n");
            Node del_node = A.reconstruct(A[ret_struct.N_d]);
            Node suc_node = A.reconstruct(A[ret_struct.N_s]);
            printf("del_node key = %ld, suc_node key = %ld\n",
                del_node.key.ashare, suc_node.key.ashare);
            printf("flag_s = %d\n", ret_struct.F_ss.bshare);
            */
            Node del_node = A[ret_struct.N_d];
            Node suc_node = A[ret_struct.N_s];
            RegAS zero_as; RegXS zero_xs;
            RegXS empty_loc, temp_root = root;

            run_coroutines(tio,
                [&tio, &temp_root, ret_struct](yield_t &yield)
                { mpc_select(tio, yield, temp_root, ret_struct.F_r, temp_root, ret_struct.ret_ptr);},
                [&tio, &del_node, ret_struct, suc_node](yield_t &yield)
                { mpc_select(tio, yield, del_node.key, ret_struct.F_ss, del_node.key, suc_node.key);},
                [&tio, &del_node, ret_struct, suc_node](yield_t & yield)
                { mpc_select(tio, yield, del_node.value, ret_struct.F_ss, del_node.value, suc_node.value);},
                [&tio, &empty_loc, ret_struct](yield_t &yield)
                { mpc_select(tio, yield, empty_loc, ret_struct.F_ss, ret_struct.N_d, ret_struct.N_s);});
            root = temp_root;

            run_coroutines(tio,
                [&tio, &A, ret_struct, del_node](yield_t &yield)
                {   auto acont = A.context(yield);
                    acont[ret_struct.N_d].NODE_KEY = del_node.key;},
                [&tio, &A, ret_struct, del_node](yield_t &yield)
                {   auto acont = A.context(yield);
                    acont[ret_struct.N_d].NODE_VALUE = del_node.value;},
                [&tio, &A, ret_struct, zero_as](yield_t &yield)
                {   auto acont = A.context(yield);
                    acont[ret_struct.N_s].NODE_KEY = zero_as;},
                [&tio, &A, ret_struct, zero_xs](yield_t &yield)
                {   auto acont = A.context(yield);
                    acont[ret_struct.N_s].NODE_VALUE = zero_xs;});

            //Add deleted (empty) location into the empty_locations vector for reuse in next insert()
            empty_locations.emplace_back(empty_loc);
        }

      return 1;
    }
}

// Now we use the node in various ways.  This function is called by
// online.cpp.
void bst(MPCIO &mpcio,
    const PRACOptions &opts, char **args)
{
    nbits_t depth=4;

    if (*args) {
        depth = atoi(*args);
        ++args;
    }

    MPCTIO tio(mpcio, 0, opts.num_cpu_threads);
    run_coroutines(tio, [&tio, depth] (yield_t &yield) {
        size_t size = size_t(1)<<depth;
        BST tree(tio.player(), size);

        int insert_array[] = {10, 10, 13, 11, 14, 8, 15, 20, 17, 19, 7, 12};
        //int insert_array[] = {1, 2, 3, 4, 5, 6};
        size_t insert_array_size = sizeof(insert_array)/sizeof(int);
        Node node;
        for(size_t i = 0; i<insert_array_size; i++) {
          randomize_node(node);
          node.key.set(insert_array[i] * tio.player());
          tree.insert(tio, yield, node);
        }

        tree.pretty_print(tio, yield);

        RegAS del_key;

        printf("\n\nDelete %x\n", 20);
        del_key.set(20 * tio.player());
        tree.del(tio, yield, del_key);
        tree.pretty_print(tio, yield);
        tree.check_bst(tio, yield);

        printf("\n\nDelete %x\n", 10);
        del_key.set(10 * tio.player());
        tree.del(tio, yield, del_key);
        tree.pretty_print(tio, yield);
        tree.check_bst(tio, yield);

        printf("\n\nDelete %x\n", 7);
        del_key.set(7 * tio.player());
        tree.del(tio, yield, del_key);
        tree.pretty_print(tio, yield);
        tree.check_bst(tio, yield);

        printf("\n\nDelete %x\n", 17);
        del_key.set(17 * tio.player());
        tree.del(tio, yield, del_key);
        tree.pretty_print(tio, yield);
        tree.check_bst(tio, yield);

        printf("\n\nDelete %x\n", 15);
        del_key.set(15 * tio.player());
        tree.del(tio, yield, del_key);
        tree.pretty_print(tio, yield);
        tree.check_bst(tio, yield);

        printf("\n\nDelete %x\n", 5);
        del_key.set(5 * tio.player());
        tree.del(tio, yield, del_key);
        tree.pretty_print(tio, yield);
        tree.check_bst(tio, yield);

        printf("\n\nInsert %x\n", 14);
        randomize_node(node);
        node.key.set(14 * tio.player());
        tree.insert(tio, yield, node);
        tree.pretty_print(tio, yield);
        tree.check_bst(tio, yield);

        printf("\n\nLookup %x\n", 8);
        randomize_node(node);
        RegAS lookup_key;
        RegBS found;
        bool rec_found;
        lookup_key.set(8 * tio.player());
        found = tree.lookup(tio, yield, lookup_key, &node);
        rec_found = mpc_reconstruct(tio, yield, found);
        tree.pretty_print(tio, yield);
        if(tio.player()!=2) {
            if(rec_found) {
                printf("Lookup Success\n");
                size_t value = mpc_reconstruct(tio, yield, node.value);
                printf("value = %lx\n", value);
            } else {
                printf("Lookup Failed\n");
            }
        }
    });
}
