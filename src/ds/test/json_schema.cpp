// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "ccf/ds/json2.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
#include <vector>

struct Foo
{
  size_t n_0 = 42;
  size_t n_1 = 43;
  int i_0 = -1;
  int64_t i64_0 = -2;
  std::string s_0 = "Default value";
  std::string s_1 = "Other default value";
  std::optional<size_t> opt = std::nullopt;
  std::vector<std::string> vec_s = {};
  size_t ignored;
};
DECLARE_JSON_TYPE_WITH_OPTIONAL_FIELDS(Foo);
DECLARE_JSON_REQUIRED_FIELDS(Foo, n_0, i_0, i64_0, s_0);
DECLARE_JSON_OPTIONAL_FIELDS(Foo, n_1, s_1, opt, vec_s);

TEST_CASE("schema generation")
{
  const auto schema = ccf::ds::json::build_schema<Foo>("Foo");

  const auto title_it = schema.find("title");
  REQUIRE(title_it != schema.end());
  REQUIRE(title_it.value() == "Foo");

  const auto properties_it = schema.find("properties");
  REQUIRE(properties_it != schema.end());

  const auto required_it = schema.find("required");
  REQUIRE(required_it != schema.end());

  REQUIRE(required_it->is_array());
  REQUIRE(required_it->size() == 4);

  // Check limits are actually achievable
  {
    auto j_max = nlohmann::json::object();
    auto j_min = nlohmann::json::object();
    for (const std::string& required : *required_it)
    {
      const auto property_it = properties_it->find(required);
      REQUIRE(property_it != properties_it->end());

      const auto type = property_it->at("type");
      if (type == "integer")
      {
        j_min[required] = property_it->at("minimum");
        j_max[required] = property_it->at("maximum");
      }
      else if (type == "string")
      {
        j_min[required] = "Hello world";
        j_max[required] = "Hello world";
      }
      else
      {
        throw std::logic_error("Unsupported type");
      }
    }

    const auto foo_min = j_min.get<Foo>();
    const auto foo_max = j_max.get<Foo>();

    using size_limits = std::numeric_limits<size_t>;

    REQUIRE(foo_min.n_0 == size_limits::min());
    REQUIRE(foo_max.n_0 == size_limits::max());

    using int_limits = std::numeric_limits<int>;
    REQUIRE(foo_min.i_0 == int_limits::min());
    REQUIRE(foo_max.i_0 == int_limits::max());

    using int64_limits = std::numeric_limits<int64_t>;
    REQUIRE(foo_min.i64_0 == int64_limits::min());
    REQUIRE(foo_max.i64_0 == int64_limits::max());
  }
}

TEST_CASE_TEMPLATE("schema types, integer", T, size_t, ssize_t)
{
  std::map<T, std::string> m;
  const auto schema = ccf::ds::json::build_schema<decltype(m)>("Map");

  REQUIRE(schema["type"] == "array");
  REQUIRE(schema["items"].is_object());

  REQUIRE(schema["items"]["type"] == "array");
  REQUIRE(schema["items"]["items"].is_array());
  REQUIRE(schema["items"]["items"].size() == 2);
  REQUIRE(schema["items"]["items"][0]["type"] == "integer");
  REQUIRE(schema["items"]["items"][1]["type"] == "string");
}

TEST_CASE_TEMPLATE("schema types, floating point", T, float, double)
{
  std::map<size_t, T> m;
  const auto schema = ccf::ds::json::build_schema<decltype(m)>("Map");

  REQUIRE(schema["type"] == "array");
  REQUIRE(schema["items"].is_object());

  REQUIRE(schema["items"]["type"] == "array");
  REQUIRE(schema["items"]["items"].is_array());
  REQUIRE(schema["items"]["items"].size() == 2);
  REQUIRE(schema["items"]["items"][0]["type"] == "integer");
  REQUIRE(schema["items"]["items"][1]["type"] == "number");
}

namespace custom
{
  namespace user
  {
    namespace defined
    {
      struct X
      {
        std::string email;
      };

      void fill_json_schema(nlohmann::json& schema, const X*)
      {
        schema["type"] = "string";
        schema["format"] = "email";
      }

      struct Y
      {
        size_t a;
        int b;
      };
      DECLARE_JSON_TYPE(Y);
      DECLARE_JSON_REQUIRED_FIELDS(Y, a, b);
    }
  }
}

TEST_CASE("custom elements")
{
  const auto x_schema =
    ccf::ds::json::build_schema<custom::user::defined::X>("custom-x");
  REQUIRE(x_schema["format"] == "email");

  const auto y_schema =
    ccf::ds::json::build_schema<custom::user::defined::Y>("custom-y");
  REQUIRE(y_schema["required"].size() == 2);
}

struct Nest0
{
  size_t n = {};
};
DECLARE_JSON_TYPE(Nest0);
DECLARE_JSON_REQUIRED_FIELDS(Nest0, n);

bool operator==(const Nest0& l, const Nest0& r)
{
  return l.n == r.n;
}

struct Nest1
{
  Nest0 a = {};
  Nest0 b = {};
};
DECLARE_JSON_TYPE(Nest1);
DECLARE_JSON_REQUIRED_FIELDS(Nest1, a, b);

bool operator==(const Nest1& l, const Nest1& r)
{
  return l.a == r.a && l.b == r.b;
}

struct Bar
{
  size_t a = {};
  std::string b = {};
  size_t c = {};
};
CCF_JSON_TYPE(
  Bar, CCF_JSON_REQUIRED(a), CCF_JSON_OPTIONAL(b), CCF_JSON_OPTIONAL(c));

struct Biz : public Bar
{
  size_t f = {};
};
CCF_JSON_TYPE_WITH_BASE(Biz, Bar, CCF_JSON_REQUIRED(f))

struct Baz : public Bar
{
  size_t d = {};
  size_t e = {};
};
CCF_JSON_TYPE_WITH_BASE(Baz, Bar, CCF_JSON_REQUIRED(d), CCF_JSON_OPTIONAL(e))

TEST_CASE("macro parser generation with base classes")
{
  const Biz default_biz = {};
  const Baz default_baz = {};
  nlohmann::json j;

  REQUIRE_THROWS_AS(j.get<Biz>(), std::invalid_argument);
  REQUIRE_THROWS_AS(j.get<Baz>(), std::invalid_argument);

  j["a"] = 42;

  REQUIRE_THROWS_AS(j.get<Biz>(), std::invalid_argument);
  REQUIRE_THROWS_AS(j.get<Baz>(), std::invalid_argument);

  j["f"] = 44;
  const Biz biz_0 = j;
  REQUIRE(biz_0.a == j["a"]);
  REQUIRE(biz_0.b == default_biz.b);
  REQUIRE(biz_0.c == default_biz.c);
  REQUIRE(biz_0.f == j["f"]);

  j["d"] = 43;
  const Baz baz_0 = j;
  REQUIRE(baz_0.a == j["a"]);
  REQUIRE(baz_0.b == default_baz.b);
  REQUIRE(baz_0.c == default_baz.c);
  REQUIRE(baz_0.d == j["d"]);
  REQUIRE(baz_0.e == default_baz.e);

  j["b"] = "Test";
  j["c"] = 100;
  j["e"] = 101;
  const Baz baz_1 = j;
  REQUIRE(baz_1.a == j["a"]);
  REQUIRE(baz_1.b == j["b"]);
  REQUIRE(baz_1.c == j["c"]);
  REQUIRE(baz_1.d == j["d"]);
  REQUIRE(baz_1.e == j["e"]);
}
