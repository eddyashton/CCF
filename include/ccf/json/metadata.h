// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ccf::json::detail
{
  enum class Presence : std::uint8_t
  {
    Required,
    Optional,
    AllowMissing
  };

  template <typename Owners, typename Fields>
  struct DefaultOwners;

  template <typename... Owners>
  struct DefaultOwners<std::tuple<Owners...>, std::tuple<>>
  {
    using Type = std::tuple<Owners...>;
  };

  template <typename... Owners, typename F, typename... Rest>
  struct DefaultOwners<std::tuple<Owners...>, std::tuple<F, Rest...>>
    : DefaultOwners<
        std::conditional_t<
          F::policy == Presence::Optional &&
            !(std::is_same_v<typename F::DefaultType, Owners> || ...),
          std::tuple<Owners..., typename F::DefaultType>,
          std::tuple<Owners...>>,
        std::tuple<Rest...>>
  {};

  template <typename Owners>
  struct DefaultCache;

  template <typename... Owners>
  struct DefaultCache<std::tuple<Owners...>>
  {
    std::tuple<std::optional<Owners>...> values;
  };

  template <typename Value, typename F, typename Cache>
  bool omit_field(const Value& member, F, Cache& defaults)
  {
    if constexpr (F::policy == Presence::Optional)
    {
      auto& owner =
        std::get<std::optional<typename F::DefaultType>>(defaults.values);
      if (!owner.has_value())
      {
        owner.emplace();
      }
      return !(member != (*owner).*F::member);
    }
    return false;
  }

  template <auto Member, Presence Policy, typename Defaults = void>
  struct Field
  {
    using DefaultType = Defaults;
    std::string_view name;
    static constexpr auto member = Member;
    static constexpr auto policy = Policy;
  };

  template <typename T, typename Fields, typename Entries = std::tuple<>>
  struct Declaration
  {
    using Type = T;
    std::string_view name;
    Fields fields;
    Entries entries;
  };

  template <typename T, typename Fields>
  constexpr auto declare(std::string_view name, Fields fields)
  {
    const auto resolved = std::apply(
      [](auto... field) {
        const auto bind_default = [](auto f) {
          using F = decltype(f);
          if constexpr (
            F::policy == Presence::Optional &&
            std::is_void_v<typename F::DefaultType>)
          {
            return Field<F::member, F::policy, T>{f.name};
          }
          else
          {
            return f;
          }
        };
        return std::tuple{bind_default(field)...};
      },
      fields);
    return Declaration<T, std::remove_cvref_t<decltype(resolved)>>{
      name, resolved, {}};
  }

  template <typename T, size_t N>
  constexpr auto declare_enum(
    std::string_view name,
    const std::array<std::pair<T, std::string_view>, N>& entries)
  {
    return Declaration<T, std::tuple<>, std::remove_cvref_t<decltype(entries)>>{
      name, {}, entries};
  }

  template <typename T, typename... Args>
  constexpr auto declare_enum(std::string_view name, Args... args)
  {
    static_assert(sizeof...(Args) % 2 == 0, "Expected enum label/value pairs");
    const auto values = std::tuple{args...};
    return [&]<size_t... I>(std::index_sequence<I...>) {
      return declare_enum<T>(
        name,
        std::array<std::pair<T, std::string_view>, sizeof...(I)>{
          std::pair<T, std::string_view>{
            std::get<(2 * I) + 1>(values), std::get<2 * I>(values)}...});
    }(std::make_index_sequence<sizeof...(Args) / 2>{});
  }

  template <typename T>
  concept Declared = requires {
    requires std::is_same_v<
      typename decltype(ccf_json_declaration(
        static_cast<const T*>(nullptr)))::Type,
      T>;
  };

  template <Declared T>
  struct Metadata
  {
    static constexpr auto declaration =
      ccf_json_declaration(static_cast<const T*>(nullptr));
    static constexpr auto name = declaration.name;
    static constexpr bool has_optional_fields = std::apply(
      [](auto... field) {
        return ((decltype(field)::policy == Presence::Optional) || ...);
      },
      declaration.fields);

    static constexpr bool requires_key(
      std::string_view key, [[maybe_unused]] bool nullable)
    {
      return std::apply(
        [key](auto... field) {
          return (
            (key == field.name &&
             decltype(field)::policy == Presence::Required) ||
            ...);
        },
        declaration.fields);
    }
  };

}
