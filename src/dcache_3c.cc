/*
 * Copyright 2026 Litz Lab
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "dcache_3c.h"

#include <list>
#include <unordered_map>
#include <unordered_set>

namespace {

struct FullyAssocShadow {
  size_t capacity_lines = 0;
  uns line_size = 1;
  std::unordered_set<Addr> seen_lines;
  std::list<Addr> lru_lines;
  std::unordered_map<Addr, std::list<Addr>::iterator> lru_pos;

  void init(uns cache_size, uns new_line_size) {
    line_size = new_line_size ? new_line_size : 1;
    capacity_lines = cache_size / line_size;
    seen_lines.clear();
    lru_lines.clear();
    lru_pos.clear();
  }

  bool access_shadow(Addr line_addr) {
    Addr block = line_addr / line_size;
    auto hit = lru_pos.find(block);
    if (hit != lru_pos.end()) {
      lru_lines.splice(lru_lines.begin(), lru_lines, hit->second);
      hit->second = lru_lines.begin();
      return true;
    }

    if (capacity_lines == 0)
      return false;

    if (lru_lines.size() >= capacity_lines) {
      Addr victim = lru_lines.back();
      lru_lines.pop_back();
      lru_pos.erase(victim);
    }

    lru_lines.push_front(block);
    lru_pos[block] = lru_lines.begin();
    return false;
  }

  bool access_seen(Addr line_addr) {
    Addr block = line_addr / line_size;
    auto inserted = seen_lines.insert(block);
    return !inserted.second;
  }
};

std::unordered_map<uns8, FullyAssocShadow> dcache_3c_state;

}  // namespace

void dcache_3c_init(uns8 proc_id, uns cache_size, uns line_size) {
  dcache_3c_state[proc_id].init(cache_size, line_size);
}

Dcache_3C_Type dcache_3c_access(uns8 proc_id, Addr line_addr, Flag real_dcache_miss) {
  FullyAssocShadow& state = dcache_3c_state[proc_id];

  bool seen_before = state.access_seen(line_addr);
  bool shadow_hit = state.access_shadow(line_addr);

  if (!real_dcache_miss)
    return DCACHE_3C_NONE;

  if (!seen_before)
    return DCACHE_3C_COMPULSORY;
  if (shadow_hit)
    return DCACHE_3C_CONFLICT;
  return DCACHE_3C_CAPACITY;
}
