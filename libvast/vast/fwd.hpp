//    _   _____   __________
//   | | / / _ | / __/_  __/     Visibility
//   | |/ / __ |_\ \  / /          Across
//   |___/_/ |_/___/ /_/       Space and Time
//
// SPDX-FileCopyrightText: (c) 2018 The VAST Contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "vast/config.hpp"

#include <caf/config.hpp>
#include <caf/fwd.hpp>
#include <caf/type_id.hpp>

#include <cstdint>
#include <vector>

#define VAST_ADD_TYPE_ID(type) CAF_ADD_TYPE_ID(vast_types, type)

// -- arrow --------------------------------------------------------------------

// Note that this can also be achieved by including <arrow/type_fwd.h>, but
// that header is a fair bit more expensive than forward declaring just the
// types we need forward-declared here. If this ever diverges between Arrow
// versions, consider switching to including that file.

namespace arrow {

class Array;
class ArrayBuilder;
class DataType;
class MemoryPool;
class RecordBatch;
class Schema;

} // namespace arrow

// -- caf ----------------------------------------------------------------------

// These are missing from <caf/fwd.hpp>.

namespace caf {

template <class>
class inbound_stream_slot;

template <class, class...>
class outbound_stream_slot;

} // namespace caf

// -- vast ---------------------------------------------------------------------

namespace vast {

class address;
class arrow_table_slice_builder;
class bitmap;
class bool_type;
class chunk;
class command;
class count_type;
class data;
class duration_type;
class expression;
class integer_type;
class legacy_abstract_type;
class legacy_type;
class list_type;
class msgpack_table_slice_builder;
class none_type;
class pattern;
class plugin;
class plugin_ptr;
class port;
class real_type;
class schema;
class segment;
class segment_builder;
class segment_store;
class store;
class string_type;
class subnet;
class synopsis;
class table_slice;
class table_slice_builder;
class table_slice_column;
class time_type;
class transform;
class transform_step;
class type;
class uuid;
class value_index;

struct legacy_address_type;
struct legacy_alias_type;
struct meta_extractor;
struct legacy_bool_type;
struct concept_;
struct conjunction;
struct legacy_count_type;
struct curried_predicate;
struct data_extractor;
struct disjunction;
struct legacy_duration_type;
struct legacy_enumeration_type;
struct field_extractor;
struct flow;
struct integer;
struct legacy_integer_type;
struct invocation;
struct legacy_list_type;
struct legacy_map_type;
struct model;
struct negation;
struct legacy_none_type;
struct offset;
struct partition_synopsis;
struct legacy_pattern_type;
struct predicate;
struct qualified_record_field;
struct query;
struct legacy_real_type;
struct legacy_record_type;
struct status;
struct legacy_string_type;
struct legacy_subnet_type;
struct taxonomies;
struct legacy_time_type;
struct type_extractor;
struct type_set;

enum class arithmetic_operator : uint8_t;
enum class bool_operator : uint8_t;
enum class ec : uint8_t;
enum class port_type : uint8_t;
enum class query_options : uint32_t;
enum class relational_operator : uint8_t;
enum class table_slice_encoding : uint8_t;

template <class>
class arrow_table_slice;

template <class>
class msgpack_table_slice;

inline constexpr size_t dynamic_extent = std::numeric_limits<size_t>::max();

template <class... Types>
class projection;

template <class>
class scope_linked;

namespace detail {

template <class>
class framed;

}

void intrusive_ptr_add_ref(const table_slice_builder*);
void intrusive_ptr_release(const table_slice_builder*);

using chunk_ptr = caf::intrusive_ptr<chunk>;
using ids = bitmap; // temporary; until we have a real type for 'ids'
using synopsis_ptr = std::unique_ptr<synopsis>;
using table_slice_builder_ptr = caf::intrusive_ptr<table_slice_builder>;
using transform_step_ptr = std::unique_ptr<transform_step>;
using value_index_ptr = std::unique_ptr<value_index>;

/// A duration in time with nanosecond resolution.
using duration = caf::timespan;

/// An absolute point in time with nanosecond resolution. It is capable to
/// represent +/- 292 years around the UNIX epoch.
using time = caf::timestamp;

/// Unsigned integer type.
using count = uint64_t;

/// Floating point type.
using real = double;

/// Enumeration type.
using enumeration = uint8_t;

namespace fbs {

struct FlatTableSlice;
struct TableSlice;
struct Type;

namespace table_slice {

namespace msgpack {

struct v0;

} // namespace msgpack

namespace arrow {

struct v0;

} // namespace arrow

} // namespace table_slice

} // namespace fbs

namespace detail {

struct stable_map_policy;

template <class, class, class, class>
class vector_map;

/// A map abstraction over an unsorted `std::vector`.
template <class Key, class T,
          class Allocator = std::allocator<std::pair<Key, T>>>
using stable_map = vector_map<Key, T, Allocator, stable_map_policy>;

} // namespace detail

namespace format {

class reader;
class writer;

using reader_ptr = std::unique_ptr<reader>;
using writer_ptr = std::unique_ptr<writer>;

} // namespace format

namespace system {

class configuration;

struct accountant_config;
struct active_partition_state;
struct passive_partition_state;
struct index_state;
struct index_statistics;
struct layout_statistics;
struct component_map;
struct component_map_entry;
struct component_state;
struct component_state_map;
struct data_point;
struct measurement;
struct node_state;
struct performance_sample;
struct query_status;
struct query_status;
struct spawn_arguments;

enum class status_verbosity;

using performance_report = std::vector<performance_sample>;
using report = std::vector<data_point>;

} // namespace system

} // namespace vast

// -- type announcements -------------------------------------------------------

constexpr inline caf::type_id_t first_vast_type_id = 800;

CAF_BEGIN_TYPE_ID_BLOCK(vast_types, first_vast_type_id)

  VAST_ADD_TYPE_ID((vast::address))
  VAST_ADD_TYPE_ID((vast::meta_extractor))
  VAST_ADD_TYPE_ID((vast::bitmap))
  VAST_ADD_TYPE_ID((vast::chunk_ptr))
  VAST_ADD_TYPE_ID((vast::conjunction))
  VAST_ADD_TYPE_ID((vast::curried_predicate))
  VAST_ADD_TYPE_ID((vast::data))
  VAST_ADD_TYPE_ID((vast::data_extractor))
  VAST_ADD_TYPE_ID((vast::disjunction))
  VAST_ADD_TYPE_ID((vast::ec))
  VAST_ADD_TYPE_ID((vast::expression))
  VAST_ADD_TYPE_ID((vast::field_extractor))
  VAST_ADD_TYPE_ID((vast::integer))
  VAST_ADD_TYPE_ID((vast::invocation))
  VAST_ADD_TYPE_ID((vast::negation))
  VAST_ADD_TYPE_ID((vast::pattern))
  VAST_ADD_TYPE_ID((vast::port))
  VAST_ADD_TYPE_ID((vast::port_type))
  VAST_ADD_TYPE_ID((vast::predicate))
  VAST_ADD_TYPE_ID((vast::qualified_record_field))
  VAST_ADD_TYPE_ID((vast::query))
  VAST_ADD_TYPE_ID((vast::query_options))
  VAST_ADD_TYPE_ID((vast::relational_operator))
  VAST_ADD_TYPE_ID((vast::schema))
  VAST_ADD_TYPE_ID((vast::subnet))
  VAST_ADD_TYPE_ID((vast::table_slice))
  VAST_ADD_TYPE_ID((vast::taxonomies))
  VAST_ADD_TYPE_ID((vast::legacy_type))
  VAST_ADD_TYPE_ID((vast::type_extractor))
  VAST_ADD_TYPE_ID((vast::type_set))
  VAST_ADD_TYPE_ID((vast::uuid))

  // TODO: Make list, record, and map concrete typs to we don't need to do
  // these kinda things. See vast/aliases.hpp for their definitions.
  VAST_ADD_TYPE_ID((std::vector<vast::data>))
  VAST_ADD_TYPE_ID((vast::detail::stable_map<std::string, vast::data>))
  VAST_ADD_TYPE_ID((vast::detail::stable_map<vast::data, vast::data>))

  VAST_ADD_TYPE_ID((vast::system::performance_report))
  VAST_ADD_TYPE_ID((vast::system::query_status))
  VAST_ADD_TYPE_ID((vast::system::report))
  VAST_ADD_TYPE_ID((vast::system::status_verbosity))

  VAST_ADD_TYPE_ID((std::pair<std::string, vast::data>))
  VAST_ADD_TYPE_ID((std::vector<uint32_t>))
  VAST_ADD_TYPE_ID((std::vector<vast::count>))
  VAST_ADD_TYPE_ID((std::vector<std::string>))
  VAST_ADD_TYPE_ID((std::vector<vast::table_slice>))
  VAST_ADD_TYPE_ID((std::vector<vast::table_slice_column>))
  VAST_ADD_TYPE_ID((std::vector<vast::uuid>))

  VAST_ADD_TYPE_ID((vast::detail::framed<vast::table_slice>))
  VAST_ADD_TYPE_ID((std::vector<vast::detail::framed<vast::table_slice>>))
  VAST_ADD_TYPE_ID((caf::stream<vast::detail::framed<vast::table_slice>>))

  VAST_ADD_TYPE_ID((caf::stream<vast::table_slice>))
  VAST_ADD_TYPE_ID((caf::stream<vast::table_slice_column>))

CAF_END_TYPE_ID_BLOCK(vast_types)

#undef VAST_CAF_ATOM_ALIAS
#undef VAST_ADD_ATOM
#undef VAST_ADD_TYPE_ID
