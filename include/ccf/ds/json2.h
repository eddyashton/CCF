// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/ds/json.h"

#include <cstdint>

struct JsonSerdeBehaviour
{
  using Flags = uint8_t;

  static constexpr Flags omit_write_if_default = 1 << 0;
  static constexpr Flags allow_read_if_missing = 1 << 1;

  static constexpr Flags always_required = 0;
  static constexpr Flags fully_optional =
    omit_write_if_default | allow_read_if_missing;
};

////////////////////////////////////////////////////////////////////////////////
// CCF_TO_JSON
#define CCF_TO_JSON_FOR_JSON_NEXT(TYPE, FLAGS, C_FIELD, JSON_FIELD) \
  { \
    if ( \
      ((FLAGS & JsonSerdeBehaviour::omit_write_if_default) == 0) || \
      (t.C_FIELD != t_default.C_FIELD)) \
    { \
      j[JSON_FIELD] = t.C_FIELD; \
    } \
  }

#define CCF_TO_JSON_FOR_JSON_FINAL(TYPE, FLAGS, C_FIELD, JSON_FIELD) \
  CCF_TO_JSON_FOR_JSON_NEXT(TYPE, FLAGS, C_FIELD, JSON_FIELD)
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// CCF_FROM_JSON
#define CCF_FROM_JSON_FOR_JSON_NEXT(TYPE, FLAGS, C_FIELD, JSON_FIELD) \
  { \
    const auto it = j.find(JSON_FIELD); \
    if (it == j.end()) \
    { \
      if constexpr ((FLAGS & JsonSerdeBehaviour::allow_read_if_missing) == 0) \
      { \
        throw ccf::JsonParseError( \
          "Missing required field '" JSON_FIELD "' in object: " + j.dump()); \
      } \
      else \
      { /* Missing value allowed */ \
      } \
    } \
    else \
    { \
      try \
      { \
        t.C_FIELD = it->get<decltype(TYPE::C_FIELD)>(); \
      } \
      catch (ccf::JsonParseError & jpe) \
      { \
        jpe.pointer_elements.push_back(JSON_FIELD); \
        throw; \
      } \
    } \
  }

#define CCF_FROM_JSON_FOR_JSON_FINAL(TYPE, FLAGS, C_FIELD, JSON_FIELD) \
  CCF_FROM_JSON_FOR_JSON_NEXT(TYPE, FLAGS, C_FIELD, JSON_FIELD)
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// CCF_FILL_SCHEMA
#define CCF_FILL_SCHEMA_FOR_JSON_NEXT(TYPE, FLAGS, C_FIELD, JSON_FIELD) \
  { \
    j["properties"][JSON_FIELD] = \
      ::ccf::ds::json::schema_element<decltype(TYPE::C_FIELD)>(); \
    if constexpr ((FLAGS & JsonSerdeBehaviour::allow_read_if_missing) == 0) \
    { \
      j["required"].push_back(JSON_FIELD); \
    } \
  }

#define CCF_FILL_SCHEMA_FOR_JSON_FINAL(TYPE, FLAGS, C_FIELD, JSON_FIELD) \
  CCF_FILL_SCHEMA_FOR_JSON_NEXT(TYPE, FLAGS, C_FIELD, JSON_FIELD)
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// CCF_ADD_COMPONENTS
#define CCF_ADD_COMPONENTS_FOR_JSON_NEXT(TYPE, FLAGS, C_FIELD, JSON_FIELD) \
  { \
    j["properties"][JSON_FIELD] = \
      ::ccf::ds::json::schema_element<decltype(TYPE::C_FIELD)>(); \
    if constexpr ((FLAGS & JsonSerdeBehaviour::allow_read_if_missing) == 0) \
    { \
      j["required"].push_back(JSON_FIELD); \
    } \
  }

#define CCF_ADD_COMPONENTS_FOR_JSON_FINAL(TYPE, FLAGS, C_FIELD, JSON_FIELD) \
  CCF_ADD_COMPONENTS_FOR_JSON_NEXT(TYPE, FLAGS, C_FIELD, JSON_FIELD)
////////////////////////////////////////////////////////////////////////////////

#define CCF_JSON_TYPE_(TYPE, BASE, ...) \
  inline void to_json(nlohmann::json& j, const TYPE& t) \
  { \
    if constexpr (!std::is_same_v<BASE, TYPE>) \
    { \
      to_json(j, static_cast<const BASE&>(t)); \
    } \
    if (!j.is_object()) \
    { \
      j = nlohmann::json::object(); \
    } \
    TYPE t_default; \
    _FOR_JSON_COUNT_NN(__VA_ARGS__)(POP3)(CCF_TO_JSON, TYPE, ##__VA_ARGS__) \
  } \
\
  inline void from_json(const nlohmann::json& j, TYPE& t) \
  { \
    if (!j.is_object()) \
    { \
      throw ccf::JsonParseError("Expected object, found: " + j.dump()); \
    } \
    if constexpr (!std::is_same_v<BASE, TYPE>) \
    { \
      from_json(j, static_cast<BASE&>(t)); \
    } \
    if (!j.is_object()) \
    { \
      throw ccf::JsonParseError("Expected object, found: " + j.dump()); \
    } \
    _FOR_JSON_COUNT_NN(__VA_ARGS__)(POP3)(CCF_FROM_JSON, TYPE, ##__VA_ARGS__) \
  } \
\
  inline void fill_json_schema(nlohmann::json& j, const TYPE* t) \
  { \
    if (!j.is_object()) \
    { \
      j = nlohmann::json::object(); \
    } \
    j["type"] = "object"; \
    if constexpr (!std::is_same_v<BASE, TYPE>) \
    { \
      fill_json_schema(j, static_cast<const BASE*>(t)); \
    } \
    _FOR_JSON_COUNT_NN(__VA_ARGS__) \
    (POP3)(CCF_FILL_SCHEMA, TYPE, ##__VA_ARGS__) \
  } \
\
  inline std::string schema_name(const TYPE*) \
  { \
    return #TYPE; \
  } \
\
  inline void add_schema_components( \
    ccf::ds::openapi::SchemaHelper& doc, nlohmann::json& j, const TYPE* t) \
  { \
    if (!j.is_object()) \
    { \
      j = nlohmann::json::object(); \
    } \
    j["type"] = "object"; \
    if constexpr (!std::is_same_v<BASE, TYPE>) \
    { \
      add_schema_components(doc, j, static_cast<const BASE*>(t)); \
    } \
    _FOR_JSON_COUNT_NN(__VA_ARGS__) \
    (POP3)(CCF_ADD_COMPONENTS, TYPE, ##__VA_ARGS__) \
  }

#define CCF_JSON_REQUIRED(x) JsonSerdeBehaviour::always_required, x, #x
#define CCF_JSON_REQUIRED_RENAME(x, j_field) \
  JsonSerdeBehaviour::always_required, x, j_field
#define CCF_JSON_OPTIONAL(x) JsonSerdeBehaviour::fully_optional, x, #x
#define CCF_JSON_OPTIONAL_RENAME(x, j_field) \
  JsonSerdeBehaviour::fully_optional, x, j_field

#define CCF_JSON_OMIT_DEFAULT(x) \
  JsonSerdeBehaviour::omit_write_if_default, x, #x
#define CCF_JSON_OMIT_DEFAULT_RENAME(x, j_field) \
  JsonSerdeBehaviour::omit_write_if_default, x, x
#define CCF_JSON_ALLOW_MISSING(x) \
  JsonSerdeBehaviour::allow_read_if_missing, x, #x
#define CCF_JSON_ALLOW_MISSING_RENAME(x, j_field) \
  JsonSerdeBehaviour::allow_read_if_missing, x, x

#define CCF_JSON_TYPE(TYPE, ...) CCF_JSON_TYPE_(TYPE, TYPE, __VA_ARGS__)
#define CCF_JSON_TYPE_WITH_BASE(TYPE, BASE, ...) \
  CCF_JSON_TYPE_(TYPE, BASE, __VA_ARGS__)