/*
  libheif unit tests for IDCreator.

  MIT License

  Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include "catch_amalgamated.hpp"
#include "id_creator.h"

#include <cstdint>
#include <set>

using NS = IDCreator::Namespace;

TEST_CASE("IDCreator hands out unique increasing ids") {
  IDCreator idc;
  std::set<uint32_t> seen;
  for (int i = 0; i < 1000; i++) {
    auto r = idc.get_new_id(NS::item);
    REQUIRE(r);
    REQUIRE(seen.insert(*r).second); // no duplicate
  }
}

TEST_CASE("IDCreator never re-hands a marked id") {
  IDCreator idc;

  // Reserve a scattering of ids, including some below and some above the
  // starting counter (1).
  const uint32_t marked[] = {1, 5, 6, 7, 100, 99, 42};
  for (uint32_t id : marked) {
    idc.mark_id_used(NS::item, id);
  }

  std::set<uint32_t> reserved(std::begin(marked), std::end(marked));

  for (int i = 0; i < 1000; i++) {
    auto r = idc.get_new_id(NS::item);
    REQUIRE(r);
    // Must never collide with a reserved id, and must be strictly increasing
    // past the highest reserved id.
    REQUIRE(reserved.find(*r) == reserved.end());
    REQUIRE(*r > 100);
  }
}

TEST_CASE("IDCreator namespaces are independent") {
  IDCreator idc;
  idc.mark_id_used(NS::item, 500);

  // track / entity_group counters are unaffected by the item reservation.
  auto t = idc.get_new_id(NS::track);
  REQUIRE(t);
  REQUIRE(*t == 1);

  auto g = idc.get_new_id(NS::entity_group);
  REQUIRE(g);
  REQUIRE(*g == 1);

  auto it = idc.get_new_id(NS::item);
  REQUIRE(it);
  REQUIRE(*it == 501);
}

TEST_CASE("IDCreator exhausts cleanly at UINT32_MAX-1") {
  IDCreator idc;
  idc.mark_id_used(NS::item, UINT32_MAX - 1);

  // Exactly one id (UINT32_MAX) is left.
  auto last = idc.get_new_id(NS::item);
  REQUIRE(last);
  REQUIRE(*last == UINT32_MAX);

  // Now exhausted: no further id, and definitely no wrap-around reuse.
  auto overflow = idc.get_new_id(NS::item);
  REQUIRE_FALSE(overflow);
  REQUIRE(overflow.is_error());
}

TEST_CASE("IDCreator marking UINT32_MAX exhausts the namespace (no overflow)") {
  // This is the value that made mark_id_used compute 'id + 1' and wrap to 0,
  // aborting under -fsanitize=integer. It must exhaust the namespace, never
  // hand out an id, and never reuse 0xFFFFFFFF.
  IDCreator idc;
  idc.mark_id_used(NS::item, UINT32_MAX);

  auto r = idc.get_new_id(NS::item);
  REQUIRE_FALSE(r);
  REQUIRE(r.is_error());

  // Marking a smaller id afterwards must not resurrect the exhausted counter.
  idc.mark_id_used(NS::item, 3);
  auto r2 = idc.get_new_id(NS::item);
  REQUIRE_FALSE(r2);
}

TEST_CASE("IDCreator unif mode shares one id space across namespaces") {
  IDCreator idc;
  idc.set_unif(true);

  std::set<uint32_t> seen;
  auto a = idc.get_new_id(NS::item);
  auto b = idc.get_new_id(NS::track);
  auto c = idc.get_new_id(NS::entity_group);
  REQUIRE(a);
  REQUIRE(b);
  REQUIRE(c);
  REQUIRE(seen.insert(*a).second);
  REQUIRE(seen.insert(*b).second); // different namespace, still unique
  REQUIRE(seen.insert(*c).second);

  // Reserving the max in unif mode exhausts the single shared counter.
  IDCreator idc2;
  idc2.set_unif(true);
  idc2.mark_id_used(NS::track, UINT32_MAX);
  auto r = idc2.get_new_id(NS::item);
  REQUIRE_FALSE(r);
}
