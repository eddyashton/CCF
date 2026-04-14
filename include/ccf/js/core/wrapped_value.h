// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/js/core/constants.h"

#include <quickjs/quickjs.h>
#include <string>

namespace ccf::js::core
{
  struct JSWrappedValue
  {
    JSContext* ctx;
    JSValue val;

    JSWrappedValue();
    JSWrappedValue(const JSWrappedValue& other);
    JSWrappedValue(JSWrappedValue&& other) noexcept;
    ~JSWrappedValue();

    /// Takes ownership of one refcount on val (no JS_DupValue)
    [[nodiscard]] static JSWrappedValue take(JSContext* ctx, JSValue val);
    /// Acquires a new refcount on val (calls JS_DupValue)
    [[nodiscard]] static JSWrappedValue copy(JSContext* ctx, JSValue val);

    JSWrappedValue& operator=(const JSWrappedValue& other);

    JSWrappedValue operator[](const char* prop) const;

    JSWrappedValue operator[](const std::string& prop) const;

    JSWrappedValue operator[](uint32_t i) const;

    [[nodiscard]] int set(const char* prop, JSWrappedValue&& value) const;

    [[nodiscard]] int set_getter(
      const char* prop, JSWrappedValue&& getter) const;

    [[nodiscard]] int set(
      const std::string& prop, JSWrappedValue&& value) const;

    [[nodiscard]] int set(const std::string& prop, JSValue value) const;

    [[nodiscard]] int set_null(const std::string& prop) const;

    [[nodiscard]] int set_uint32(const std::string& prop, uint32_t i) const;

    [[nodiscard]] int set_int64(const std::string& prop, int64_t i) const;

    [[nodiscard]] int set_bool(const std::string& prop, bool b) const;

    [[nodiscard]] int set_at_index(
      uint32_t index, JSWrappedValue&& value) const;

    [[nodiscard]] bool is_exception() const;

    [[nodiscard]] bool is_error() const;

    [[nodiscard]] bool is_obj() const;

    [[nodiscard]] bool is_str() const;

    [[nodiscard]] bool is_true() const;

    [[nodiscard]] bool is_undefined() const;

    [[nodiscard]] JSValue take();
  };
}
