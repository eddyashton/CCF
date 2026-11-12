// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/json/glaze.h"

#include <glaze/json/read.hpp>
#include <glaze/json/write.hpp>
#include <string>
#include <string_view>

namespace ccf::json
{
  struct Options : glz::opts
  {
    bool validate_trailing_whitespace = true;
    bool escape_control_characters = true;
    bool concatenate = false;
    bool validate_skipped = true;
    bool replace_invalid_utf8 = false;
  };

  inline constexpr auto options = [] {
    Options value{};
    value.null_terminated = false;
    value.error_on_unknown_keys = false;
    value.error_on_missing_keys = true;
    value.skip_null_members = false;
    return value;
  }();

  namespace detail
  {
    template <auto Options, typename T>
    void write_with_options(const T& value, std::string& output)
    {
      const auto error = glz::write<Options>(value, output);
      if (error)
      {
        throw std::logic_error(glz::format_error(error, ""));
      }
    }
  }

  template <typename T>
  void write(const T& value, std::string& output)
  {
    detail::write_with_options<options>(value, output);
  }

  template <typename T>
  std::string write(const T& value)
  {
    std::string output;
    write(value, output);
    return output;
  }

  template <typename T>
  std::string write_for_response(const T& value)
  {
    constexpr auto response_options = [] {
      auto result = options;
      result.replace_invalid_utf8 = true;
      return result;
    }();
    std::string output;
    detail::write_with_options<response_options>(value, output);
    if (!glz::validate_utf8(output.data(), output.size()))
    {
      // Reuse the DOM's exact scalar-string replacement policy without
      // constructing a DOM for the serialized response payload.
      output = nlohmann::json::parse(
                 nlohmann::json(output).dump(
                   -1, ' ', false, nlohmann::json::error_handler_t::replace))
                 .get<std::string>();
    }
    return output;
  }

  template <typename T>
  void read(T& value, std::string_view input)
  {
    try
    {
      const auto error = glz::read<options>(value, input);
      if (error)
      {
        throw ccf::JsonParseError(glz::format_error(error, ""));
      }
    }
    catch (const nlohmann::json::exception& error)
    {
      throw ccf::JsonParseError(error.what());
    }
  }

  template <typename T>
  T read(std::string_view input)
  {
    T value{};
    read(value, input);
    return value;
  }
}
