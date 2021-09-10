//    _   _____   __________
//   | | / / _ | / __/_  __/     Visibility
//   | |/ / __ |_\ \  / /          Across
//   |___/_/ |_/___/ /_/       Space and Time
//
// SPDX-FileCopyrightText: (c) 2021 The VAST Contributors
// SPDX-License-Identifier: BSD-3-Clause

#include "vast/type.hpp"

#include "vast/detail/assert.hpp"
#include "vast/detail/overload.hpp"
#include "vast/fbs/type.hpp"
#include "vast/legacy_type.hpp"

#include <caf/sum_type.hpp>

namespace vast {

namespace {

std::span<const std::byte> none_type_representation() {
  // This helper function solely exists because ADL will not find the as_bytes
  // overload for none_type from within the as_bytes overload for type.
  return as_bytes(none_type{});
}

} // namespace

type::type() noexcept = default;

type::type(const type& other) noexcept = default;

type& type::operator=(const type& rhs) noexcept = default;

type::type(type&& other) noexcept = default;

type& type::operator=(type&& other) noexcept = default;

type::~type() noexcept = default;

type::type(chunk_ptr&& table) noexcept : table_{std::move(table)} {
#if VAST_ENABLE_ASSERTIONS
  VAST_ASSERT(table_);
  VAST_ASSERT(table_->size() > 0);
  const auto* const data = reinterpret_cast<const uint8_t*>(table_->data());
  auto verifier = flatbuffers::Verifier{data, table_->size()};
  VAST_ASSERT(fbs::GetType(data)->Verify(verifier),
              "Encountered invalid vast.fbs.Type FlatBuffers table.");
#  if defined(FLATBUFFERS_TRACK_VERIFIER_BUFFER_SIZE)
  VAST_ASSERT(verifier.GetComputedSize() == table_->size(),
              "Encountered unexpected excess bytes in vast.fbs.Type "
              "FlatBuffers table.");
#  endif // defined(FLATBUFFERS_TRACK_VERIFIER_BUFFER_SIZE)
#endif   // VAST_ENABLE_ASSERTIONS
}

type::type(std::string_view name, const type& nested) noexcept {
  if (name.empty()) {
    // This special case exists for easier conversion of legacy types, which did
    // not require an legacy alias type wrapping to have a name.
    *this = nested;
  } else {
    const auto nested_bytes = as_bytes(nested);
    // By default the builder allocates 1024 bytes, which is much more than
    // what we require: 52 (fixed) + name (rounded up to a multiple of 4) +
    // nested type.
    const auto reserved_size
      = 52 + (((name.size() + 3) / 4) * 4) + nested_bytes.size();
    auto builder = flatbuffers::FlatBufferBuilder{reserved_size};
    const auto alias_type_name = builder.CreateString(name);
    const auto alias_type_type = builder.CreateVector(
      reinterpret_cast<const uint8_t*>(nested_bytes.data()),
      nested_bytes.size());
    const auto alias_type = fbs::type::alias_type::Createv0(
      builder, alias_type_name, alias_type_type);
    const auto type = fbs::CreateType(builder, fbs::type::Type::alias_type_v0,
                                      alias_type.Union());
    builder.Finish(type);
    auto result = builder.Release();
    VAST_ASSERT(result.size() == reserved_size);
    table_ = chunk::make(std::move(result));
  }
}

type::type(const legacy_type& other) noexcept {
  auto f = detail::overload{
    [&](const legacy_none_type&) {
      *this = type{other.name(), none_type{}};
    },
    [&](const legacy_bool_type&) {
      *this = type{other.name(), bool_type{}};
    },
    [&](const legacy_integer_type&) {
      *this = type{other.name(), integer_type{}};
    },
    [&](const legacy_count_type&) {
      *this = type{other.name(), count_type{}};
    },
    [&](const legacy_real_type&) {
      *this = type{other.name(), real_type{}};
    },
    [&](const legacy_duration_type&) {
      *this = type{other.name(), duration_type{}};
    },
    [&](const legacy_time_type&) {
      *this = type{other.name(), time_type{}};
    },
    [&](const legacy_string_type&) {
      *this = type{other.name(), string_type{}};
    },
    [&](const legacy_list_type& list) {
      *this = type{other.name(), type{list_type{type{list.value_type}}}};
    },
    [&](const legacy_alias_type& alias) {
      return type{other.name(), type{alias.value_type}};
    },
    [&](const auto&) {
      // TODO: Implement for all legacy types, then remove this handler.
      // - legacy_pattern_type,
      // - legacy_address_type,
      // - legacy_subnet_type,
      // - legacy_enumeration_type,
      // - legacy_map_type,
      // - legacy_record_type,
    },
  };
  caf::visit(f, other);
}

type::operator bool() const noexcept {
  return table(transparent::yes).type_type() != fbs::type::Type::NONE;
}

bool operator==(const type& lhs, const type& rhs) noexcept {
  const auto lhs_bytes = as_bytes(lhs);
  const auto rhs_bytes = as_bytes(rhs);
  return std::equal(lhs_bytes.begin(), lhs_bytes.end(), rhs_bytes.begin(),
                    rhs_bytes.end());
}

std::strong_ordering operator<=>(const type& lhs, const type& rhs) noexcept {
  auto lhs_bytes = as_bytes(lhs);
  auto rhs_bytes = as_bytes(rhs);
  // TODO: Replace implementation with `std::lexicographical_compare_three_way`
  // once that is implemented for all compilers we need to support. This does
  // the same thing essentially, just a lot less generic.
  if (lhs_bytes.data() == rhs_bytes.data()
      && lhs_bytes.size() == rhs_bytes.size())
    return std::strong_ordering::equal;
  while (!lhs_bytes.empty() && !rhs_bytes.empty()) {
    if (lhs_bytes[0] < rhs_bytes[0])
      return std::strong_ordering::less;
    if (lhs_bytes[0] > rhs_bytes[0])
      return std::strong_ordering::greater;
    lhs_bytes = lhs_bytes.subspan(1);
    rhs_bytes = rhs_bytes.subspan(1);
  }
  return !lhs_bytes.empty()   ? std::strong_ordering::greater
         : !rhs_bytes.empty() ? std::strong_ordering::less
                              : std::strong_ordering::equivalent;
}

const fbs::Type& type::table(enum transparent transparent) const noexcept {
  const auto& repr = as_bytes(*this);
  const auto* root = fbs::GetType(repr.data());
  VAST_ASSERT(root);
  while (transparent == transparent::yes) {
    switch (root->type_type()) {
      case fbs::type::Type::NONE:
      case fbs::type::Type::bool_type_v0:
      case fbs::type::Type::integer_type_v0:
      case fbs::type::Type::count_type_v0:
      case fbs::type::Type::real_type_v0:
      case fbs::type::Type::duration_type_v0:
      case fbs::type::Type::time_type_v0:
      case fbs::type::Type::string_type_v0:
      case fbs::type::Type::list_type_v0:
        transparent = transparent::no;
        break;
      case fbs::type::Type::alias_type_v0:
        root = root->type_as_alias_type_v0()->type_nested_root();
        VAST_ASSERT(root);
        break;
    }
  }
  return *root;
}

uint8_t type::type_index() const noexcept {
  static_assert(
    std::is_same_v<uint8_t, std::underlying_type_t<vast::fbs::type::Type>>);
  return static_cast<uint8_t>(table(transparent::yes).type_type());
}

std::span<const std::byte> as_bytes(const type& x) noexcept {
  return x.table_ ? as_bytes(*x.table_) : none_type_representation();
}

std::string_view type::name() const& noexcept {
  const auto& root = table(transparent::no);
  switch (root.type_type()) {
    case fbs::type::Type::NONE:
      return "none";
    case fbs::type::Type::bool_type_v0:
      return "bool";
    case fbs::type::Type::integer_type_v0:
      return "integer";
    case fbs::type::Type::count_type_v0:
      return "count";
    case fbs::type::Type::real_type_v0:
      return "real";
    case fbs::type::Type::duration_type_v0:
      return "duration";
    case fbs::type::Type::time_type_v0:
      return "time";
    case fbs::type::Type::string_type_v0:
      return "string";
    case fbs::type::Type::list_type_v0:
      return "list";
    case fbs::type::Type::alias_type_v0:
      return root.type_as_alias_type_v0()->name()->string_view();
  }
}

// -- none_type ---------------------------------------------------------------

uint8_t none_type::type_index() noexcept {
  return static_cast<uint8_t>(fbs::type::Type::NONE);
}

std::span<const std::byte> as_bytes(const none_type&) noexcept {
  static const auto buffer = []() noexcept {
    constexpr auto reserved_size = 12;
    auto builder = flatbuffers::FlatBufferBuilder{reserved_size};
    const auto type = fbs::CreateType(builder);
    builder.Finish(type);
    auto result = builder.Release();
    VAST_ASSERT(result.size() == reserved_size);
    return result;
  }();
  return as_bytes(buffer);
}

// -- bool_type ---------------------------------------------------------------

uint8_t bool_type::type_index() noexcept {
  return static_cast<uint8_t>(fbs::type::Type::bool_type_v0);
}

std::span<const std::byte> as_bytes(const bool_type&) noexcept {
  static const auto buffer = []() noexcept {
    constexpr auto reserved_size = 32;
    auto builder = flatbuffers::FlatBufferBuilder{reserved_size};
    const auto bool_type = fbs::type::bool_type::Createv0(builder);
    const auto type = fbs::CreateType(builder, fbs::type::Type::bool_type_v0,
                                      bool_type.Union());
    builder.Finish(type);
    auto result = builder.Release();

    VAST_ASSERT(result.size() == reserved_size);
    return result;
  }();
  return as_bytes(buffer);
}

// -- integer_type ------------------------------------------------------------

uint8_t integer_type::type_index() noexcept {
  return static_cast<uint8_t>(fbs::type::Type::integer_type_v0);
}

std::span<const std::byte> as_bytes(const integer_type&) noexcept {
  static const auto buffer = []() noexcept {
    constexpr auto reserved_size = 32;
    auto builder = flatbuffers::FlatBufferBuilder{reserved_size};
    const auto integer_type = fbs::type::integer_type::Createv0(builder);
    const auto type = fbs::CreateType(builder, fbs::type::Type::integer_type_v0,
                                      integer_type.Union());
    builder.Finish(type);
    auto result = builder.Release();

    VAST_ASSERT(result.size() == reserved_size);
    return result;
  }();
  return as_bytes(buffer);
}

// -- count_type --------------------------------------------------------------

uint8_t count_type::type_index() noexcept {
  return static_cast<uint8_t>(fbs::type::Type::count_type_v0);
}

std::span<const std::byte> as_bytes(const count_type&) noexcept {
  static const auto buffer = []() noexcept {
    constexpr auto reserved_size = 32;
    auto builder = flatbuffers::FlatBufferBuilder{reserved_size};
    const auto count_type = fbs::type::count_type::Createv0(builder);
    const auto type = fbs::CreateType(builder, fbs::type::Type::count_type_v0,
                                      count_type.Union());
    builder.Finish(type);
    auto result = builder.Release();

    VAST_ASSERT(result.size() == reserved_size);
    return result;
  }();
  return as_bytes(buffer);
}

// -- real_type ---------------------------------------------------------------

uint8_t real_type::type_index() noexcept {
  return static_cast<uint8_t>(fbs::type::Type::real_type_v0);
}

std::span<const std::byte> as_bytes(const real_type&) noexcept {
  static const auto buffer = []() noexcept {
    constexpr auto reserved_size = 32;
    auto builder = flatbuffers::FlatBufferBuilder{reserved_size};
    const auto real_type = fbs::type::real_type::Createv0(builder);
    const auto type = fbs::CreateType(builder, fbs::type::Type::real_type_v0,
                                      real_type.Union());
    builder.Finish(type);
    auto result = builder.Release();

    VAST_ASSERT(result.size() == reserved_size);
    return result;
  }();
  return as_bytes(buffer);
}

// -- duration_type -----------------------------------------------------------

uint8_t duration_type::type_index() noexcept {
  return static_cast<uint8_t>(fbs::type::Type::duration_type_v0);
}

std::span<const std::byte> as_bytes(const duration_type&) noexcept {
  static const auto buffer = []() noexcept {
    constexpr auto reserved_size = 32;
    auto builder = flatbuffers::FlatBufferBuilder{reserved_size};
    const auto duration_type = fbs::type::duration_type::Createv0(builder);
    const auto type = fbs::CreateType(
      builder, fbs::type::Type::duration_type_v0, duration_type.Union());
    builder.Finish(type);
    auto result = builder.Release();

    VAST_ASSERT(result.size() == reserved_size);
    return result;
  }();
  return as_bytes(buffer);
}

// -- time_type ---------------------------------------------------------------

uint8_t time_type::type_index() noexcept {
  return static_cast<uint8_t>(fbs::type::Type::time_type_v0);
}

std::span<const std::byte> as_bytes(const time_type&) noexcept {
  static const auto buffer = []() noexcept {
    constexpr auto reserved_size = 32;
    auto builder = flatbuffers::FlatBufferBuilder{reserved_size};
    const auto time_type = fbs::type::time_type::Createv0(builder);
    const auto type = fbs::CreateType(builder, fbs::type::Type::time_type_v0,
                                      time_type.Union());
    builder.Finish(type);
    auto result = builder.Release();

    VAST_ASSERT(result.size() == reserved_size);
    return result;
  }();
  return as_bytes(buffer);
}

// -- string_type --------------------------------------------------------------

uint8_t string_type::type_index() noexcept {
  return static_cast<uint8_t>(fbs::type::Type::string_type_v0);
}

std::span<const std::byte> as_bytes(const string_type&) noexcept {
  static const auto buffer = []() noexcept {
    constexpr auto reserved_size = 32;
    auto builder = flatbuffers::FlatBufferBuilder{reserved_size};
    const auto string_type = fbs::type::string_type::Createv0(builder);
    const auto type = fbs::CreateType(builder, fbs::type::Type::string_type_v0,
                                      string_type.Union());
    builder.Finish(type);
    auto result = builder.Release();

    VAST_ASSERT(result.size() == reserved_size);
    return result;
  }();
  return as_bytes(buffer);
}

// -- list_type ---------------------------------------------------------------

list_type::list_type(const list_type& other) noexcept = default;

list_type& list_type::operator=(const list_type& rhs) noexcept = default;

list_type::list_type(list_type&& other) noexcept = default;

list_type& list_type::operator=(list_type&& other) noexcept = default;

list_type::~list_type() noexcept = default;

list_type::list_type(const type& value_type) noexcept {
  const auto value_type_bytes = as_bytes(value_type);
  const auto reserved_size = 44 + value_type_bytes.size();
  auto builder = flatbuffers::FlatBufferBuilder{reserved_size};
  const auto list_type_offset = fbs::type::list_type::Createv0(
    builder, builder.CreateVector(
               reinterpret_cast<const uint8_t*>(value_type_bytes.data()),
               value_type_bytes.size()));
  const auto type_offset = fbs::CreateType(
    builder, fbs::type::Type::list_type_v0, list_type_offset.Union());
  builder.Finish(type_offset);
  auto result = builder.Release();
  VAST_ASSERT(result.size() == reserved_size);
  auto chunk = chunk::make(std::move(result));
  static_cast<type&>(*this) = type{std::move(chunk)};
}

type list_type::value_type() const noexcept {
  const auto* view = table(transparent::yes).type_as_list_type_v0()->type();
  VAST_ASSERT(view);
  return type{table_->slice(as_bytes(*view))};
}

uint8_t list_type::type_index() noexcept {
  return static_cast<uint8_t>(fbs::type::Type::list_type_v0);
}

std::span<const std::byte> as_bytes(const list_type& x) noexcept {
  return as_bytes(static_cast<const type&>(x));
}

} // namespace vast
