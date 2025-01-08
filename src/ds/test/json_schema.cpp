// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "ccf/ds/json2.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <nlohmann/json.hpp>
#include <vector>

struct Bar
{
  size_t a = {};
  std::string b = {};
  size_t c = {};
};
CCF_JSON_TYPE(
  Bar, CCF_JSON_REQUIRED(a), CCF_JSON_OPTIONAL(b), CCF_JSON_OPTIONAL(c));

TEST_CASE("basic macro parser generation")
{
  const Bar default_bar = {};
  nlohmann::json j;

  REQUIRE_THROWS_AS(j.get<Bar>(), std::invalid_argument);

  j["a"] = 42;

  const Bar bar_0 = j;
  REQUIRE(bar_0.a == j["a"]);
  REQUIRE(bar_0.b == default_bar.b);
  REQUIRE(bar_0.c == default_bar.c);

  j["b"] = "Test";
  j["c"] = 100;
  const Bar bar_1 = j;
  REQUIRE(bar_1.a == j["a"]);
  REQUIRE(bar_1.b == j["b"].get<std::string>());
  REQUIRE(bar_1.c == j["c"]);
}

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
  REQUIRE(baz_1.b == j["b"].get<std::string>());
  REQUIRE(baz_1.c == j["c"]);
  REQUIRE(baz_1.d == j["d"]);
  REQUIRE(baz_1.e == j["e"]);
}

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
CCF_JSON_TYPE(
  Foo,
  CCF_JSON_REQUIRED(n_0),
  CCF_JSON_REQUIRED(i_0),
  CCF_JSON_REQUIRED(i64_0),
  CCF_JSON_REQUIRED(s_0),
  CCF_JSON_OPTIONAL(n_1),
  CCF_JSON_OPTIONAL(s_1),
  CCF_JSON_OPTIONAL(opt),
  CCF_JSON_OPTIONAL(vec_s));

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
      CCF_JSON_TYPE(Y, CCF_JSON_REQUIRED(a), CCF_JSON_REQUIRED(b));
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
CCF_JSON_TYPE(Nest0, CCF_JSON_REQUIRED(n));

bool operator==(const Nest0& l, const Nest0& r)
{
  return l.n == r.n;
}

struct Nest1
{
  Nest0 a = {};
  Nest0 b = {};
};
CCF_JSON_TYPE(Nest1, CCF_JSON_REQUIRED(a), CCF_JSON_REQUIRED(b));

bool operator==(const Nest1& l, const Nest1& r)
{
  return l.a == r.a && l.b == r.b;
}

struct Nest2
{
  Nest1 x;
  std::vector<Nest1> xs;
};
CCF_JSON_TYPE(Nest2, CCF_JSON_REQUIRED(x), CCF_JSON_REQUIRED(xs));

bool operator==(const Nest2& l, const Nest2& r)
{
  return l.x == r.x && l.xs == r.xs;
}

struct Nest3
{
  Nest2 v;
};
CCF_JSON_TYPE(Nest3, CCF_JSON_REQUIRED(v));

bool operator==(const Nest3& l, const Nest3& r)
{
  return l.v == r.v;
}

TEST_CASE("nested")
{
  const Nest0 n0_1{10};
  const Nest0 n0_2{20};
  const Nest0 n0_3{30};
  const Nest0 n0_4{40};

  const Nest1 n1_1{n0_1, n0_2};
  const Nest1 n1_2{n0_1, n0_3};
  const Nest1 n1_3{n0_1, n0_4};
  const Nest1 n1_4{n0_2, n0_3};
  const Nest1 n1_5{n0_3, n0_4};
  const Nest1 n1_6{n0_4, n0_4};

  const Nest2 n2_1{n1_1, {n1_6, n1_5, n1_4, n1_3, n1_2}};

  Nest3 n3{n2_1};

  nlohmann::json j = n3;
  const auto r0 = j.get<Nest3>();

  REQUIRE(n3 == r0);

  {
    auto invalid_json = j;
    invalid_json["v"]["xs"][3]["a"].erase("n");
    try
    {
      invalid_json.get<Nest3>();
    }
    catch (ccf::JsonParseError& jpe)
    {
      REQUIRE(jpe.pointer() == "#/v/xs/3/a");
    }

    invalid_json["v"]["xs"][3].erase("a");
    try
    {
      invalid_json.get<Nest3>();
    }
    catch (ccf::JsonParseError& jpe)
    {
      REQUIRE(jpe.pointer() == "#/v/xs/3");
    }

    invalid_json["v"]["xs"][3] = "Broken";
    try
    {
      invalid_json.get<Nest3>();
    }
    catch (ccf::JsonParseError& jpe)
    {
      REQUIRE(jpe.pointer() == "#/v/xs/3");
    }

    invalid_json["v"]["xs"] = "Broken";
    try
    {
      invalid_json.get<Nest3>();
    }
    catch (ccf::JsonParseError& jpe)
    {
      REQUIRE(jpe.pointer() == "#/v/xs");
    }

    invalid_json["v"].erase("xs");
    try
    {
      invalid_json.get<Nest3>();
    }
    catch (ccf::JsonParseError& jpe)
    {
      REQUIRE(jpe.pointer() == "#/v");
    }
  }
}

struct EnumStruct
{
  enum class SampleEnum
  {
    One,
    Two,
    Three,
    Unconverted // Deliberately omitted from conversion
  };

  SampleEnum se;
};

DECLARE_JSON_ENUM(
  EnumStruct::SampleEnum,
  {{EnumStruct::SampleEnum::One, "one"},
   {EnumStruct::SampleEnum::Two, "two"},
   {EnumStruct::SampleEnum::Three, "three"}})
CCF_JSON_TYPE(EnumStruct, CCF_JSON_REQUIRED(se));

TEST_CASE("enum")
{
  {
    INFO("Schema generation");
    EnumStruct es;
    es.se = EnumStruct::SampleEnum::Two;

    nlohmann::json j = es;

    REQUIRE(j["se"] == "two");

    const auto schema = ccf::ds::json::build_schema<EnumStruct>("EnumStruct");

    const nlohmann::json expected{"one", "two", "three"};
    REQUIRE(schema["properties"]["se"]["enum"] == expected);
  }

  {
    INFO("from_json");

    nlohmann::json j;

    // Test good conversions
    j = "one";
    REQUIRE(j.get<EnumStruct::SampleEnum>() == EnumStruct::SampleEnum::One);

    j = "two";
    REQUIRE(j.get<EnumStruct::SampleEnum>() == EnumStruct::SampleEnum::Two);

    j = "three";
    REQUIRE(j.get<EnumStruct::SampleEnum>() == EnumStruct::SampleEnum::Three);

    // Any other value will throw
    j = "One";
    REQUIRE_THROWS(j.get<EnumStruct::SampleEnum>());

    j = "two ";
    REQUIRE_THROWS(j.get<EnumStruct::SampleEnum>());

    j = " three";
    REQUIRE_THROWS(j.get<EnumStruct::SampleEnum>());

    j = "penguin";
    REQUIRE_THROWS(j.get<EnumStruct::SampleEnum>());

    j = 0;
    REQUIRE_THROWS(j.get<EnumStruct::SampleEnum>());

    j = 1;
    REQUIRE_THROWS(j.get<EnumStruct::SampleEnum>());

    j = nlohmann::json::object();
    REQUIRE_THROWS(j.get<EnumStruct::SampleEnum>());

    j = nullptr;
    REQUIRE_THROWS(j.get<EnumStruct::SampleEnum>());
  }

  {
    INFO("to_json");

    nlohmann::json j;

    j = EnumStruct::SampleEnum::One;
    REQUIRE(j.is_string());
    REQUIRE(j.get<std::string>() == "one");

    j = EnumStruct::SampleEnum::Two;
    REQUIRE(j.is_string());
    REQUIRE(j.get<std::string>() == "two");

    j = EnumStruct::SampleEnum::Three;
    REQUIRE(j.is_string());
    REQUIRE(j.get<std::string>() == "three");

    REQUIRE_THROWS(j = EnumStruct::SampleEnum::Unconverted);
  }
}

struct Stringable
{
  std::string s;
  Stringable() = default;
  Stringable(const std::string& s_) : s(s_) {}
  operator std::string() const
  {
    return s;
  }
  bool operator<(const Stringable& other) const
  {
    return s < other.s;
  }
};

TEST_CASE("mappings")
{
  {
    INFO("string-keyed maps");
    std::map<std::string, size_t> m;
    const auto schema = ccf::ds::json::build_schema<decltype(m)>("Map");
    REQUIRE(schema["type"] == "object");

    m["foo"] = 42;
    nlohmann::json j(m);
    REQUIRE(j.is_object());
  }

  {
    INFO("num-keyed maps");
    std::map<size_t, size_t> m;
    const auto schema = ccf::ds::json::build_schema<decltype(m)>("Map");
    REQUIRE(schema["type"] == "array");

    m[5] = 42;
    nlohmann::json j(m);
    REQUIRE(j.is_array());
  }

  {
    INFO("stringable-keyed maps");
    std::map<Stringable, size_t> m;
    const auto schema = ccf::ds::json::build_schema<decltype(m)>("Map");
    REQUIRE(schema["type"] == "object");

    Stringable foo("foo");
    m[foo] = 42;
    nlohmann::json j(m);
    REQUIRE(j.is_object());
  }

  // Surprising! Enums are stringed in JSON, but produce pair-arrays rather than
  // objects. Schema generation correctly documents this.
  {
    INFO("enum-keyed maps");
    std::map<EnumStruct::SampleEnum, size_t> m;
    const auto schema = ccf::ds::json::build_schema<decltype(m)>("Map");
    REQUIRE(schema["type"] == "array");

    m[EnumStruct::SampleEnum::One] = 42;
    nlohmann::json j(m);
    REQUIRE(j.is_array());
  }
}

namespace examples
{
  struct X
  {
    int a, b;
  };
  CCF_JSON_TYPE(X, CCF_JSON_REQUIRED(a), CCF_JSON_REQUIRED(b));

  struct Y
  {
    bool c;
    std::string d;
  };
  CCF_JSON_TYPE(Y, CCF_JSON_REQUIRED(c), CCF_JSON_OPTIONAL(d));

  struct X_A : X
  {
    int m;
  };
  CCF_JSON_TYPE_WITH_BASE(X_A, X, CCF_JSON_REQUIRED(m));

  struct X_B : X
  {
    int n;
  };
  CCF_JSON_TYPE_WITH_BASE(X_B, X, CCF_JSON_OPTIONAL(n));
}

namespace renamed
{
  struct Foo
  {
    size_t x;
    size_t y;
    size_t z;

    size_t a;
    size_t b;
    size_t c;
  };
  // CCF_JSON_TYPE(
  //   Foo,
  //   CCF_JSON_REQUIRED_RENAME(x, "X"),
  //   CCF_JSON_REQUIRED_RENAME(y, "SOMETHING_ELSE"),
  //   CCF_JSON_REQUIRED_RENAME(z, "z-z!?(),;"),
  //   CCF_JSON_OPTIONAL_RENAME(a, "A"),
  //   CCF_JSON_OPTIONAL_RENAME(b, "OTHER_NAME"),
  //   CCF_JSON_OPTIONAL_RENAME(c, "c"));
  inline void to_json(nlohmann::json& j, const Foo& t)
  {
    if constexpr (!std::is_same_v<Foo, Foo>)
    {
      to_json(j, static_cast<const Foo&>(t));
    }
    if (!j.is_object())
    {
      j = nlohmann::json::object();
    }
    Foo t_default;
    {
      if (
        ((::ccf::json::SerdeBehaviour::always_required &
          ::ccf::json::SerdeBehaviour::omit_write_if_default) == 0) ||
        (t.x != t_default.x))
      {
        j["X"] = t.x;
      }
    }
    {
      if (
        ((::ccf::json::SerdeBehaviour::always_required &
          ::ccf::json::SerdeBehaviour::omit_write_if_default) == 0) ||
        (t.y != t_default.y))
      {
        j["SOMETHING_ELSE"] = t.y;
      }
    }
    {
      if (
        ((::ccf::json::SerdeBehaviour::always_required &
          ::ccf::json::SerdeBehaviour::omit_write_if_default) == 0) ||
        (t.z != t_default.z))
      {
        j["z-z!?(),;"] = t.z;
      }
    }
    {
      if (
        ((::ccf::json::SerdeBehaviour::fully_optional &
          ::ccf::json::SerdeBehaviour::omit_write_if_default) == 0) ||
        (t.a != t_default.a))
      {
        j["A"] = t.a;
      }
    }
    {
      if (
        ((::ccf::json::SerdeBehaviour::fully_optional &
          ::ccf::json::SerdeBehaviour::omit_write_if_default) == 0) ||
        (t.b != t_default.b))
      {
        j["OTHER_NAME"] = t.b;
      }
    }
    {
      if (
        ((::ccf::json::SerdeBehaviour::fully_optional &
          ::ccf::json::SerdeBehaviour::omit_write_if_default) == 0) ||
        (t.c != t_default.c))
      {
        j["c"] = t.c;
      }
    }
  }
  inline void from_json(const nlohmann::json& j, Foo& t)
  {
    if (!j.is_object())
    {
      throw ccf::JsonParseError("Expected object, found: " + j.dump());
    }
    if constexpr (!std::is_same_v<Foo, Foo>)
    {
      from_json(j, static_cast<Foo&>(t));
    }
    if (!j.is_object())
    {
      throw ccf::JsonParseError("Expected object, found: " + j.dump());
    }
    {
      const auto it = j.find("X");
      if (it == j.end())
      {
        if constexpr (
          (::ccf::json::SerdeBehaviour::always_required &
           ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
        {
          throw ccf::JsonParseError(
            "Missing required field '"
            "X"
            "' in object: " +
            j.dump());
        }
        else
        {
        }
      }
      else
      {
        try
        {
          t.x = it->get<decltype(Foo::x)>();
        }
        catch (ccf::JsonParseError& jpe)
        {
          jpe.pointer_elements.push_back("X");
          throw;
        }
      }
    }
    {
      const auto it = j.find("SOMETHING_ELSE");
      if (it == j.end())
      {
        if constexpr (
          (::ccf::json::SerdeBehaviour::always_required &
           ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
        {
          throw ccf::JsonParseError(
            "Missing required field '"
            "SOMETHING_ELSE"
            "' in object: " +
            j.dump());
        }
        else
        {
        }
      }
      else
      {
        try
        {
          t.y = it->get<decltype(Foo::y)>();
        }
        catch (ccf::JsonParseError& jpe)
        {
          jpe.pointer_elements.push_back("SOMETHING_ELSE");
          throw;
        }
      }
    }
    {
      const auto it = j.find("z-z!?(),;");
      if (it == j.end())
      {
        if constexpr (
          (::ccf::json::SerdeBehaviour::always_required &
           ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
        {
          throw ccf::JsonParseError(
            "Missing required field '"
            "z-z!?(),;"
            "' in object: " +
            j.dump());
        }
        else
        {
        }
      }
      else
      {
        try
        {
          t.z = it->get<decltype(Foo::z)>();
        }
        catch (ccf::JsonParseError& jpe)
        {
          jpe.pointer_elements.push_back("z-z!?(),;");
          throw;
        }
      }
    }
    {
      const auto it = j.find("A");
      if (it == j.end())
      {
        if constexpr (
          (::ccf::json::SerdeBehaviour::fully_optional &
           ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
        {
          throw ccf::JsonParseError(
            "Missing required field '"
            "A"
            "' in object: " +
            j.dump());
        }
        else
        {
        }
      }
      else
      {
        try
        {
          t.a = it->get<decltype(Foo::a)>();
        }
        catch (ccf::JsonParseError& jpe)
        {
          jpe.pointer_elements.push_back("A");
          throw;
        }
      }
    }
    {
      const auto it = j.find("OTHER_NAME");
      if (it == j.end())
      {
        if constexpr (
          (::ccf::json::SerdeBehaviour::fully_optional &
           ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
        {
          throw ccf::JsonParseError(
            "Missing required field '"
            "OTHER_NAME"
            "' in object: " +
            j.dump());
        }
        else
        {
        }
      }
      else
      {
        try
        {
          t.b = it->get<decltype(Foo::b)>();
        }
        catch (ccf::JsonParseError& jpe)
        {
          jpe.pointer_elements.push_back("OTHER_NAME");
          throw;
        }
      }
    }
    {
      const auto it = j.find("c");
      if (it == j.end())
      {
        if constexpr (
          (::ccf::json::SerdeBehaviour::fully_optional &
           ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
        {
          throw ccf::JsonParseError(
            "Missing required field '"
            "c"
            "' in object: " +
            j.dump());
        }
        else
        {
        }
      }
      else
      {
        try
        {
          t.c = it->get<decltype(Foo::c)>();
        }
        catch (ccf::JsonParseError& jpe)
        {
          jpe.pointer_elements.push_back("c");
          throw;
        }
      }
    }
  }
  inline void fill_json_schema(nlohmann::json& j, const Foo* t)
  {
    if (!j.is_object())
    {
      j = nlohmann::json::object();
    }
    j["type"] = "object";
    if constexpr (!std::is_same_v<Foo, Foo>)
    {
      fill_json_schema(j, static_cast<const Foo*>(t));
    }
    {
      j["properties"]["X"] =
        ::ccf::ds::json::schema_element<decltype(Foo::x)>();
      if constexpr (
        (::ccf::json::SerdeBehaviour::always_required &
         ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
      {
        j["required"].push_back("X");
      }
    }
    {
      j["properties"]["SOMETHING_ELSE"] =
        ::ccf::ds::json::schema_element<decltype(Foo::y)>();
      if constexpr (
        (::ccf::json::SerdeBehaviour::always_required &
         ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
      {
        j["required"].push_back("SOMETHING_ELSE");
      }
    }
    {
      j["properties"]["z-z!?(),;"] =
        ::ccf::ds::json::schema_element<decltype(Foo::z)>();
      if constexpr (
        (::ccf::json::SerdeBehaviour::always_required &
         ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
      {
        j["required"].push_back("z-z!?(),;");
      }
    }
    {
      j["properties"]["A"] =
        ::ccf::ds::json::schema_element<decltype(Foo::a)>();
      if constexpr (
        (::ccf::json::SerdeBehaviour::fully_optional &
         ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
      {
        j["required"].push_back("A");
      }
    }
    {
      j["properties"]["OTHER_NAME"] =
        ::ccf::ds::json::schema_element<decltype(Foo::b)>();
      if constexpr (
        (::ccf::json::SerdeBehaviour::fully_optional &
         ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
      {
        j["required"].push_back("OTHER_NAME");
      }
    }
    {
      j["properties"]["c"] =
        ::ccf::ds::json::schema_element<decltype(Foo::c)>();
      if constexpr (
        (::ccf::json::SerdeBehaviour::fully_optional &
         ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
      {
        j["required"].push_back("c");
      }
    }
  }
  inline std::string schema_name(const Foo*)
  {
    return "Foo";
  }
  inline void add_schema_components(
    ccf::ds::openapi::SchemaHelper& doc, nlohmann::json& j, const Foo* t)
  {
    if (!j.is_object())
    {
      j = nlohmann::json::object();
    }
    j["type"] = "object";
    if constexpr (!std::is_same_v<Foo, Foo>)
    {
      add_schema_components(doc, j, static_cast<const Foo*>(t));
    }
    {
      j["properties"]["X"] =
        ::ccf::ds::json::schema_element<decltype(Foo::x)>();
      if constexpr (
        (::ccf::json::SerdeBehaviour::always_required &
         ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
      {
        j["required"].push_back("X");
      }
    }
    {
      j["properties"]["SOMETHING_ELSE"] =
        ::ccf::ds::json::schema_element<decltype(Foo::y)>();
      if constexpr (
        (::ccf::json::SerdeBehaviour::always_required &
         ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
      {
        j["required"].push_back("SOMETHING_ELSE");
      }
    }
    {
      j["properties"]["z-z!?(),;"] =
        ::ccf::ds::json::schema_element<decltype(Foo::z)>();
      if constexpr (
        (::ccf::json::SerdeBehaviour::always_required &
         ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
      {
        j["required"].push_back("z-z!?(),;");
      }
    }
    {
      j["properties"]["A"] =
        ::ccf::ds::json::schema_element<decltype(Foo::a)>();
      if constexpr (
        (::ccf::json::SerdeBehaviour::fully_optional &
         ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
      {
        j["required"].push_back("A");
      }
    }
    {
      j["properties"]["OTHER_NAME"] =
        ::ccf::ds::json::schema_element<decltype(Foo::b)>();
      if constexpr (
        (::ccf::json::SerdeBehaviour::fully_optional &
         ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
      {
        j["required"].push_back("OTHER_NAME");
      }
    }
    {
      j["properties"]["c"] =
        ::ccf::ds::json::schema_element<decltype(Foo::c)>();
      if constexpr (
        (::ccf::json::SerdeBehaviour::fully_optional &
         ::ccf::json::SerdeBehaviour::allow_read_if_missing) == 0)
      {
        j["required"].push_back("c");
      }
    }
  };
}

TEST_CASE("JSON with different field names")
{
  const auto schema = ccf::ds::json::build_schema<renamed::Foo>("renamed::Foo");

  const auto& properties = schema["properties"];
  const auto& required = schema["required"];

  std::vector<char const*> required_json_fields{
    "X", "SOMETHING_ELSE", "z-z!?(),;"};
  for (const auto s : required_json_fields)
  {
    REQUIRE(properties.find(s) != properties.end());
    REQUIRE(std::find(required.begin(), required.end(), s) != required.end());
  }

  std::vector<char const*> optional_json_fields{"A", "OTHER_NAME", "c"};
  for (const auto s : optional_json_fields)
  {
    REQUIRE(properties.find(s) != properties.end());
    REQUIRE(std::find(required.begin(), required.end(), s) == required.end());
  }

  renamed::Foo foo;
  foo.x = 1;
  foo.y = 2;
  foo.z = 3;
  foo.a = 4;
  foo.b = 5;
  foo.c = 6;

  const nlohmann::json j = foo;
  REQUIRE(j["X"] == foo.x);
  REQUIRE(j["SOMETHING_ELSE"] == foo.y);
  REQUIRE(j["z-z!?(),;"] == foo.z);
  REQUIRE(j["A"] == foo.a);
  REQUIRE(j["OTHER_NAME"] == foo.b);
  REQUIRE(j["c"] == foo.c);

  const auto foo2 = j.get<renamed::Foo>();
  REQUIRE(foo2.x == foo.x);
  REQUIRE(foo2.y == foo.y);
  REQUIRE(foo2.z == foo.z);
  REQUIRE(foo2.a == foo.a);
  REQUIRE(foo2.b == foo.b);
  REQUIRE(foo2.c == foo.c);
}

TEST_CASE("example validation")
{
  using namespace examples;

  // struct X
  {
    // Valid JSON
    REQUIRE_NOTHROW("{ \"a\": 42, \"b\": 100 }"_json.get<X>());
    REQUIRE_NOTHROW(
      "{ \"a\": 42, \"b\": 100, \"Unused\": [\"Anything\"] }"_json.get<X>());

    // Invalid JSON
    REQUIRE_THROWS("{}"_json.get<X>());
    REQUIRE_THROWS("{ \"a\": 42 }"_json.get<X>());
    REQUIRE_THROWS("{ \"a\": 42, \"b\": \"Hello world\" }"_json.get<X>());
  }

  // struct Y
  {
    // Valid JSON
    REQUIRE_NOTHROW("{ \"c\": true }"_json.get<Y>());
    REQUIRE_NOTHROW("{ \"c\": false, \"d\": \"Hello\" }"_json.get<Y>());

    // Invalid JSON
    REQUIRE_THROWS("{ \"d\": \"Hello\" }"_json.get<Y>());
  }

  // struct X_A
  {
    // Valid JSON
    REQUIRE_NOTHROW("{ \"a\": 42, \"b\": 100, \"m\": 101 }"_json.get<X_A>());

    // Invalid JSON
    REQUIRE_THROWS("{ \"a\": 42, \"b\": 100 }"_json.get<X_A>());
    REQUIRE_THROWS("{ \"m\": 101 }"_json.get<X_A>());
  }

  // struct X_B
  {
    // Valid JSON
    REQUIRE_NOTHROW("{ \"a\": 42, \"b\": 100 }"_json.get<X_B>());
    REQUIRE_NOTHROW("{ \"a\": 42, \"b\": 100, \"n\": 101 }"_json.get<X_B>());

    // Invalid JSON
    REQUIRE_THROWS("{ \"n\": 101 }"_json.get<X_B>());
  }
}
