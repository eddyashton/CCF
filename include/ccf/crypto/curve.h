// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/crypto/md_type.h"
#include "ccf/ds/enum_formatter.h"
#include "ccf/ds/json.h"

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <stdexcept>
#include <string>

namespace ccf::crypto
{
  // SNIPPET_START: supported_curves
  enum class CurveID : uint8_t
  {
    /// No curve
    NONE = 0,
    /// The SECP384R1 curve
    SECP384R1,
    /// The SECP256R1 curve
    SECP256R1,
    /// The CURVE25519 curve
    CURVE25519,
    X25519
  };
  template <typename BasicJsonType>
  inline void to_json(BasicJsonType& j, const CurveID& e)
  {
    static_assert(
      std::is_enum_v<CurveID>,
      "CurveID"
      " must be an enum!");
    static const std::pair<CurveID, BasicJsonType> m[] = {
      {CurveID::NONE, "None"},
      {CurveID::SECP384R1, "Secp384R1"},
      {CurveID::SECP256R1, "Secp256R1"},
      {CurveID::CURVE25519, "Curve25519"},
      {CurveID::X25519, "X25519"}};
    auto it = std::find_if(
      std::begin(m),
      std::end(m),
      [e](const std::pair<CurveID, BasicJsonType>& ej_pair) -> bool {
        return ej_pair.first == e;
      });
    if (it == std::end(m))
    {
      throw ccf::JsonParseError(fmt::format(
        "Value {} in enum "
        "CurveID"
        " has no specified JSON conversion",
        (size_t)e));
    }
    j = it->second;
  }
  template <typename BasicJsonType>
  inline void from_json(const BasicJsonType& j, CurveID& e)
  {
    static_assert(
      std::is_enum_v<CurveID>,
      "CurveID"
      " must be an enum!");
    static const std::pair<CurveID, BasicJsonType> m[] = {
      {CurveID::NONE, "None"},
      {CurveID::SECP384R1, "Secp384R1"},
      {CurveID::SECP256R1, "Secp256R1"},
      {CurveID::CURVE25519, "Curve25519"},
      {CurveID::X25519, "X25519"}};
    auto it = std::find_if(
      std::begin(m),
      std::end(m),
      [&j](const std::pair<CurveID, BasicJsonType>& ej_pair) -> bool {
        return ej_pair.second == j;
      });
    if (it == std::end(m))
    {
      throw ccf::JsonParseError(fmt::format(
        "{} is not convertible to "
        "CurveID",
        j.dump()));
    }
    e = it->first;
  }
  inline std::string schema_name(const CurveID*)
  {
    return "CurveID";
  }
  inline void fill_enum_schema(nlohmann::json& j, const CurveID*)
  {
    static const std::pair<CurveID, nlohmann::json> m[] = {
      {CurveID::NONE, "None"},
      {CurveID::SECP384R1, "Secp384R1"},
      {CurveID::SECP256R1, "Secp256R1"},
      {CurveID::CURVE25519, "Curve25519"},
      {CurveID::X25519, "X25519"}};
    auto enums = nlohmann::json::array();
    for (const auto& p : m)
    {
      enums.push_back(p.second);
    }
    j["enum"] = enums;
    j["type"] = "string";
  }
  inline auto format_as(CurveID val)
  {
    switch (val)
    { ADD_FORMAT_CASE_FOR_JSON_NEXT(CurveID, {{CurveID::NONE) ADD_FORMAT_CASE_FOR_JSON_NEXT(CurveID, "None"}) ADD_FORMAT_CASE_FOR_JSON_NEXT(CurveID, {CurveID::SECP384R1) ADD_FORMAT_CASE_FOR_JSON_NEXT(CurveID, "Secp384R1"}) ADD_FORMAT_CASE_FOR_JSON_NEXT(CurveID, {CurveID::SECP256R1) ADD_FORMAT_CASE_FOR_JSON_NEXT(CurveID, "Secp256R1"}) ADD_FORMAT_CASE_FOR_JSON_NEXT(CurveID, {CurveID::CURVE25519) ADD_FORMAT_CASE_FOR_JSON_NEXT(CurveID, "Curve25519"}) ADD_FORMAT_CASE_FOR_JSON_NEXT(CurveID, {CurveID::X25519) ADD_FORMAT_CASE_FOR_JSON_FINAL(CurveID, "X25519"}}) default:
      {
        static_assert(nonstd::dependent_false<CurveID>::value,
                      "Unhandled value in formatting macro";)
      }
    }
  };
  DECLARE_JSON_ENUM(
    CurveID,
    {{CurveID::NONE, "None"},
     {CurveID::SECP384R1, "Secp384R1"},
     {CurveID::SECP256R1, "Secp256R1"},
     {CurveID::CURVE25519, "Curve25519"},
     {CurveID::X25519, "X25519"}});

  static constexpr CurveID service_identity_curve_choice = CurveID::SECP384R1;
  // SNIPPET_END: supported_curves

  // Get message digest algorithm to use for given elliptic curve
  inline MDType get_md_for_ec(CurveID ec)
  {
    switch (ec)
    {
      case CurveID::SECP384R1:
        return MDType::SHA384;
      case CurveID::SECP256R1:
        return MDType::SHA256;
      default:
      {
        throw std::logic_error(fmt::format("Unhandled CurveID: {}", ec));
      }
    }
  }
}