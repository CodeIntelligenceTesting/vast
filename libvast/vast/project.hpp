//    _   _____   __________
//   | | / / _ | / __/_  __/     Visibility
//   | |/ / __ |_\ \  / /          Across
//   |___/_/ |_/___/ /_/       Space and Time
//
// SPDX-FileCopyrightText: (c) 2021 The VAST Contributors
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "vast/fwd.hpp"

#include "vast/table_slice.hpp"

#include <iterator>

namespace vast {

namespace detail {

// Returns a tuple of addresses for the element at the given index in each of
// the given tuples.
template <std::size_t Index, class... Tuples>
constexpr auto map_tuple_elements_get_addresses(Tuples&&... tuples) {
  return std::make_tuple(std::addressof(std::get<Index>(tuples))...);
}

// Helper utility of `map_tuple_elements` for use with index sequences.
template <class Invocable, class... Tuples, std::size_t... Indices>
constexpr auto
map_tuple_elements_impl(std::index_sequence<Indices...>, Invocable& invocable,
                        Tuples&&... tuples) {
  return std::make_tuple(std::apply(
    [&](auto&&... args) {
      return std::invoke(invocable, *std::forward<decltype(args)>(args)...);
    },
    map_tuple_elements_get_addresses<Indices>(tuples...))...);
}

// Invoke function for each of the given tuples, capturing the result in a
// tuple itself.
template <class Invocable, class Tuple, class... Tuples>
constexpr auto
map_tuple_elements(Invocable& invocable, Tuple&& tuple, Tuples&&... tuples) {
  constexpr auto size = std::tuple_size_v<std::decay_t<Tuple>>;
  static_assert(((size == std::tuple_size_v<std::decay_t<Tuples>>) &&...),
                "tuple sizes must match exactly");
  return map_tuple_elements_impl(std::make_index_sequence<size>{}, invocable,
                                 std::forward<Tuple>(tuple),
                                 std::forward<Tuples>(tuples)...);
}

} // namespace detail

/// A typed view on a given set of columns of a table slice.
template <class... Types>
class projection final {
public:
  // TODO: Consider making this a virtual base class that has multiple
  // implementations depending on the table slice type.
  struct iterator final {
    // TODO: Consider making this a random access iterator.
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using reference = std::tuple<std::optional<view<Types>>...>;

    reference operator*() const noexcept {
      auto get = [&](auto column, auto type) noexcept
        -> std::optional<view<type_to_data<decltype(type)>>> {
        auto data = proj_.slice_.at(row_, column, std::move(type));
        if (caf::holds_alternative<caf::none_t>(data))
          return std::nullopt;
        return caf::get<view<type_to_data<decltype(type)>>>(data);
      };
      return detail::map_tuple_elements(
        get, proj_.indices_, std::make_tuple(data_to_type<Types>{}...));
    }

    iterator& operator++() noexcept {
      ++row_;
      return *this;
    }

    iterator operator++(int) & noexcept {
      auto result = *this;
      ++*this;
      return result;
    }

    friend bool operator==(const iterator& lhs, const iterator& rhs) noexcept {
      return &lhs.proj_ == &rhs.proj_ && lhs.row_ == rhs.row_;
    };

    friend bool operator!=(const iterator& lhs, const iterator& rhs) noexcept {
      return !(lhs == rhs);
    };

    iterator(const projection& proj, table_slice::size_type row) noexcept
      : proj_{proj}, row_{row} {
      // nop
    }

    template <class Inspector>
    friend auto inspect(Inspector& f, iterator& x) ->
      typename Inspector::result_type {
      return f(caf::meta::type_name("vast.projection.iterator"), x.proj_,
               x.row_);
    }

  private:
    const projection& proj_ = {};
    table_slice::size_type row_ = 0;
  };

  explicit operator bool() const noexcept {
    return std::all_of(indices_.begin(), indices_.end(),
                       [size = size()](table_slice::size_type index) noexcept {
                         return index >= 0 && index < size;
                       });
  }

  [[nodiscard]] table_slice::size_type size() const noexcept {
    return slice_.rows();
  }

  [[nodiscard]] iterator begin() const noexcept {
    if (*this)
      return {*this, 0};
    return end();
  }

  [[nodiscard]] iterator end() const noexcept {
    return {*this, size()};
  }

  projection(
    table_slice slice,
    std::array<table_slice::size_type, sizeof...(Types)> indices) noexcept
    : slice_{std::move(slice)}, indices_{indices} {
    // nop
  }

  template <class Inspector>
  friend auto inspect(Inspector& f, projection& x) ->
    typename Inspector::result_type {
    return f(caf::meta::type_name("vast.projection"), x.slice_, x.indices_);
  }

private:
  const table_slice slice_ = {};
  const std::array<table_slice::size_type, sizeof...(Types)> indices_;
};

template <class... Types, class... Hints>
projection<Types...> project(table_slice slice, Hints&&... hints) {
  static_assert(sizeof...(Types) == sizeof...(Hints),
                "project requires an equal number of types and hints");
  const auto& layout = slice.layout();
  auto find_flat_index_for_hint
    = [&](const auto& type, auto&& hint) noexcept -> table_slice::size_type {
    if constexpr (std::is_convertible_v<decltype(hint), offset>) {
      if (auto field = layout.at(hint))
        if (congruent(field->type, type))
          if (auto flat_index = layout.flat_index_at(hint))
            return *flat_index;
    } else if constexpr (std::is_constructible_v<std::string_view,
                                                 decltype(hint)>) {
      table_slice::size_type flat_index = 0;
      for (const auto& field : record_type::each{layout}) {
        const auto full_name = layout.name() + '.' + field.key();
        auto name_view = std::string_view{full_name};
        while (true) {
          if (name_view == hint)
            if (congruent(field.type(), type))
              return flat_index;
          auto colon = name_view.find_first_of('.');
          if (colon == std::string_view::npos)
            break;
          name_view.remove_prefix(colon + 1);
        }
        ++flat_index;
      }
    } else if constexpr (std::is_convertible_v<decltype(hint),
                                               table_slice::size_type>) {
      table_slice::size_type flat_index = 0;
      for (const auto& field : record_type::each{layout}) {
        if (flat_index == detail::narrow_cast<table_slice::size_type>(hint))
          if (congruent(field.type(), type))
            return flat_index;
        ++flat_index;
      }
    } else {
      static_assert(detail::always_false_v<decltype(hint)>,
                    "projection index must be convertible to 'offset', "
                    "'table_slice::size_type', or 'std::string_view'");
    }
    return static_cast<table_slice::size_type>(-1);
  };
  return {
    slice,
    {find_flat_index_for_hint(data_to_type<Types>{},
                              std::forward<Hints>(hints))...},
  };
}

} // namespace vast
