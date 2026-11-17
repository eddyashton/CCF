// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/json/metadata.h"
#include "ccf/json/primitives.h"

#include <version>

// Clang 18's feature macro hides libstdc++ 13's otherwise supported expected.
#if defined(__clang__) && defined(__GLIBCXX__)
#  if __clang_major__ == 18 && _GLIBCXX_RELEASE == 13 && \
    __cplusplus > 202002L && __cpp_concepts == 201907L && \
    !defined(__cpp_lib_expected)
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wbuiltin-macro-redefined"
#    pragma push_macro("__cpp_concepts")
#    undef __cpp_concepts
#    define __cpp_concepts 202002L
// A prior include may have set the guard without defining std::expected.
#    ifdef _GLIBCXX_EXPECTED
#      undef _GLIBCXX_EXPECTED
#    endif
#    include <expected>
#    pragma pop_macro("__cpp_concepts")
#    pragma clang diagnostic pop
#  endif
#endif

#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>
#include <map>
#include <unordered_map>

namespace ccf::json::detail
{
  using Json = nlohmann::json;

  template <Declared T>
  constexpr auto glaze_fields()
  {
    constexpr auto declaration = Metadata<T>::declaration;
    if constexpr (std::is_enum_v<T>)
    {
      return [&]<size_t... I>(std::index_sequence<I...>) {
        return std::apply(
          [](auto... args) { return glz::enumerate(args...); },
          std::tuple_cat(std::tuple{
            declaration.entries[I].second, declaration.entries[I].first}...));
      }(std::make_index_sequence<declaration.entries.size()>{});
    }
    else
    {
      return std::apply(
        [](auto... field) {
          return std::apply(
            [](auto... args) { return glz::object(args...); },
            std::tuple_cat(std::tuple{field.name, decltype(field)::member}...));
        },
        declaration.fields);
    }
  }

  template <Declared T>
  std::optional<std::string_view> enum_label(T value)
    requires std::is_enum_v<T>
  {
    for (const auto& [entry, label] : Metadata<T>::declaration.entries)
    {
      if (entry == value)
      {
        return label;
      }
    }
    return std::nullopt;
  }

  template <typename T>
  inline constexpr bool standard_container = false;

  template <typename T>
  inline constexpr bool standard_container<std::optional<T>> = true;

  template <typename T, typename Allocator>
  inline constexpr bool standard_container<std::vector<T, Allocator>> = true;

  template <typename... Args>
  inline constexpr bool standard_container<std::map<Args...>> = true;

  template <typename... Args>
  inline constexpr bool standard_container<std::unordered_map<Args...>> = true;

  template <typename... Args>
  inline constexpr bool standard_container<std::set<Args...>> = true;

  template <typename... Args>
  inline constexpr bool standard_container<std::unordered_set<Args...>> = true;

  template <typename... Args>
  inline constexpr bool standard_container<std::tuple<Args...>> = true;

  template <typename... Args>
  inline constexpr bool standard_container<std::pair<Args...>> = true;

  template <typename... Args>
  inline constexpr bool standard_container<std::variant<Args...>> = true;

  template <typename T, size_t N>
  inline constexpr bool standard_container<std::array<T, N>> = true;

  template <typename T>
  inline constexpr bool legacy_map = false;

  template <typename T>
  inline constexpr bool legacy_pair = false;

  template <typename First, typename Second>
  inline constexpr bool legacy_pair<std::pair<First, Second>> = true;

  template <typename Key, typename Value, typename... Args>
  inline constexpr bool legacy_map<std::map<Key, Value, Args...>> =
    std::is_constructible_v<Json, const std::map<Key, Value, Args...>&>;

  template <typename Key, typename Value, typename... Args>
  inline constexpr bool legacy_map<std::unordered_map<Key, Value, Args...>> =
    std::
      is_constructible_v<Json, const std::unordered_map<Key, Value, Args...>&>;

  // A DOM contract, including adl_serializer specializations, takes precedence
  // over aggregate reflection. Undeclared enums keep their DOM wire format.
  // Standard containers keep native Glaze I/O, except the CCF byte wire format.
  template <typename T>
  concept AdlCandidate =
    !Declared<T> && (std::is_class_v<T> || std::is_enum_v<T>) &&
    !nlohmann::detail::is_basic_json<T>::value && !glz::str_t<T> &&
    !standard_container<T> && std::is_constructible_v<Json, const T&>;

  template <typename T>
  concept AdlLeaf = AdlCandidate<T> && !glz::glaze_t<T>;

  template <typename T>
  concept DomCandidate =
    std::same_as<T, Json> || std::same_as<T, std::vector<uint8_t>> ||
    std::same_as<T, std::vector<char>> || std::same_as<T, char> ||
    glz::array_char_t<T> || legacy_map<T> || legacy_pair<T> || AdlCandidate<T>;

  template <typename T>
  concept DomAdapter = DomCandidate<T> && (!AdlCandidate<T> || AdlLeaf<T>);
}

namespace glz
{
  template <ccf::json::detail::Declared T>
  struct meta<T> : ccf::json::detail::Metadata<T>
  {
    static constexpr auto value = ccf::json::detail::glaze_fields<T>();
    static constexpr bool custom_write =
      std::is_enum_v<T> || meta::has_optional_fields;
  };

  template <ccf::json::detail::DomCandidate T>
  struct meta<T>
  {
    static constexpr bool glaze_reflect = false;
    static constexpr bool custom_read = true;
    static constexpr bool custom_write = true;
  };

  template <ccf::json::detail::Declared T>
    requires(
      !std::is_enum_v<T> && ccf::json::detail::Metadata<T>::has_optional_fields)
  struct to<JSON, T>
  {
    static constexpr bool can_error = true;

    template <auto Options>
    static void op(
      const T& value, is_context auto&& context, auto& output, auto& index)
    {
      using namespace ccf::json::detail;
      using Fields =
        std::remove_cvref_t<decltype(Metadata<T>::declaration.fields)>;
      DefaultCache<typename DefaultOwners<std::tuple<>, Fields>::Type>
        defaults{};
      if (!ensure_space(
            context, output, index + context.depth + write_padding_bytes))
      {
        return;
      }
      if constexpr (!check_opening_handled(Options))
      {
        dump('{', output, index);
      }
      if constexpr (Options.prettify)
      {
        context.depth += check_indentation_width(Options);
      }
      bool first = true;
      std::apply(
        [&](auto... field) {
          const auto write_field = [&](auto f) {
            const auto& member = value.*decltype(f)::member;
            if (
              context.error != error_code::none ||
              omit_field(member, f, defaults))
            {
              return;
            }
            if (!ensure_space(
                  context, output, index + context.depth + write_padding_bytes))
            {
              return;
            }
            if (!first)
            {
              dump(',', output, index);
            }
            first = false;
            if constexpr (Options.prettify)
            {
              dump_newline_indent(
                check_indentation_char(Options), context.depth, output, index);
            }
            write_pair_content<Options>(f.name, member, context, output, index);
          };
          (write_field(field), ...);
        },
        Metadata<T>::declaration.fields);
      if (context.error != error_code::none)
      {
        return;
      }
      if constexpr (Options.prettify)
      {
        context.depth -= check_indentation_width(Options);
      }
      if (!ensure_space(
            context, output, index + context.depth + write_padding_bytes))
      {
        return;
      }
      if constexpr (!check_closing_handled(Options))
      {
        if constexpr (Options.prettify)
        {
          if (!first)
          {
            dump_newline_indent(
              check_indentation_char(Options), context.depth, output, index);
          }
        }
        dump('}', output, index);
      }
    }
  };

  template <>
  struct meta<std::vector<uint8_t>>
  {
    static constexpr bool glaze_reflect = false;
    static constexpr std::string_view name = "std::vector<uint8_t>";
    static constexpr bool custom_read = true;
    static constexpr bool custom_write = true;
  };

  template <typename Key, typename Value, typename... Args>
    requires ccf::json::detail::legacy_map<std::map<Key, Value, Args...>>
  struct meta<std::map<Key, Value, Args...>>
  {
    static constexpr bool glaze_reflect = false;
    static constexpr bool custom_read = true;
    static constexpr bool custom_write = true;
  };

  template <typename Key, typename Value>
    requires ccf::json::detail::legacy_map<std::map<Key, Value>>
  struct meta<std::map<Key, Value>>
  {
    static constexpr bool glaze_reflect = false;
    static constexpr bool custom_read = true;
    static constexpr bool custom_write = true;
  };

  template <typename Key, typename Value, typename... Args>
    requires ccf::json::detail::legacy_map<
      std::unordered_map<Key, Value, Args...>>
  struct meta<std::unordered_map<Key, Value, Args...>>
  {
    static constexpr bool glaze_reflect = false;
    static constexpr bool custom_read = true;
    static constexpr bool custom_write = true;
  };

  // Glaze normally writes unmapped enums as numbers; CCF rejects them.
  template <ccf::json::detail::Declared T>
    requires std::is_enum_v<T>
  struct to<JSON, T>
  {
    static constexpr bool can_error = true;

    template <auto Options>
    static void op(T value, is_context auto&& context, auto&&... args)
    {
      const auto label = ccf::json::detail::enum_label(value);
      if (!label)
      {
        context.error = error_code::unexpected_enum;
        return;
      }
      serialize<JSON>::op<Options>(
        *label, context, std::forward<decltype(args)>(args)...);
    }
  };

  template <typename T>
  struct DomWriter
  {
    template <auto Options>
    static void op(const T& value, is_context auto&& context, auto&&... args)
    {
      constexpr auto error_handler = [] {
        if constexpr (requires { Options.replace_invalid_utf8; })
        {
          if constexpr (Options.replace_invalid_utf8)
          {
            return nlohmann::json::error_handler_t::replace;
          }
        }
        return nlohmann::json::error_handler_t::strict;
      }();
      const raw_json raw{
        ccf::json::detail::Json(value).dump(-1, ' ', false, error_handler)};
      serialize<JSON>::op<Options>(
        raw, context, std::forward<decltype(args)>(args)...);
    }
  };

  template <typename T>
  struct DomReader
  {
    template <auto Options>
    static void op(T& value, is_context auto&& context, auto& it, auto end)
    {
      const auto start = it;
      skip_value<JSON>::op<Options>(context, it, end);
      if (
        context.error != error_code::none &&
        context.error != error_code::end_reached)
      {
        return;
      }
      const auto dom = ccf::parse_json_safe(start, it);
      if constexpr (std::same_as<T, ccf::json::detail::Json>)
      {
        value = dom;
      }
      else if constexpr (std::same_as<T, std::vector<uint8_t>>)
      {
        value = dom.template get<T>();
      }
      else
      {
        dom.get_to(value);
      }
    }
  };

  template <ccf::json::detail::DomAdapter T>
  struct to<JSON, T> : DomWriter<T>
  {};

  template <ccf::json::detail::DomAdapter T>
  struct from<JSON, T> : DomReader<T>
  {};

  template <ccf::json::detail::DomAdapter T>
    requires nullable_like<T>
  struct to<JSON, T> : DomWriter<T>
  {};

  template <ccf::json::detail::DomAdapter T>
    requires nullable_t<T>
  struct from<JSON, T> : DomReader<T>
  {};

  template <>
  struct to<JSON, char> : DomWriter<char>
  {};

  template <>
  struct from<JSON, char> : DomReader<char>
  {};

  template <>
  struct to<JSON, std::vector<char>> : DomWriter<std::vector<char>>
  {};

  template <>
  struct from<JSON, std::vector<char>> : DomReader<std::vector<char>>
  {};

  template <typename First, typename Second>
  struct to<JSON, std::pair<First, Second>>
    : DomWriter<std::pair<First, Second>>
  {};

  template <typename First, typename Second>
  struct from<JSON, std::pair<First, Second>>
    : DomReader<std::pair<First, Second>>
  {};

  template <size_t N>
  struct to<JSON, std::array<char, N>> : DomWriter<std::array<char, N>>
  {};

  template <size_t N>
  struct from<JSON, std::array<char, N>> : DomReader<std::array<char, N>>
  {};
}
