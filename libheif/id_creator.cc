/*
 * HEIF codec.
 * Copyright (c) 2026 Dirk Farin <dirk.farin@gmail.com>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "id_creator.h"
#include <cstdint>

namespace {

// The next free id after 'id', i.e. the value a counter should take once 'id'
// has been handed out or reserved. When 'id' is the maximum representable value
// there is no larger id, so the counter becomes 0 == "exhausted" and
// get_new_id() will refuse to hand out another id. Computing this explicitly
// (rather than 'id + 1') keeps the exhausted transition free of the unsigned
// wrap that the 'integer' sanitizer flags, while guaranteeing the counter always
// moves strictly past 'id' so no id is ever handed out twice.
inline uint32_t next_id_after(uint32_t id)
{
  return id == UINT32_MAX ? 0u : id + 1u;
}

}


Result<uint32_t> IDCreator::get_new_id(Namespace ns)
{
  if (m_unif) {
    if (m_next_id_global == 0) {
      return Error(heif_error_Usage_error,
                   heif_suberror_Unspecified,
                   "ID namespace overflow");
    }
    uint32_t id = m_next_id_global;
    m_next_id_global = next_id_after(id);
    return id;
  }

  uint32_t* counter = nullptr;
  switch (ns) {
    case Namespace::item:
      counter = &m_next_id_item;
      break;
    case Namespace::track:
      counter = &m_next_id_track;
      break;
    case Namespace::entity_group:
      counter = &m_next_id_entity_group;
      break;
  }

  if (*counter == 0) {
    return Error(heif_error_Usage_error,
                 heif_suberror_Unspecified,
                 "ID namespace overflow");
  }

  uint32_t id = *counter;
  *counter = next_id_after(id);

  // Keep the global counter ahead of all namespace counters so that switching to
  // unif mode later does not reuse an ID that was already handed out.
  if (m_next_id_global != 0 && id >= m_next_id_global) {
    m_next_id_global = next_id_after(id);
  }

  return id;
}


void IDCreator::mark_id_used(Namespace ns, uint32_t id)
{
  uint32_t* counter = nullptr;
  switch (ns) {
    case Namespace::item:
      counter = &m_next_id_item;
      break;
    case Namespace::track:
      counter = &m_next_id_track;
      break;
    case Namespace::entity_group:
      counter = &m_next_id_entity_group;
      break;
  }

  // A counter value of 0 means "exhausted" (see get_new_id). Advancing past a
  // just-used id keeps get_new_id() from ever handing out the same id again;
  // marking 0xFFFFFFFF used moves the counter to 0 == exhausted.
  if (*counter != 0 && id >= *counter) {
    *counter = next_id_after(id);
  }

  if (m_next_id_global != 0 && id >= m_next_id_global) {
    m_next_id_global = next_id_after(id);
  }
}
