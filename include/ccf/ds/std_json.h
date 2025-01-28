// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/crypto/base64.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

// Converters between nlohmann::json and std:: types

// TODO: Do we need both?
namespace std
{
  template <typename T>
  inline void to_json(nlohmann::json& j, const std::optional<T>& t)
  {
    if (t.has_value())
    {
      j = t.value();
    }
  }

  template <typename T>
  inline void from_json(const nlohmann::json& j, std::optional<T>& t)
  {
    if (!j.is_null())
    {
      t = j.get<T>();
    }
  }

  template <typename T>
  inline void to_json(nlohmann::json& j, const std::vector<T>& t)
  {
    if constexpr (std::is_same_v<T, uint8_t>)
    {
      j = ccf::crypto::b64_from_raw(t);
    }
    else
    {
      j = nlohmann::json::array();
      for (const auto& e : t)
      {
        j.push_back(e);
      }
    }
  }

  template <typename T>
  inline void from_json(const nlohmann::json& j, std::vector<T>& t)
  {
    if constexpr (std::is_same_v<T, uint8_t>)
    {
      if (j.is_string())
      {
        try
        {
          t = ccf::crypto::raw_from_b64(j.get<std::string>());
          return;
        }
        catch (const std::exception& e)
        {
          throw ccf::JsonParseError(fmt::format(
            "Vector of bytes object \"{}\" is not valid base64", j.dump()));
        }
      }
    }

    // Fall-through. So we can convert _from_ [1,2,3] to
    // std::vector<uint8_t>, but would prefer (and will produce in to_json) a
    // base64 string

    if (!j.is_array())
    {
      throw ccf::JsonParseError(
        fmt::format("Vector object \"{}\" is not an array", j.dump()));
    }

    for (size_t i = 0; i < j.size(); ++i)
    {
      try
      {
        t.push_back(j.at(i).template get<T>());
      }
      catch (ccf::JsonParseError& jpe)
      {
        jpe.pointer_elements.push_back(std::to_string(i));
        throw;
      }
    }
  }
};

namespace nlohmann
{
  namespace
  {
    template <typename T>
    inline void vector_from_json(const nlohmann::json& j, std::vector<T>& t)
    {
      if (!j.is_array())
      {
        throw ccf::JsonParseError(
          fmt::format("Vector object \"{}\" is not an array", j.dump()));
      }

      for (auto i = 0u; i < j.size(); ++i)
      {
        try
        {
          t.push_back(j.at(i).template get<T>());
        }
        catch (ccf::JsonParseError& jpe)
        {
          jpe.pointer_elements.push_back(std::to_string(i));
          throw;
        }
      }
    }
  }

  template <typename T>
  struct adl_serializer<std::optional<T>>
  {
    static inline void to_json(nlohmann::json& j, const std::optional<T>& t)
    {
      if (t.has_value())
      {
        j = t.value();
      }
    }

    static inline void from_json(const nlohmann::json& j, std::optional<T>& t)
    {
      if (!j.is_null())
      {
        t = j.get<T>();
      }
    }
  };

  template <typename T>
  struct adl_serializer<std::vector<T>>
  {
    static inline void to_json(nlohmann::json& j, const std::vector<T>& t)
    {
      j = nlohmann::json::array();
      for (const auto& e : t)
      {
        j.push_back(e);
      }
    }

    static inline void from_json(const nlohmann::json& j, std::vector<T>& t)
    {
      vector_from_json(j, t);
    }
  };

  template <>
  struct adl_serializer<std::vector<uint8_t>>
  {
    static inline void to_json(nlohmann::json& j, const std::vector<uint8_t>& t)
    {
      j = ccf::crypto::b64_from_raw(t);
    }

    static inline void from_json(
      const nlohmann::json& j, std::vector<uint8_t>& t)
    {
      if (j.is_string())
      {
        try
        {
          t = ccf::crypto::raw_from_b64(j.get<std::string>());
          return;
        }
        catch (const std::exception& e)
        {
          throw ccf::JsonParseError(fmt::format(
            "Vector of bytes object \"{}\" is not valid base64", j.dump()));
        }
      }

      // Fall-through. So we can convert _from_ [1,2,3] to
      // std::vector<uint8_t>, but would prefer (and will produce in to_json) a
      // base64 string

      vector_from_json(j, t);
    }
  };
}
