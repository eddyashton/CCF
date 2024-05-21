// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include <vector>
#include <string>
#include <stdint.h>
#include <optional>

#include "ccf/service/map.h"

namespace ccf
{
  using Module = std::string;
  using Modules = kv::RawCopySerialisedMap<std::string, Module>;
  using ModulesQuickJsBytecode =
    kv::RawCopySerialisedMap<std::string, std::vector<uint8_t>>;
  using ModulesQuickJsVersion = kv::RawCopySerialisedValue<std::string>;
  using InterpreterFlush = ServiceValue<bool>;

  namespace Tables
  {
    static constexpr auto MODULES = "public:ccf.gov.modules";
    static constexpr auto MODULES_QUICKJS_BYTECODE =
      "public:ccf.gov.modules_quickjs_bytecode";
    static constexpr auto MODULES_QUICKJS_VERSION =
      "public:ccf.gov.modules_quickjs_version";
    static constexpr auto INTERPRETER_FLUSH =
      "public:ccf.gov.interpreter.flush";
  }
}