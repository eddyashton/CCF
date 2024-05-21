// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include <string>
#include <queue>
#include <map>
#include <llhttp/llhttp.h>
#include <endian.h>
#include <cctype>
#include <algorithm>

#include "http_builder.h"
#include "http2_types.h"
#include "enclave/tls_session.h"

namespace http
{
  class RequestProcessor
  {
  public:
    virtual void handle_request(
      llhttp_method method,
      const std::string_view& url,
      HeaderMap&& headers,
      std::vector<uint8_t>&& body,
      int32_t stream_id = http2::DEFAULT_STREAM_ID) = 0;
  };

  class ResponseProcessor
  {
  public:
    virtual void handle_response(
      http_status status, HeaderMap&& headers, std::vector<uint8_t>&& body) = 0;
  };
}