// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include <expected>

constexpr auto concepts_before_json = __cpp_concepts;

#include "ccf/ds/openapi.h"
#include "ccf/json.h"

#include <doctest/doctest.h>
#include <glaze/json/schema.hpp>

static_assert(__cpp_concepts == concepts_before_json);
static_assert(*std::expected<int, int>{7} == 7);

TEST_CASE("JSON headers preserve expected and schema include order")
{
  const auto good = glz::read_json<int>(std::string{"7"});
  const auto bad = glz::read_json<int>(std::string{"{"});
  REQUIRE(good.has_value());
  CHECK(*good == 7);
  CHECK_FALSE(bad.has_value());
  CHECK(static_cast<bool>(bad.error()));
  CHECK_THROWS_AS(bad.value(), std::bad_expected_access<glz::error_ctx>);
  const auto native_schema = glz::write_json_schema<int>();
  REQUIRE(native_schema.has_value());
  CHECK(nlohmann::json::parse(*native_schema).at("type") == "integer");
}
