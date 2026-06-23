// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "http/http_parser.h"

#include <regex>
#include <stdexcept>

namespace http
{
  URL parse_url_full(const std::string& url)
  {
    LOG_TRACE_FMT("Received url to parse: {}", url);

    // From https://tools.ietf.org/html/rfc3986#appendix-B
    std::regex url_regex(
      "^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\\?([^#]*))?(#(.*))?");

    std::smatch match;
    if (!std::regex_match(url, match, url_regex))
    {
      throw std::invalid_argument(fmt::format("Unable to parse url: {}", url));
    }

    const auto host_port = match[4].str();

    // IPv6 hosts may contain colons, so only search for port after the closing
    // square bracket
    const auto closing_bracket = host_port.rfind(']');
    const auto port_delim_start =
      closing_bracket == std::string::npos ? 0 : closing_bracket;
    const auto port_delim = host_port.find(':', port_delim_start);

    URL u;
    u.scheme = match[2].str();
    u.host = host_port.substr(0, port_delim);
    if (port_delim != std::string::npos)
    {
      u.port = host_port.substr(port_delim + 1);
    }
    u.path = match[5].str();
    u.query = match[7].str();
    u.fragment = match[9].str();
    return u;
  }
}
