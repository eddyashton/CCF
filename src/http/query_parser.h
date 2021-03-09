// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include <stdexcept>

namespace http
{
  class UrlQueryParseError : public std::invalid_argument
  {
  public:
    using std::invalid_argument::invalid_argument;
  };

  // TODO: Add a string_view -> multimap (split at "&", split each into k=v at
  // "=") converter function, replace jsonhandler::get_params_from_query with
  // this
}
