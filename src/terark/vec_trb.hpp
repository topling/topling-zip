#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iterator>
#include <map>
#include <new>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

#include "stdtypes.hpp"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wclass-memaccess"
#endif

// A Unit contains {Node,Data{Key,Value}}
// A Node contains {left{tag,link},right{tag,link},color,waste}
// {Node,Data,Key,Value} can be stored as packed or seperated

template <class Uidx>
struct vec_trb_node_t {
  typedef Uidx index_type;

  Uidx children[2];

  vec_trb_node_t(Uidx left, Uidx right) {
    children[0] = left;
    children[1] = right;
  }
  vec_trb_node_t() {}

  static size_t constexpr index_bits = 8 * sizeof(Uidx);
  static size_t constexpr flag_bit_mask = Uidx(1) << (index_bits - 1);
  static size_t constexpr type_bit_mask = Uidx(1) << (index_bits - 2);
  static size_t constexpr full_bit_mask = flag_bit_mask | type_bit_mask;
  static size_t constexpr nil_sentinel = ~Uidx(0) & ~full_bit_mask;

  bool left_is_child() const { return (children[0] & type_bit_mask) == 0; }

  bool right_is_child() const { return (children[1] & type_bit_mask) == 0; }

  bool left_is_thread() const { return (children[0] & type_bit_mask) != 0; }

  bool right_is_thread() const { return (children[1] & type_bit_mask) != 0; }

  void left_set_child() { children[0] &= ~type_bit_mask; }

  void right_set_child() { children[1] &= ~type_bit_mask; }

  void left_set_thread() { children[0] |= type_bit_mask; }

  void right_set_thread() { children[1] |= type_bit_mask; }

  void left_set_link(size_t link) {
    children[0] = Uidx((children[0] & full_bit_mask) | link);
  }

  void right_set_link(size_t link) {
    children[1] = Uidx((children[1] & full_bit_mask) | link);
  }

  size_t left_get_link() const { return children[0] & ~full_bit_mask; }

  size_t right_get_link() const { return children[1] & ~full_bit_mask; }

  bool is_used() const { return (children[1] & flag_bit_mask) == 0; }

  void set_used() { children[1] &= ~flag_bit_mask; }

  bool is_empty() const { return (children[1] & flag_bit_mask) != 0; }

  void set_empty() { children[1] |= flag_bit_mask; }

  bool is_black() const { return (children[0] & flag_bit_mask) == 0; }

  void set_black() { children[0] &= ~flag_bit_mask; }

  bool is_red() const { return (children[0] & flag_bit_mask) != 0; }

  void set_red() { children[0] |= flag_bit_mask; }
};

template <class Accessor>
size_t vec_trb_move_next(size_t node, Accessor access) {
  if (access(node).right_is_thread()) {
    return access(node).right_get_link();
  } else {
    node = access(node).right_get_link();
    while (access(node).left_is_child()) {
      node = access(node).left_get_link();
    }
    return node;
  }
}

template <class Accessor>
size_t vec_trb_move_prev(size_t node, Accessor access) {
  if (access(node).left_is_thread()) {
    return access(node).left_get_link();
  } else {
    node = access(node).left_get_link();
    while (access(node).right_is_child()) {
      node = access(node).right_get_link();
    }
    return node;
  }
}

template <class Node, class EnableCount>
struct vec_trb_count_t {
  typedef Node count_node_type;
  typedef typename count_node_type::index_type count_index_type;
  count_index_type count;
  vec_trb_count_t() : count(0) {}
  void increase_count() { ++count; }
  void decrease_count() { --count; }
  void reset() { count = 0; }
  size_t get_count() const { return count; }
};

template <class Node>
struct vec_trb_count_t<Node, std::false_type> {
  typedef Node count_node_type;
  void increase_count() {}
  void decrease_count() {}
  void reset() {}
  size_t get_count() const { return 0; }
};

template <class Node, class EnableMost>
struct vec_trb_most_t {
  typedef Node most_node_type;
  typedef typename most_node_type::index_type most_index_type;

  most_index_type left, right;

  vec_trb_most_t()
      : left(most_node_type::nil_sentinel),
        right(most_node_type::nil_sentinel) {}

  template <class Accessor>
  size_t get_left(size_t, Accessor) const {
    return left;
  }

  template <class Accessor>
  size_t get_right(size_t, Accessor) const {
    return right;
  }

  void set_left(size_t value) { left = most_index_type(value); }

  void set_right(size_t value) { right = most_index_type(value); }

  void update_left(size_t check, size_t value) {
    if (check == left) {
      left = most_index_type(value);
    }
  }

  void update_right(size_t check, size_t value) {
    if (check == right) {
      right = most_index_type(value);
    }
  }

  template <class Accessor>
  void detach_left(size_t value, Accessor access) {
    if (value == left) {
      left = most_index_type(vec_trb_move_next(left, access));
    }
  }

  template <class Accessor>
  void detach_right(size_t value, Accessor access) {
    if (value == right) {
      right = most_index_type(vec_trb_move_prev(right, access));
    }
  }
};

template <class Node>
struct vec_trb_most_t<Node, std::false_type> {
  typedef Node most_node_type;

  template <class Accessor>
  size_t get_left(size_t root, Accessor access) const {
    size_t node = root;
    if (root != most_node_type::nil_sentinel) {
      while (access(node).left_is_child()) {
        node = access(node).left_get_link();
      }
    }
    return node;
  }

  template <class Accessor>
  size_t get_right(size_t root, Accessor access) const {
    size_t node = root;
    if (root != most_node_type::nil_sentinel) {
      while (access(node).right_is_child()) {
        node = access(node).right_get_link();
      }
    }
    return node;
  }

  void set_left(size_t) {}
  void set_right(size_t) {}
  void update_left(size_t, size_t) {}
  void update_right(size_t, size_t) {}

  template <class Accessor>
  void detach_left(size_t, Accessor) {}

  template <class Accessor>
  void detach_right(size_t, Accessor) {}
};

template <class Node, class EnableCount, class EnableMost>
struct vec_trb_root_t {
  typedef Node node_type;
  typedef typename node_type::index_type index_type;

  vec_trb_root_t() { root.root = node_type::nil_sentinel; }

  struct root_type : public vec_trb_count_t<Node, EnableCount>,
                     public vec_trb_most_t<Node, EnableMost> {
    index_type root;
  } root;

  size_t get_count() const { return root.get_count(); }

  template <class Accessor>
  size_t get_most_left(Accessor access) const {
    return root.get_left(root.root, access);
  }

  template <class Accessor>
  size_t get_most_right(Accessor access) const {
    return root.get_right(root.root, access);
  }
};

template <class Node, size_t MaxDepth>
struct vec_trb_stack_t {
  typedef Node node_type;
  typedef typename Node::index_type index_type;
  typedef decltype(Node::flag_bit_mask) mask_type;

  static mask_type constexpr dir_bit_mask = Node::flag_bit_mask;

  size_t height;
  index_type stack[MaxDepth];

  bool is_left(size_t k) const {
    assert(k < MaxDepth);
    return (mask_type(stack[k]) & dir_bit_mask) == 0;
  }

  bool is_right(size_t k) const {
    assert(k < MaxDepth);
    return (mask_type(stack[k]) & dir_bit_mask) != 0;
  }

  size_t get_index(size_t k) const {
    assert(k < MaxDepth);
    return index_type(mask_type(stack[k]) & ~dir_bit_mask);
  }

  void push_index(size_t index, bool left) {
    TERARK_VERIFY_LT(height, MaxDepth);
    stack[height++] = index_type(mask_type(index) | (left ? 0 : dir_bit_mask));
  }

  void update_index(size_t k, size_t index, bool left) {
    assert(k < MaxDepth);
    stack[k] = index_type(mask_type(index) | (left ? 0 : dir_bit_mask));
  }

  void update_index(size_t k, size_t index) {
    assert(k < MaxDepth);
    stack[k] = index_type(index | (stack[k] & dir_bit_mask));
  }
};

namespace threded_rb_tree_tools {
template <typename U, typename C, int (U::*)(C const &, C const &) const>
struct check_compare_1;
template <typename U, typename C>
std::true_type check_compare(check_compare_1<U, C, &U::compare> *) {
  return std::true_type();
}
template <typename U, typename C, int (U::*)(C, C) const>
struct check_compare_2;
template <typename U, typename C>
std::true_type check_compare(check_compare_2<U, C, &U::compare> *) {
  return std::true_type();
}
template <typename U, typename C>
std::false_type check_compare(...) {
  return std::false_type();
}
template <typename U, typename C>
struct has_compare : public decltype(check_compare<U, C>(nullptr)) {};
}  // namespace threded_rb_tree_tools

template <class Root, class Cmp, class NodeAccessor, size_t MaxDepth>
void vec_trb_find_path_for_multi(
    Root &root, vec_trb_stack_t<typename Root::node_type, MaxDepth> &stack,
    NodeAccessor access, size_t index, Cmp cmp) {
  typedef typename Root::node_type node_type;

  stack.height = 0;
  size_t p = root.root.root;
  while (p != node_type::nil_sentinel) {
    if (cmp(index, p)) {
      stack.push_index(p, true);
      if (access(p).left_is_thread()) {
        return;
      }
      p = access(p).left_get_link();
    } else {
      stack.push_index(p, false);
      if (access(p).right_is_thread()) {
        return;
      }
      p = access(p).right_get_link();
    }
  }
}

/*
template <class Root, class Cmp, class Key, class NodeAccessor,
          class KeyAccessor, size_t MaxDepth>
void vec_trb_find_path_for_multi(
    Root &root, vec_trb_stack_t<typename Root::node_type, MaxDepth> &stack,
    NodeAccessor access, Key const &key, KeyAccessor getkey, Cmp cmp) {
  typedef typename Root::node_type node_type;

  stack.height = 0;
  size_t p = root.root.root;
  while (p != node_type::nil_sentinel) {
    if (cmp(key, getkey(p))) {
      stack.push_index(p, true);
      if (access(p).left_is_thread()) {
        return;
      }
      p = access(p).left_get_link();
    } else {
      stack.push_index(p, false);
      if (access(p).right_is_thread()) {
        return;
      }
      p = access(p).right_get_link();
    }
  }
}
*/

template <class Root, class Cmp, class Key, class Accessor, size_t MaxDepth>
bool vec_trb_find_path_for_unique(
    Root &root, vec_trb_stack_t<typename Root::node_type, MaxDepth> &stack,
    Accessor access, Key const &key, Cmp cmp) {
  typedef typename threded_rb_tree_tools::has_compare<Cmp, Key>::type cmp_3way;
  return vec_trb_find_path_for_unique(root, stack, access, key, cmp,
                                      cmp_3way());
}

template <class Root, class Cmp, class Key, class Accessor, size_t MaxDepth>
bool vec_trb_find_path_for_unique(
    Root &root, vec_trb_stack_t<typename Root::node_type, MaxDepth> &stack,
    Accessor access, Key const &key, Cmp cmp, std::false_type) {
  typedef typename Root::node_type node_type;

  stack.height = 0;
  size_t p = root.root.root;
  while (p != node_type::nil_sentinel) {
    if (cmp(key, access.key(p))) {
      stack.push_index(p, true);
      if (access.node(p).left_is_thread()) {
        return false;
      }
      p = access.node(p).left_get_link();
    } else {
      stack.push_index(p, false);
      if (!cmp(access.key(p), key)) {
        return true;
      }
      if (access.node(p).right_is_thread()) {
        return false;
      }
      p = access.node(p).right_get_link();
    }
  }
  return false;
}

template <class Root, class Cmp, class Key, class Accessor, size_t MaxDepth>
bool vec_trb_find_path_for_unique(
    Root &root, vec_trb_stack_t<typename Root::node_type, MaxDepth> &stack,
    Accessor access, Key const &key, Cmp cmp, std::true_type) {
  typedef typename Root::node_type node_type;

  stack.height = 0;
  size_t p = root.root.root;
  while (p != node_type::nil_sentinel) {
    int c = cmp.compare(key, access.key(p));
    if (c < 0) {
      stack.push_index(p, true);
      if (access.node(p).left_is_thread()) {
        return false;
      }
      p = access.node(p).left_get_link();
    } else {
      stack.push_index(p, false);
      if (c == 0) {
        return true;
      }
      if (access.node(p).right_is_thread()) {
        return false;
      }
      p = access.node(p).right_get_link();
    }
  }
  return false;
}

template <class Root, class Cmp, class NodeAccessor, size_t MaxDepth>
bool vec_trb_find_path_for_remove(
    Root &root, vec_trb_stack_t<typename Root::node_type, MaxDepth> &stack,
    NodeAccessor access, size_t index, Cmp cmp) {
  typedef
      typename threded_rb_tree_tools::has_compare<Cmp, size_t>::type cmp_3way;
  return vec_trb_find_path_for_remove(root, stack, access, index, cmp,
                                      cmp_3way());
}

template <class Root, class Cmp, class NodeAccessor, size_t MaxDepth>
bool vec_trb_find_path_for_remove(
    Root &root, vec_trb_stack_t<typename Root::node_type, MaxDepth> &stack,
    NodeAccessor access, size_t index, Cmp cmp, std::false_type) {
  typedef typename Root::node_type node_type;

  stack.height = 0;
  size_t p = root.root.root;
  while (p != node_type::nil_sentinel) {
    if (cmp(index, p)) {
      stack.push_index(p, true);
      if (access(p).left_is_thread()) {
        return false;
      }
      p = access(p).left_get_link();
    } else {
      stack.push_index(p, false);
      if (!cmp(p, index)) {
        return true;
      }
      if (access(p).right_is_thread()) {
        return false;
      }
      p = access(p).right_get_link();
    }
  }
  return false;
}

template <class Root, class Cmp, class NodeAccessor, size_t MaxDepth>
bool vec_trb_find_path_for_remove(
    Root &root, vec_trb_stack_t<typename Root::node_type, MaxDepth> &stack,
    NodeAccessor access, size_t index, Cmp cmp, std::true_type) {
  typedef typename Root::node_type node_type;

  stack.height = 0;
  size_t p = root.root.root;
  while (p != node_type::nil_sentinel) {
    int c = cmp.compare(index, p);
    if (c < 0) {
      stack.push_index(p, true);
      if (access(p).left_is_thread()) {
        return false;
      }
      p = access(p).left_get_link();
    } else {
      stack.push_index(p, false);
      if (c == 0) {
        return true;
      }
      if (access(p).right_is_thread()) {
        return false;
      }
      p = access(p).right_get_link();
    }
  }
  return false;
}

template <class Root, class Cmp, class Key, class Accessor>
size_t vec_trb_lower_bound(Root &root, Accessor access, Key const &key,
                           Cmp cmp) {
  typedef typename Root::node_type node_type;

  size_t p = root.root.root, w = node_type::nil_sentinel;
  while (p != node_type::nil_sentinel) {
    if (cmp(access.key(p), key)) {
      if (access.node(p).right_is_thread()) {
        break;
      }
      p = access.node(p).right_get_link();
    } else {
      w = p;
      if (access.node(p).left_is_thread()) {
        break;
      }
      p = access.node(p).left_get_link();
    }
  }
  return w;
}
template <class Root, class Cmp, class Key, class Accessor>
size_t vec_trb_equal_unique(Root &root, Accessor access, Key const &key,
                            Cmp cmp) {
  typedef
      typename threded_rb_tree_tools::has_compare<Cmp, size_t>::type cmp_3way;
  return vec_trb_equal_unique(root, access, key, cmp, cmp_3way());
}
template <class Root, class Cmp, class Key, class Accessor>
size_t vec_trb_equal_unique(Root &root, Accessor access, Key const &key,
                            Cmp cmp, std::false_type) {
  typedef typename Root::node_type node_type;

  size_t p = root.root.root;
  while (p != node_type::nil_sentinel) {
    if (cmp(access.key(p), key)) {
      if (access.node(p).right_is_thread()) {
        return node_type::nil_sentinel;
      }
      p = access.node(p).right_get_link();
    } else {
      if (!cmp(key, access.key(p))) {
        return p;
      }
      if (access.node(p).left_is_thread()) {
        return node_type::nil_sentinel;
      }
      p = access.node(p).left_get_link();
    }
  }
  return node_type::nil_sentinel;
}
template <class Root, class Cmp, class Key, class Accessor>
size_t vec_trb_equal_unique(Root &root, Accessor access, Key const &key,
                            Cmp cmp, std::true_type) {
  typedef typename Root::node_type node_type;

  size_t p = root.root.root;
  while (p != node_type::nil_sentinel) {
    int c = cmp.compare(access.key(p), key);
    if (c < 0) {
      if (access.node(p).right_is_thread()) {
        return node_type::nil_sentinel;
      }
      p = access.node(p).right_get_link();
    } else {
      if (c == 0) {
        return p;
      }
      if (access.node(p).left_is_thread()) {
        return node_type::nil_sentinel;
      }
      p = access.node(p).left_get_link();
    }
  }
  return node_type::nil_sentinel;
}
template <class Root, class Cmp, class Key, class Accessor>
size_t vec_trb_reverse_lower_bound(Root &root, Accessor access, Key const &key,
                                   Cmp cmp) {
  typedef typename Root::node_type node_type;

  size_t p = root.root.root, w = node_type::nil_sentinel;
  while (p != node_type::nil_sentinel) {
    if (cmp(key, access.key(p))) {
      if (access.node(p).left_is_thread()) {
        break;
      }
      p = access.node(p).left_get_link();
    } else {
      w = p;
      if (access.node(p).right_is_thread()) {
        break;
      }
      p = access.node(p).right_get_link();
    }
  }
  return w;
}

template <class Root, class Cmp, class Key, class Accessor>
size_t vec_trb_upper_bound(Root &root, Accessor access, Key const &key,
                           Cmp cmp) {
  typedef typename Root::node_type node_type;

  size_t p = root.root.root, w = node_type::nil_sentinel;
  while (p != node_type::nil_sentinel) {
    if (cmp(key, access.key(p))) {
      w = p;
      if (access.node(p).left_is_thread()) {
        break;
      }
      p = access.node(p).left_get_link();
    } else {
      if (access.node(p).right_is_thread()) {
        break;
      }
      p = access.node(p).right_get_link();
    }
  }
  return w;
}
template <class Root, class Cmp, class Key, class Accessor>
size_t vec_trb_reverse_upper_bound(Root &root, Accessor access, Key const &key,
                                   Cmp cmp) {
  typedef typename Root::node_type node_type;

  size_t p = root.root.root, w = node_type::nil_sentinel;
  while (p != node_type::nil_sentinel) {
    if (cmp(access.key(p), key)) {
      w = p;
      if (access.node(p).right_is_thread()) {
        break;
      }
      p = access.node(p).right_get_link();
    } else {
      if (access.node(p).left_is_thread()) {
        break;
      }
      p = access.node(p).left_get_link();
    }
  }
  return w;
}

template <class Root, class Cmp, class Key, class Accessor>
std::pair<size_t, size_t> vec_trb_equal_range(Root &root, Accessor access,
                                              Key const &key, Cmp cmp) {
  typedef typename Root::node_type node_type;

  size_t p = root.root.root;
  size_t lower = node_type::nil_sentinel;
  size_t upper = node_type::nil_sentinel;
  while (p != node_type::nil_sentinel) {
    if (cmp(access.key(p), key)) {
      if (access.node(p).right_is_thread()) {
        break;
      }
      p = access.node(p).right_get_link();
    } else {
      if (upper == node_type::nil_sentinel && cmp(key, access.key(p))) {
        upper = p;
      }
      lower = p;
      if (access.node(p).left_is_thread()) {
        break;
      }
      p = access.node(p).left_get_link();
    }
  }
  p = upper == node_type::nil_sentinel     ? root.root.root
      : access.node(upper).left_is_child() ? access.node(upper).left_get_link()
                                           : node_type::nil_sentinel;
  while (p != node_type::nil_sentinel) {
    if (cmp(key, access.key(p))) {
      upper = p;
      if (access.node(p).left_is_thread()) {
        break;
      }
      p = access.node(p).left_get_link();
    } else {
      if (access.node(p).right_is_thread()) {
        break;
      }
      p = access.node(p).right_get_link();
    }
  }
  return std::make_pair(lower, upper);
}

template <class Root, class Cmp, class Key, class Accessor>
void vec_trb_reverse_equal_range(Root &root, Accessor access, Key const &key,
                                 Cmp cmp, size_t &lower, size_t &upper) {
  typedef typename Root::node_type node_type;

  size_t p = root.root.root;
  lower = node_type::nil_sentinel;
  upper = node_type::nil_sentinel;
  while (p != node_type::nil_sentinel) {
    if (cmp(key, access.key(p))) {
      if (access.node(p).left_is_thread()) {
        break;
      }
      p = access.node(p).left_get_link();
    } else {
      if (upper == node_type::nil_sentinel && cmp(access.key(p), key)) {
        upper = p;
      }
      lower = p;
      if (access.node(p).right_is_thread()) {
        break;
      }
      p = access.node(p).right_get_link();
    }
  }
  p = upper == node_type::nil_sentinel      ? root.root.root
      : access.node(upper).right_is_child() ? access(upper).right_get_link()
                                            : node_type::nil_sentinel;
  while (p != node_type::nil_sentinel) {
    if (cmp(access.key(p), key)) {
      upper = p;
      if (access.node(p).right_is_thread()) {
        break;
      }
      p = access.node(p).right_get_link();
    } else {
      if (access.node(p).left_is_thread()) {
        break;
      }
      p = access.node(p).left_get_link();
    }
  }
}
template <class Root, class Accessor, size_t MaxDepth>
void vec_trb_insert(Root &root,
                    vec_trb_stack_t<typename Root::node_type, MaxDepth> &stack,
                    Accessor access, size_t index) {
  typedef typename Root::node_type node_type;
  typedef typename Root::index_type index_type;

  root.root.increase_count();
  access(index).set_used();
  access(index).left_set_thread();
  access(index).right_set_thread();

  if (stack.height == 0) {
    access(index).left_set_link(index_type(node_type::nil_sentinel));
    access(index).right_set_link(index_type(node_type::nil_sentinel));
    access(index).set_black();
    root.root.root = index_type(index);
    root.root.set_left(index);
    root.root.set_right(index);
    return;
  }
  access(index).set_red();
  size_t k = stack.height - 1;
  size_t where = stack.get_index(k);
  if (stack.is_left(k)) {
    access(index).left_set_link(access(where).left_get_link());
    access(index).right_set_link(where);
    access(where).left_set_child();
    access(where).left_set_link(index);
    root.root.update_left(where, index);
  } else {
    access(index).right_set_link(access(where).right_get_link());
    access(index).left_set_link(where);
    access(where).right_set_child();
    access(where).right_set_link(index);
    root.root.update_right(where, index);
  }
  if (k >= 1) {
    while (access(stack.get_index(k)).is_red()) {
      size_t p2 = stack.get_index(k - 1);
      size_t p1 = stack.get_index(k);
      if (stack.is_left(k - 1)) {
        size_t u = access(p2).right_get_link();
        if (access(p2).right_is_child() && access(u).is_red()) {
          access(p1).set_black();
          access(u).set_black();
          access(p2).set_red();
          if (k < 2) {
            break;
          }
          k -= 2;
        } else {
          size_t y;
          if (stack.is_left(k)) {
            y = p1;
          } else {
            y = access(p1).right_get_link();
            access(p1).right_set_link(access(y).left_get_link());
            access(y).left_set_link(p1);
            access(p2).left_set_link(y);
            if (access(y).left_is_thread()) {
              access(y).left_set_child();
              access(p1).right_set_thread();
              access(p1).right_set_link(y);
            }
          }
          access(p2).set_red();
          access(y).set_black();
          access(p2).left_set_link(access(y).right_get_link());
          access(y).right_set_link(p2);
          if (k == 1) {
            root.root.root = index_type(y);
          } else if (stack.is_left(k - 2)) {
            access(stack.get_index(k - 2)).left_set_link(y);
          } else {
            access(stack.get_index(k - 2)).right_set_link(y);
          }
          if (access(y).right_is_thread()) {
            access(y).right_set_child();
            access(p2).left_set_thread();
            access(p2).left_set_link(y);
          }
          assert(access(p2).right_get_link() == u);
          break;
        }
      } else {
        size_t u = access(p2).left_get_link();
        if (access(p2).left_is_child() && access(u).is_red()) {
          access(p1).set_black();
          access(u).set_black();
          access(p2).set_red();
          if (k < 2) {
            break;
          }
          k -= 2;
        } else {
          size_t y;
          if (stack.is_right(k)) {
            y = p1;
          } else {
            y = access(p1).left_get_link();
            access(p1).left_set_link(access(y).right_get_link());
            access(y).right_set_link(p1);
            access(p2).right_set_link(y);
            if (access(y).right_is_thread()) {
              access(y).right_set_child();
              access(p1).left_set_thread();
              access(p1).left_set_link(y);
            }
          }
          access(p2).set_red();
          access(y).set_black();
          access(p2).right_set_link(access(y).left_get_link());
          access(y).left_set_link(p2);
          if (k == 1) {
            root.root.root = index_type(y);
          } else if (stack.is_right(k - 2)) {
            access(stack.get_index(k - 2)).right_set_link(y);
          } else {
            access(stack.get_index(k - 2)).left_set_link(y);
          }
          if (access(y).left_is_thread()) {
            access(y).left_set_child();
            access(p2).right_set_thread();
            access(p2).right_set_link(y);
          }
          assert(access(p2).left_get_link() == u);
          break;
        }
      }
    }
  }
  access(root.root.root).set_black();
}

template <class Root, class Accessor, size_t MaxDepth>
void vec_trb_remove(Root &root,
                    vec_trb_stack_t<typename Root::node_type, MaxDepth> &stack,
                    Accessor access) {
  typedef typename Root::node_type node_type;
  typedef typename Root::index_type index_type;

  assert(stack.height > 0);
  root.root.decrease_count();
  size_t k = stack.height - 1;
  size_t p = stack.get_index(k);
  access(p).set_empty();
  root.root.detach_left(p, access);
  root.root.detach_right(p, access);
  if (access(p).right_is_thread()) {
    if (access(p).left_is_child()) {
      size_t t = access(p).left_get_link();
      while (access(t).right_is_child()) {
        t = access(t).right_get_link();
      }
      access(t).right_set_link(access(p).right_get_link());
      if (k == 0) {
        root.root.root = index_type(access(p).left_get_link());
      } else if (stack.is_left(k - 1)) {
        access(stack.get_index(k - 1)).left_set_link(access(p).left_get_link());
      } else {
        access(stack.get_index(k - 1))
            .right_set_link(access(p).left_get_link());
      }
    } else {
      if (k == 0) {
        root.root.root = index_type(access(p).left_get_link());
      } else if (stack.is_left(k - 1)) {
        access(stack.get_index(k - 1)).left_set_link(access(p).left_get_link());
        access(stack.get_index(k - 1)).left_set_thread();
      } else {
        access(stack.get_index(k - 1))
            .right_set_link(access(p).right_get_link());
        access(stack.get_index(k - 1)).right_set_thread();
      }
    }
  } else {
    size_t r = access(p).right_get_link();
    if (access(r).left_is_thread()) {
      access(r).left_set_link(access(p).left_get_link());
      if (access(p).left_is_child()) {
        access(r).left_set_child();
        size_t t = access(p).left_get_link();
        while (access(t).right_is_child()) {
          t = access(t).right_get_link();
        }
        access(t).right_set_link(r);
      } else {
        access(r).left_set_thread();
      }
      if (k == 0) {
        root.root.root = index_type(r);
      } else if (stack.is_left(k - 1)) {
        access(stack.get_index(k - 1)).left_set_link(r);
      } else {
        access(stack.get_index(k - 1)).right_set_link(r);
      }
      bool is_red = access(r).is_red();
      if (access(p).is_red()) {
        access(r).set_red();
      } else {
        access(r).set_black();
      }
      if (is_red) {
        access(p).set_red();
      } else {
        access(p).set_black();
      }
      stack.update_index(k, r, false);
      ++k;
    } else {
      size_t s;
      size_t const j = stack.height - 1;
      for (++k;;) {
        stack.update_index(k, r, true);
        ++k;
        s = access(r).left_get_link();
        if (access(s).left_is_thread()) {
          break;
        }
        r = s;
      }
      assert(stack.get_index(j) == p);
      assert(j == stack.height - 1);
      stack.update_index(j, s, false);
      if (access(s).right_is_child()) {
        access(r).left_set_link(access(s).right_get_link());
      } else {
        assert(access(r).left_get_link() == s);
        access(r).left_set_thread();
      }
      access(s).left_set_link(access(p).left_get_link());
      if (access(p).left_is_child()) {
        size_t t = access(p).left_get_link();
        while (access(t).right_is_child()) {
          t = access(t).right_get_link();
        }
        access(t).right_set_link(s);
        access(s).left_set_child();
      }
      access(s).right_set_link(access(p).right_get_link());
      access(s).right_set_child();
      bool is_red = access(p).is_red();
      if (access(s).is_red()) {
        access(p).set_red();
      } else {
        access(p).set_black();
      }
      if (is_red) {
        access(s).set_red();
      } else {
        access(s).set_black();
      }
      if (j == 0) {
        root.root.root = index_type(s);
      } else if (stack.is_left(j - 1)) {
        access(stack.get_index(j - 1)).left_set_link(s);
      } else {
        access(stack.get_index(j - 1)).right_set_link(s);
      }
    }
  }
  if (access(p).is_black()) {
    for (; k > 0; --k) {
      if (stack.is_left(k - 1)) {
        if (access(stack.get_index(k - 1)).left_is_child()) {
          size_t x = access(stack.get_index(k - 1)).left_get_link();
          if (access(x).is_red()) {
            access(x).set_black();
            break;
          }
        }
        size_t w = access(stack.get_index(k - 1)).right_get_link();
        if (access(w).is_red()) {
          access(w).set_black();
          access(stack.get_index(k - 1)).set_red();
          access(stack.get_index(k - 1))
              .right_set_link(access(w).left_get_link());
          access(w).left_set_link(stack.get_index(k - 1));
          if (k == 1) {
            root.root.root = index_type(w);
          } else if (stack.is_left(k - 2)) {
            access(stack.get_index(k - 2)).left_set_link(w);
          } else {
            access(stack.get_index(k - 2)).right_set_link(w);
          }
          stack.update_index(k, stack.get_index(k - 1), true);
          stack.update_index(k - 1, w);
          w = access(stack.get_index(k)).right_get_link();
          ++k;
        }
        if ((access(w).left_is_thread() ||
             access(access(w).left_get_link()).is_black()) &&
            (access(w).right_is_thread() ||
             access(access(w).right_get_link()).is_black())) {
          access(w).set_red();
        } else {
          if (access(w).right_is_thread() ||
              access(access(w).right_get_link()).is_black()) {
            size_t y = access(w).left_get_link();
            access(y).set_black();
            access(w).set_red();
            access(w).left_set_link(access(y).right_get_link());
            access(y).right_set_link(w);
            access(stack.get_index(k - 1)).right_set_link(y);
            if (access(y).right_is_thread()) {
              size_t z = access(y).right_get_link();
              access(y).right_set_child();
              access(z).left_set_thread();
              access(z).left_set_link(y);
            }
            w = y;
          }
          if (access(stack.get_index(k - 1)).is_red()) {
            access(w).set_red();
          } else {
            access(w).set_black();
          }
          access(stack.get_index(k - 1)).set_black();
          access(access(w).right_get_link()).set_black();
          access(stack.get_index(k - 1))
              .right_set_link(access(w).left_get_link());
          access(w).left_set_link(stack.get_index(k - 1));
          if (k == 1) {
            root.root.root = index_type(w);
          } else if (stack.is_left(k - 2)) {
            access(stack.get_index(k - 2)).left_set_link(w);
          } else {
            access(stack.get_index(k - 2)).right_set_link(w);
          }
          if (access(w).left_is_thread()) {
            access(w).left_set_child();
            access(stack.get_index(k - 1)).right_set_thread();
            access(stack.get_index(k - 1)).right_set_link(w);
          }
          break;
        }
      } else {
        if (access(stack.get_index(k - 1)).right_is_child()) {
          size_t x = access(stack.get_index(k - 1)).right_get_link();
          if (access(x).is_red()) {
            access(x).set_black();
            break;
          }
        }
        size_t w = access(stack.get_index(k - 1)).left_get_link();
        if (access(w).is_red()) {
          access(w).set_black();
          access(stack.get_index(k - 1)).set_red();
          access(stack.get_index(k - 1))
              .left_set_link(access(w).right_get_link());
          access(w).right_set_link(stack.get_index(k - 1));
          if (k == 1) {
            root.root.root = index_type(w);
          } else if (stack.is_right(k - 2)) {
            access(stack.get_index(k - 2)).right_set_link(w);
          } else {
            access(stack.get_index(k - 2)).left_set_link(w);
          }
          stack.update_index(k, stack.get_index(k - 1), false);
          stack.update_index(k - 1, w);
          w = access(stack.get_index(k)).left_get_link();
          ++k;
        }
        if ((access(w).right_is_thread() ||
             access(access(w).right_get_link()).is_black()) &&
            (access(w).left_is_thread() ||
             access(access(w).left_get_link()).is_black())) {
          access(w).set_red();
        } else {
          if (access(w).left_is_thread() ||
              access(access(w).left_get_link()).is_black()) {
            size_t y = access(w).right_get_link();
            access(y).set_black();
            access(w).set_red();
            access(w).right_set_link(access(y).left_get_link());
            access(y).left_set_link(w);
            access(stack.get_index(k - 1)).left_set_link(y);
            if (access(y).left_is_thread()) {
              size_t z = access(y).left_get_link();
              access(y).left_set_child();
              access(z).right_set_thread();
              access(z).right_set_link(y);
            }
            w = y;
          }
          if (access(stack.get_index(k - 1)).is_red()) {
            access(w).set_red();
          } else {
            access(w).set_black();
          }
          access(stack.get_index(k - 1)).set_black();
          access(access(w).left_get_link()).set_black();
          access(stack.get_index(k - 1))
              .left_set_link(access(w).right_get_link());
          access(w).right_set_link(stack.get_index(k - 1));
          if (k == 1) {
            root.root.root = index_type(w);
          } else if (stack.is_right(k - 2)) {
            access(stack.get_index(k - 2)).right_set_link(w);
          } else {
            access(stack.get_index(k - 2)).left_set_link(w);
          }
          if (access(w).right_is_thread()) {
            access(w).right_set_child();
            access(stack.get_index(k - 1)).left_set_thread();
            access(stack.get_index(k - 1)).left_set_link(w);
          }
          break;
        }
      }
    }
    if (root.root.root != node_type::nil_sentinel) {
      access(root.root.root).set_black();
    }
  }
}

template <class config_t>
class vec_trb_impl {
 public:
  typedef typename config_t::key_type key_type;
  typedef typename config_t::mapped_type mapped_type;
  typedef typename config_t::value_type value_type;
  typedef typename config_t::storage_type storage_type;
  typedef typename config_t::vec_type vec_type;
  typedef size_t size_type;
  typedef std::ptrdiff_t difference_type;
  typedef typename config_t::key_compare key_compare;
  typedef value_type &reference;
  typedef value_type const &const_reference;
  typedef value_type *pointer;
  typedef value_type const *const_pointer;

 protected:
  typedef typename config_t::node_type node_type;
  typedef typename node_type::index_type index_type;

  typedef vec_trb_root_t<node_type, std::true_type, std::true_type> root_node_t;

  static size_type constexpr stack_max_depth = sizeof(index_type) * 12;

  struct Root
      : public vec_trb_root_t<node_type, std::true_type, std::true_type>,
        public key_compare {
    template <class any_key_compare, class any_container_type>
    Root(any_key_compare &&_comp, any_container_type &&_container)
        : key_compare(std::forward<any_key_compare>(_comp)),
          vec(std::forward<any_container_type>(_container)) {
      free = node_type::nil_sentinel;
    }

    vec_type vec;
    size_type free;
  };

  typedef typename config_t::UnitAccessor UnitAccessor;

  struct NodeAccessor {
    node_type &operator()(size_type index) const {
      return config_t::get_node(*container_ptr, index);
    }
    vec_type *container_ptr;
  };

  struct CNodeAccessor {
    node_type const &operator()(size_type index) const {
      return config_t::get_node(*container_ptr, index);
    }
    vec_type const *container_ptr;
  };

  struct KeyAccessor {
    key_type const &operator()(size_type index) const {
      return config_t::get_key(config_t::get_value(*container_ptr, index));
    }
    vec_type const *container_ptr;
  };

  template <class k_t, class v_t>
  struct get_key_select_t {
    key_type const &operator()(key_type const &x) const { return x; }
    key_type const &operator()(storage_type const &x) const {
      return config_t::get_key(x);
    }
    template <class first_t, class second_t>
    key_type operator()(std::pair<first_t, second_t> const &pair) {
      return key_type(pair.first);
    }
    template <class in_t, class... args_t>
    key_type operator()(in_t const &in, args_t const &...args) const {
      return key_type(in);
    }
  };

  template <class k_t>
  struct get_key_select_t<k_t, k_t> {
    key_type const &operator()(key_type const &value) const {
      return config_t::get_key(value);
    }
    template <class in_t, class... args_t>
    key_type operator()(in_t const &in, args_t const &...args) const {
      return key_type(in, args...);
    }
  };

  typedef get_key_select_t<key_type, storage_type> get_key_t;

  struct key_compare_ex {
    bool operator()(size_type left, size_type right) const {
      key_compare &compare = *root_ptr;
      auto &left_key =
          config_t::get_key(config_t::get_value(root_ptr->vec, left));
      auto &right_key =
          config_t::get_key(config_t::get_value(root_ptr->vec, right));
      if (compare(left_key, right_key)) {
        return true;
      } else if (compare(right_key, left_key)) {
        return false;
      } else {
        return left < right;
      }
    }

    Root *root_ptr;
  };

 public:
  class iterator {
   public:
    typedef std::bidirectional_iterator_tag iterator_category;
    typedef typename vec_trb_impl::value_type value_type;
    typedef typename vec_trb_impl::difference_type difference_type;
    typedef typename vec_trb_impl::reference reference;
    typedef typename vec_trb_impl::pointer pointer;

   public:
    explicit iterator(vec_trb_impl *_tree, size_type _where)
        : tree(_tree), where(_where) {}

    iterator(iterator const &) = default;

    iterator &operator++() {
      where = tree->next_i(where);
      return *this;
    }

    iterator &operator--() {
      if (where == node_type::nil_sentinel) {
        where = tree->root_.rbeg_i();
      } else {
        where = tree->prev_i(where);
      }
      return *this;
    }

    iterator operator++(int) {
      iterator save(*this);
      ++*this;
      return save;
    }

    iterator operator--(int) {
      iterator save(*this);
      --*this;
      return save;
    }

    reference operator*() const {
      return reinterpret_cast<reference>(
          config_t::get_value(tree->root_.vec, where));
    }

    pointer operator->() const {
      return reinterpret_cast<pointer>(
          &config_t::get_value(tree->root_.vec, where));
    }

    bool operator==(iterator const &other) const {
      assert(tree == other.tree);
      return where == other.where;
    }

    bool operator!=(iterator const &other) const {
      assert(tree == other.tree);
      return where != other.where;
    }

   private:
    friend class vec_trb_impl;

    vec_trb_impl *tree;
    size_type where;
  };

  class const_iterator {
   public:
    typedef std::bidirectional_iterator_tag iterator_category;
    typedef typename vec_trb_impl::value_type value_type;
    typedef typename vec_trb_impl::difference_type difference_type;
    typedef typename vec_trb_impl::reference reference;
    typedef typename vec_trb_impl::const_reference const_reference;
    typedef typename vec_trb_impl::pointer pointer;
    typedef typename vec_trb_impl::const_pointer const_pointer;

   public:
    explicit const_iterator(vec_trb_impl const *_tree, size_type _where)
        : tree(_tree), where(_where) {}

    const_iterator(iterator const &other)
        : tree(other.tree), where(other.where) {}

    const_iterator(const_iterator const &) = default;

    const_iterator &operator++() {
      where = tree->next_i(where);
      return *this;
    }

    const_iterator &operator--() {
      if (where == node_type::nil_sentinel) {
        where = tree->rbeg_i();
      } else {
        where = tree->prev_i(where);
      }
      return *this;
    }

    const_iterator operator++(int) {
      const_iterator save(*this);
      ++*this;
      return save;
    }

    const_iterator operator--(int) {
      const_iterator save(*this);
      --*this;
      return save;
    }

    const_reference operator*() const {
      return reinterpret_cast<const_reference>(
          config_t::get_value(tree->root_.vec, where));
    }

    const_pointer operator->() const {
      return reinterpret_cast<const_pointer>(
          &config_t::get_value(tree->root_.vec, where));
    }

    bool operator==(const_iterator const &other) const {
      assert(tree == other.tree);
      return where == other.where;
    }

    bool operator!=(const_iterator const &other) const {
      assert(tree == other.tree);
      return where != other.where;
    }

   private:
    friend class vec_trb_impl;

    vec_trb_impl const *tree;
    size_type where;
  };

  class reverse_iterator {
   public:
    typedef std::bidirectional_iterator_tag iterator_category;
    typedef typename vec_trb_impl::value_type value_type;
    typedef typename vec_trb_impl::difference_type difference_type;
    typedef typename vec_trb_impl::reference reference;
    typedef typename vec_trb_impl::pointer pointer;

   public:
    explicit reverse_iterator(vec_trb_impl *_tree, size_type _where)
        : tree(_tree), where(_where) {}

    explicit reverse_iterator(iterator const &other) : tree(other.tree) {
      if (other.where == node_type::nil_sentinel) {
        where = other.tree->beg_i();
      } else {
        where = other.tree->prev_i(other.where);
      }
    }

    reverse_iterator(reverse_iterator const &) = default;

    reverse_iterator &operator++() {
      where = tree->prev_i(where);
      return *this;
    }

    reverse_iterator &operator--() {
      if (where == node_type::nil_sentinel) {
        where = tree->root_.beg_i();
      } else {
        where = tree->next_i(where);
      }
      return *this;
    }

    reverse_iterator operator++(int) {
      reverse_iterator save(*this);
      ++*this;
      return save;
    }

    reverse_iterator operator--(int) {
      reverse_iterator save(*this);
      --*this;
      return save;
    }

    reference operator*() const {
      return reinterpret_cast<reference>(
          config_t::get_value(tree->root_.vec, where));
    }

    pointer operator->() const {
      return reinterpret_cast<pointer>(
          &config_t::get_value(tree->root_.vec, where));
    }

    bool operator==(reverse_iterator const &other) const {
      assert(tree == other.tree);
      return where == other.where;
    }

    bool operator!=(reverse_iterator const &other) const {
      assert(tree == other.tree);
      return where != other.where;
    }

    iterator base() const {
      if (node_type::nil_sentinel == where) {
        return tree->begin();
      }
      return ++iterator(tree, where);
    }

   private:
    friend class vec_trb_impl;

    vec_trb_impl *tree;
    size_type where;
  };

  class const_reverse_iterator {
   public:
    typedef std::bidirectional_iterator_tag iterator_category;
    typedef typename vec_trb_impl::value_type value_type;
    typedef typename vec_trb_impl::difference_type difference_type;
    typedef typename vec_trb_impl::reference reference;
    typedef typename vec_trb_impl::const_reference const_reference;
    typedef typename vec_trb_impl::pointer pointer;
    typedef typename vec_trb_impl::const_pointer const_pointer;

   public:
    explicit const_reverse_iterator(vec_trb_impl const *_tree, size_type _where)
        : tree(_tree), where(_where) {}

    explicit const_reverse_iterator(const_iterator const &other)
        : tree(other.tree) {
      if (other.where == node_type::nil_sentinel) {
        where = other.tree->beg_i();
      } else {
        where = other.tree->prev_i(other.where);
      }
    }

    const_reverse_iterator(reverse_iterator const &other)
        : tree(other.tree), where(other.where) {}

    const_reverse_iterator(const_reverse_iterator const &) = default;

    const_reverse_iterator &operator++() {
      where = tree->prev_i(where);
      return *this;
    }

    const_reverse_iterator &operator--() {
      if (where == node_type::nil_sentinel) {
        where = tree->root_.beg_i();
      } else {
        where = tree->next_i(where);
      }
      return *this;
    }

    const_reverse_iterator operator++(int) {
      const_reverse_iterator save(*this);
      ++*this;
      return save;
    }

    const_reverse_iterator operator--(int) {
      const_reverse_iterator save(*this);
      --*this;
      return save;
    }

    const_reference operator*() const {
      return reinterpret_cast<const_reference>(
          config_t::get_value(tree->root_.vec, where));
    }

    const_pointer operator->() const {
      return reinterpret_cast<const_pointer>(
          &config_t::get_value(tree->root_.vec, where));
    }

    bool operator==(const_reverse_iterator const &other) const {
      assert(tree == other.tree);
      return where == other.where;
    }

    bool operator!=(const_reverse_iterator const &other) const {
      assert(tree == other.tree);
      return where != other.where;
    }

    const_iterator base() const {
      if (node_type::nil_sentinel == where) {
        return tree->cbegin();
      }
      return ++const_iterator(tree, where);
    }

   private:
    friend class vec_trb_impl;

    vec_trb_impl const *tree;
    size_type where;
  };

 public:
  typedef typename std::conditional<config_t::unique_type::value,
                                    std::pair<iterator, bool>, iterator>::type
      insert_result_t;
  typedef std::pair<iterator, bool> pair_ib_t;

 protected:
  typedef std::pair<size_type, bool> pair_posi_t;

  template <class unique_type>
  typename std::enable_if<unique_type::value, insert_result_t>::type result_(
      pair_posi_t posi) {
    return std::make_pair(iterator(this, posi.first), posi.second);
  }

  template <class unique_type>
  typename std::enable_if<!unique_type::value, insert_result_t>::type result_(
      pair_posi_t posi) {
    return iterator(this, posi.first);
  }

 public:
  // empty
  vec_trb_impl() : root_(key_compare(), vec_type()) {}

  // empty
  explicit vec_trb_impl(key_compare const &comp,
                        vec_type const &vec = vec_type())
      : root_(comp, vec) {}

  // empty
  explicit vec_trb_impl(vec_type const &vec) : root_(key_compare(), vec) {}

  // range
  template <class iterator_t>
  vec_trb_impl(iterator_t begin, iterator_t end,
               key_compare const &comp = key_compare(),
               vec_type const &vec = vec_type())
      : root_(comp, vec) {
    insert(begin, end);
  }

  // range
  template <class iterator_t>
  vec_trb_impl(iterator_t begin, iterator_t end, vec_type const &vec)
      : root_(key_compare(), vec) {
    insert(begin, end);
  }

  // copy
  vec_trb_impl(vec_trb_impl const &other)
      : root_(other.get_cmp_(), vec_type()) {
    insert(other.begin(), other.end());
  }

  // copy
  vec_trb_impl(vec_trb_impl const &other, vec_type const &vec)
      : root_(other.get_cmp_(), vec) {
    insert(other.begin(), other.end());
  }

  // move
  vec_trb_impl(vec_trb_impl &&other) : root_(key_compare(), vec_type()) {
    swap(other);
  }

  // move
  vec_trb_impl(vec_trb_impl &&other, vec_type const &vec)
      : root_(key_compare(), vec) {
    insert(std::make_move_iterator(other.begin()),
           std::make_move_iterator(other.end()));
  }

  // initializer list
  vec_trb_impl(std::initializer_list<value_type> il,
               key_compare const &comp = key_compare(),
               vec_type const &vec = vec_type())
      : vec_trb_impl(il.begin(), il.end(), comp, vec) {}

  // initializer list
  vec_trb_impl(std::initializer_list<value_type> il, vec_type const &vec)
      : vec_trb_impl(il.begin(), il.end(), key_compare(), vec) {}

  // destructor
  ~vec_trb_impl() { trb_clear_<true>(); }

  // copy
  vec_trb_impl &operator=(vec_trb_impl const &other) {
    if (this == &other) {
      return *this;
    }
    trb_clear_<false>();
    insert(other.begin(), other.end());
    return *this;
  }

  // move
  vec_trb_impl &operator=(vec_trb_impl &&other) {
    if (this == &other) {
      return *this;
    }
    std::swap(root_, other.root_);
    return *this;
  }

  // initializer list
  vec_trb_impl &operator=(std::initializer_list<value_type> il) {
    clear();
    insert(il.begin(), il.end());
    return *this;
  }

  void reserve(size_t cap) { root_.vec.reserve(cap); }

  void swap(vec_trb_impl &other) { std::swap(root_, other.root_); }

  typedef std::pair<iterator, iterator> pair_ii_t;
  typedef std::pair<const_iterator, const_iterator> pair_cici_t;

  // single element
  insert_result_t insert(value_type const &value) {
    TERARK_VERIFY_LT(size(), max_size() - 1);
    return result_<typename config_t::unique_type>(
        trb_insert_(typename config_t::unique_type(), value));
  }

  // single element
  template <class in_value_t>
  typename std::enable_if<std::is_convertible<in_value_t, value_type>::value,
                          insert_result_t>::type
  insert(in_value_t &&value) {
    TERARK_VERIFY_LT(size(), max_size() - 1);
    return result_<typename config_t::unique_type>(trb_insert_(
        typename config_t::unique_type(), std::forward<in_value_t>(value)));
  }

  // with hint
  iterator insert(const_iterator hint, value_type const &value) {
    TERARK_VERIFY_LT(size(), max_size() - 1);
    return iterator(this,
                    trb_insert_(typename config_t::unique_type(), value).first);
  }

  // with hint
  template <class in_value_t>
  typename std::enable_if<std::is_convertible<in_value_t, value_type>::value,
                          insert_result_t>::type
  insert(const_iterator hint, in_value_t &&value) {
    TERARK_VERIFY_LT(size(), max_size() - 1);
    return result_<typename config_t::unique_type>(trb_insert_(
        typename config_t::unique_type(), std::forward<in_value_t>(value)));
  }

  // range
  template <class iterator_t>
  void insert(iterator_t begin, iterator_t end) {
    for (; begin != end; ++begin) {
      emplace(*begin);
    }
  }

  // initializer list
  void insert(std::initializer_list<value_type> il) {
    insert(il.begin(), il.end());
  }

  // single element
  template <class... args_t>
  insert_result_t emplace(args_t &&...args) {
    TERARK_VERIFY_LT(size(), max_size() - 1);
    return result_<typename config_t::unique_type>(trb_insert_(
        typename config_t::unique_type(), std::forward<args_t>(args)...));
  }

  // with hint
  template <class... args_t>
  insert_result_t emplace_hint(const_iterator hint, args_t &&...args) {
    TERARK_VERIFY_LT(size(), max_size() - 1);
    return result_<typename config_t::unique_type>(trb_insert_(
        typename config_t::unique_type(), std::forward<args_t>(args)...));
  }

  iterator find(key_type const &key) {
    if (config_t::unique_type::value) {
      return iterator(this, vec_trb_equal_unique(
                                root_, config_t::get_unit_accessor(root_.vec),
                                key, get_cmp_()));
    } else {
      size_type where = vec_trb_lower_bound(
          root_, config_t::get_unit_accessor(root_.vec), key, get_cmp_());
      return iterator(this, (where == node_type::nil_sentinel ||
                             get_cmp_()(key, get_key_(where)))
                                ? node_type::nil_sentinel
                                : where);
    }
  }

  const_iterator find(key_type const &key) const {
    if (config_t::unique_type::value) {
      return const_iterator(
          this,
          vec_trb_equal_unique(root_, config_t::get_unit_accessor(root_.vec),
                               key, get_cmp_()));
    } else {
      size_type where = vec_trb_lower_bound(
          root_, config_t::get_unit_accessor(root_.vec), key, get_cmp_());
      return const_iterator(this, (where == node_type::nil_sentinel ||
                                   get_cmp_()(key, get_key_(where)))
                                      ? node_type::nil_sentinel
                                      : where);
    }
  }

  iterator erase(const_iterator where) {
    auto index = vec_trb_move_next(where.where, NodeAccessor{&root_.vec});
    trb_erase_index_(where.where);
    return iterator(this, index);
  }

  size_type erase(key_type const &key) {
    size_type erase_count = 0;
    while (trb_erase_key_(key)) {
      ++erase_count;
      if (config_t::unique_type::value) {
        break;
      }
    }
    return erase_count;
  }

  iterator erase(const_iterator erase_begin, const_iterator erase_end) {
    if (erase_begin == cbegin() && erase_end == cend()) {
      clear();
      return begin();
    } else {
      while (erase_begin != erase_end) {
        erase(erase_begin++);
      }
      return iterator(this, erase_begin.where);
    }
  }

  size_type count(key_type const &key) const {
    pair_cici_t range = equal_range(key);
    return std::distance(range.first, range.second);
  }

  iterator lower_bound(key_type const &key) {
    return iterator(this, lwb_i(key));
  }

  const_iterator lower_bound(key_type const &key) const {
    return const_iterator(this, lwb_i(key));
  }

  iterator upper_bound(key_type const &key) {
    return iterator(this, upb_i(key));
  }

  const_iterator upper_bound(key_type const &key) const {
    return const_iterator(this, upb_i(key));
  }

  pair_ii_t equal_range(key_type const &key) {
    auto r = vec_trb_equal_range(root_, config_t::get_unit_accessor(root_.vec),
                                 key, get_cmp_());
    return pair_ii_t(iterator(this, r.first), iterator(this, r.second));
  }

  pair_cici_t equal_range(key_type const &key) const {
    auto r = vec_trb_equal_range(root_, config_t::get_unit_accessor(root_.vec),
                                 key, get_cmp_());
    return pair_cici_t(const_iterator(this, r.first),
                       const_iterator(this, r.second));
  }

  size_type beg_i() const {
    return root_.get_most_left(CNodeAccessor{&root_.vec});
  }

  size_type end_i() const { return node_type::nil_sentinel; }

  size_type rbeg_i() const {
    return root_.get_most_right(CNodeAccessor{&root_.vec});
  }

  size_type rend_i() const { return node_type::nil_sentinel; }

  size_type next_i(size_type i) const {
    return vec_trb_move_next(i, CNodeAccessor{&root_.vec});
  }

  size_type prev_i(size_type i) const {
    return vec_trb_move_prev(i, CNodeAccessor{&root_.vec});
  }

  size_type lwb_i(key_type const &key) const {
    return vec_trb_lower_bound(root_, config_t::get_unit_accessor(root_.vec),
                               key, get_cmp_());
  }

  size_type upb_i(key_type const &key) const {
    return vec_trb_upper_bound(root_, config_t::get_unit_accessor(root_.vec),
                               key, get_cmp_());
  }

  size_type rlwb_i(key_type const &key) const {
    return vec_trb_reverse_lower_bound(
        root_, config_t::get_unit_accessor(root_.vec), key, get_cmp_());
  }

  size_type rupb_i(key_type const &key) const {
    return vec_trb_reverse_upper_bound(
        root_, config_t::get_unit_accessor(root_.vec), key, get_cmp_());
  }

  key_type const &key(size_type i) const {
    return config_t::get_key(config_t::get_value(root_.vec, i));
  }

  value_type &elem_at(size_type i) {
    return reinterpret_cast<value_type &>(config_t::get_value(root_.vec, i));
  }

  value_type const &elem_at(size_type i) const {
    return reinterpret_cast<value_type const &>(
        config_t::get_value(root_.vec, i));
  }

  iterator begin() { return iterator(this, beg_i()); }

  iterator end() { return iterator(this, end_i()); }

  const_iterator begin() const { return const_iterator(this, beg_i()); }

  const_iterator end() const { return const_iterator(this, end_i()); }

  const_iterator cbegin() const { return const_iterator(this, beg_i()); }

  const_iterator cend() const { return const_iterator(this, end_i()); }

  reverse_iterator rbegin() { return reverse_iterator(this, rbeg_i()); }

  reverse_iterator rend() { return reverse_iterator(this, rend_i()); }

  const_reverse_iterator rbegin() const {
    return const_reverse_iterator(this, rbeg_i());
  }

  const_reverse_iterator rend() const {
    return const_reverse_iterator(this, rend_i());
  }

  const_reverse_iterator crbegin() const {
    return const_reverse_iterator(this, rbeg_i());
  }

  const_reverse_iterator crend() const {
    return const_reverse_iterator(this, rend_i());
  }

  bool empty() const { return root_.root.root == node_type::nil_sentinel; }

  void clear() { trb_clear_<false>(); }

  size_type size() const { return root_.get_count(); }

  size_type max_size() const {
    return std::min<size_type>(root_.vec.max_size(),
                               size_type(node_type::nil_sentinel));
  }

  vec_type &vec() { return root_.vec; }

  vec_type const &vec() const { return root_.vec; }

 protected:
  Root root_;

 protected:
  key_compare &get_cmp_() { return root_; }

  key_compare const &get_cmp_() const { return root_; }

  key_type const &get_key_(size_type index) const {
    return config_t::get_key(config_t::get_value(root_.vec, index));
  }

  size_type alloc_index_() {
    if (root_.free == node_type::nil_sentinel) {
      return config_t::alloc_index(root_.vec);
    }
    auto access = NodeAccessor{&root_.vec};
    size_type index = root_.free;
    root_.free = access(index).left_get_link();
    return index;
  }

  void dealloc_index_(size_type index) {
    auto access = NodeAccessor{&root_.vec};
    access(index).left_set_link(root_.free);
    root_.free = index;
  }

  template <class arg_first_t, class... args_other_t>
  pair_posi_t trb_insert_(std::false_type, arg_first_t &&arg_first,
                          args_other_t &&...args_other) {
    vec_trb_stack_t<node_type, stack_max_depth> stack;
    size_type index = alloc_index_();
    auto &value = config_t::get_value(root_.vec, index);
    ::new (&value) storage_type(std::forward<arg_first_t>(arg_first),
                                std::forward<args_other_t>(args_other)...);
    vec_trb_find_path_for_multi(root_, stack, NodeAccessor{&root_.vec}, index,
                                key_compare_ex{&root_});
    vec_trb_insert(root_, stack, NodeAccessor{&root_.vec}, index);
    return {index, true};
  }

  template <class arg_first_t, class... args_other_t>
  pair_posi_t trb_insert_(std::true_type, arg_first_t &&arg_first,
                          args_other_t &&...args_other) {
    vec_trb_stack_t<node_type, stack_max_depth> stack;
    auto key = get_key_t()(arg_first, args_other...);
    bool exists = vec_trb_find_path_for_unique(
        root_, stack, config_t::get_unit_accessor(root_.vec), key, get_cmp_());
    if (exists) {
      return {stack.get_index(stack.height - 1), false};
    }
    size_type index = alloc_index_();
    auto &value = config_t::get_value(root_.vec, index);
    ::new (&value) storage_type(std::forward<arg_first_t>(arg_first),
                                std::forward<args_other_t>(args_other)...);
    vec_trb_insert(root_, stack, NodeAccessor{&root_.vec}, index);
    return {index, true};
  }

  bool trb_erase_key_(key_type const &key) {
    vec_trb_stack_t<node_type, stack_max_depth> stack;
    bool exists = vec_trb_find_path_for_unique(
        root_, stack, config_t::get_unit_accessor(root_.vec), key, get_cmp_());
    if (!exists) {
      return false;
    }
    auto index = stack.get_index(stack.height - 1);
    vec_trb_remove(root_, stack, NodeAccessor{&root_.vec});
    auto &value = config_t::get_value(root_.vec, index);
    value.~storage_type();
    dealloc_index_(index);
    return true;
  }

  void trb_erase_index_(size_type index) {
    vec_trb_stack_t<node_type, stack_max_depth> stack;
    bool exists = vec_trb_find_path_for_remove(
        root_, stack, NodeAccessor{&root_.vec}, index, key_compare_ex{&root_});
    assert(exists);
    (void)exists;  // for compiler : shut up !
    vec_trb_remove(root_, stack, NodeAccessor{&root_.vec});
    auto &value = config_t::get_value(root_.vec, index);
    value.~storage_type();
    dealloc_index_(index);
  }

  template <bool clear_memory>
  void trb_clear_() {
    auto access = NodeAccessor{&root_.vec};
    root_.root.root = node_type::nil_sentinel;
    root_.root.set_left(node_type::nil_sentinel);
    root_.root.set_right(node_type::nil_sentinel);
    if (clear_memory) {
      root_.free = node_type::nil_sentinel;
      for (index_type i = 0; i < root_.get_count(); ++i) {
        if (access(i).is_used()) {
          config_t::get_value(root_.vec, i).~storage_type();
        }
      }
      root_.vec.clear();
    } else {
      for (index_type i = 0; i < root_.get_count(); ++i) {
        if (access(i).is_used()) {
          access(i).set_empty();
          config_t::get_value(root_.vec, i).~storage_type();
          dealloc_index_(i);
        }
      }
    }
    root_.root.reset();
  }
};

template <class Key, class Obj, class Cmp, class KeyOfObj, class Uidx,
          class Unique>
struct vec_trb_default_config_t {
  typedef Key key_type;
  typedef Obj value_type;
  typedef Obj storage_type;
  typedef Cmp key_compare;

  typedef vec_trb_node_t<Uidx> node_type;
  struct unit_type {
    node_type node;
    uint8_t data[sizeof(Obj)];
  };
  typedef std::vector<unit_type> vec_type;
  typedef Unique unique_type;

  struct NodeAccessor {
    unit_type *base;
    node_type &operator()(size_t idx) const { return base[idx].node; }
  };
  struct CNodeAccessor {
    const unit_type *base;
    const node_type &operator()(size_t idx) const { return base[idx].node; }
  };
  static NodeAccessor get_node_accessor(vec_type &vec) { return {vec.data()}; }
  static CNodeAccessor get_node_accessor(const vec_type &vec) {
    return {vec.data()};
  }

  struct UnitAccessor : KeyOfObj {
    unit_type *base;
    node_type &node(size_t idx) const { return base[idx].node; }
    const Key &key(size_t idx) const {
      // calling KeyOfObj::operator()(Obj&)
      return (*this)(*reinterpret_cast<Obj *>(&base[idx].data));
    }
    UnitAccessor(unit_type *b) : base(b) {}
  };
  struct CUnitAccessor : KeyOfObj {
    const unit_type *base;
    const node_type &node(size_t idx) const { return base[idx].node; }
    const Key &key(size_t idx) const {
      // calling KeyOfObj::operator()(const Obj&)
      return (*this)(*reinterpret_cast<Obj const *>(&base[idx].data));
    }
    CUnitAccessor(const unit_type *b) : base(b) {}
  };
  static UnitAccessor get_unit_accessor(vec_type &vec) { return {vec.data()}; }
  static CUnitAccessor get_unit_accessor(const vec_type &vec) {
    return {vec.data()};
  }

  static node_type &get_node(vec_type &vec, size_t index) {
    return vec[index].node;
  }

  static node_type const &get_node(vec_type const &vec, size_t index) {
    return vec[index].node;
  }

  static storage_type &get_value(vec_type &vec, size_t index) {
    return reinterpret_cast<storage_type &>(vec[index].data);
  }

  static storage_type const &get_value(vec_type const &vec, size_t index) {
    return reinterpret_cast<storage_type const &>(vec[index].data);
  }

  static size_t alloc_index(vec_type &vec) {
    auto index = vec.size();
    vec.emplace_back();
    return index;
  }

  template <class in_type>
  static key_type const &get_key(in_type &&data) {
    return KeyOfObj()(data);
  }
};

template <class Key, class Cmp, class Uidx, class Unique>
struct vec_trb_default_set_config_t
    : vec_trb_default_config_t<Key, Key, Cmp, std::_Identity<Key>, Uidx,
                               Unique> {
  typedef Key mapped_type;
};

template <class Key, class Value, class Cmp, class Uidx, class Unique>
struct vec_trb_default_map_config_t
    : vec_trb_default_config_t<Key, std::pair<Key const, Value>, Cmp,
                               std::_Select1st<std::pair<Key const, Value>>,
                               Uidx, Unique> {
  typedef Value mapped_type;
};

template <class Key, class Cmp = std::less<Key>, class Uidx = uint32_t>
using vec_trb_set =
    vec_trb_impl<vec_trb_default_set_config_t<Key, Cmp, Uidx, std::true_type>>;

template <class Key, class Cmp = std::less<Key>, class Uidx = uint32_t>
using vec_trb_multiset =
    vec_trb_impl<vec_trb_default_set_config_t<Key, Cmp, Uidx, std::false_type>>;

template <class Key, class Value, class Cmp = std::less<Key>,
          class Uidx = uint32_t>
class vec_trb_map
    : public vec_trb_impl<
          vec_trb_default_map_config_t<Key, Value, Cmp, Uidx, std::true_type>> {
  typedef vec_trb_default_map_config_t<Key, Value, Cmp, Uidx, std::true_type>
      config_t;
  typedef vec_trb_impl<config_t> super;

 public:
  typedef typename super::value_type kv_pair_t;
  typedef typename super::KeyAccessor KeyAccessor;
  using super::super;

  Value &operator[](const Key &key) {
    size_t lo = super::lwb_i(key);
    if (lo == super::node_type::nil_sentinel ||
        super::get_cmp_()(key, KeyAccessor{&super::root_.vec}(lo))) {
      lo = super::trb_insert_(std::false_type(), key, Value()).first;
    }
    return config_t::get_value(super::root_.vec, lo).second;
  }
};

template <class Key, class Value, class Cmp = std::less<Key>,
          class Uidx = uint32_t>
using vec_trb_multimap = vec_trb_impl<
    vec_trb_default_map_config_t<Key, Value, Cmp, Uidx, std::false_type>>;

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
