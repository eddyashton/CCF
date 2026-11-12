// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "ccf/json.h"
#include "ccf/kv/serialisers/json_serialiser.h"
#include "ccf/tx_id.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

namespace custom_json
{
  struct Specialized
  {
    int value = 3;
  };
}

template <>
struct nlohmann::adl_serializer<custom_json::Specialized>
{
  static void to_json(nlohmann::json& j, const custom_json::Specialized& value)
  {
    j = std::to_string(value.value);
  }

  static void from_json(
    const nlohmann::json& j, custom_json::Specialized& value)
  {
    value.value = std::stoi(j.get<std::string>());
  }
};

namespace
{
  using Json = nlohmann::json;

  struct Base
  {
    int id = 7;
    int retries = 7;
  };

  DECLARE_JSON_TYPE_WITH_OPTIONAL_FIELDS(Base);
  DECLARE_JSON_REQUIRED_FIELDS(Base, id);
  DECLARE_JSON_OPTIONAL_FIELDS(Base, retries);

  struct Record : Base
  {
    std::optional<ccf::TxID> transaction_id;
    std::vector<uint8_t> bytes;
    std::map<std::string, std::string> labels;
    std::optional<std::string> next_link;
  };

  DECLARE_JSON_TYPE_WITH_BASE_AND_OPTIONAL_FIELDS(Record, Base);
  DECLARE_JSON_REQUIRED_FIELDS(Record, transaction_id, bytes, labels);
  DECLARE_JSON_OPTIONAL_FIELDS_WITH_RENAMES(Record, next_link, "@nextLink");

  struct DenseRecord : Base
  {
    std::optional<ccf::TxID> transaction_id;
    std::vector<uint8_t> bytes;
    std::map<std::string, std::string> labels;
    std::optional<std::string> next_link;
  };

  CCF_JSON_TYPE(
    DenseRecord,
    CCF_JSON_BASE(Base),
    CCF_JSON_REQUIRED(transaction_id, bytes, labels),
    CCF_JSON_NAMED(Optional, next_link, "@nextLink"));

  struct NewRecord
  {
    int id = 7;
    std::string message;
    int priority = 7;
  };

  CCF_JSON_TYPE(
    NewRecord,
    CCF_JSON_REQUIRED(id, message),
    CCF_JSON_ALLOW_MISSING(priority));

  enum class Mode
  {
    Active,
    Retired,
    Unmapped
  };

  CCF_JSON_ENUM(Mode, "active", Mode::Active, "retired", Mode::Retired);

  enum class CustomMode
  {
    Active
  };

  void to_json(Json& j, CustomMode)
  {
    j = "custom-active";
  }

  void from_json(const Json& j, CustomMode& value)
  {
    if (j != "custom-active")
    {
      throw ccf::JsonParseError("Unknown custom mode");
    }
    value = CustomMode::Active;
  }

  struct CustomLeaves
  {
    CustomMode mode = CustomMode::Active;
    custom_json::Specialized specialized;
  };

  CCF_JSON_TYPE(CustomLeaves, CCF_JSON_REQUIRED(mode, specialized));

  struct AbstractLeaf
  {
    virtual ~AbstractLeaf() = default;
    virtual int get() const = 0;
  };

  struct ConcreteLeaf : AbstractLeaf
  {
    int get() const override
    {
      return 7;
    }
  };

  using LeafPtr = std::shared_ptr<AbstractLeaf>;

  void to_json(Json& j, const LeafPtr& value)
  {
    j = value ? Json(value->get()) : Json(nullptr);
  }

  void from_json(const Json& j, LeafPtr& value)
  {
    if (j.is_null())
    {
      value.reset();
    }
    else if (j == 7)
    {
      value = std::make_shared<ConcreteLeaf>();
    }
    else
    {
      throw ccf::JsonParseError("Unknown leaf");
    }
  }

  struct TextLeaf
  {
    std::string value;
  };

  void to_json(Json& j, const TextLeaf& value)
  {
    j = value.value;
  }

  struct TextRecord
  {
    std::string text;
    TextLeaf custom;
    Json dom;
  };

  CCF_JSON_TYPE(TextRecord, CCF_JSON_REQUIRED(text, custom, dom));

  struct PrimitiveLeaves
  {
    char character = 'x';
    std::pair<std::string, int> pair{"key", 7};
    std::vector<std::pair<int, int>> pairs{{1, 7}, {2, 8}};
    std::array<char, 2> characters{'x', '\0'};
    std::array<char, 0> empty;
  };

  CCF_JSON_TYPE(
    PrimitiveLeaves,
    CCF_JSON_REQUIRED(character, pair, pairs, characters, empty));

  struct WithMapDefaults
  {
    std::map<std::string, int> labels{{"default", 7}};
    std::map<std::string, std::map<std::string, int>> nested{
      {"outer", {{"stale", 8}}}};
  };

  CCF_JSON_TYPE(WithMapDefaults, CCF_JSON_ALLOW_MISSING(labels, nested));

  struct StatefulDefaults
  {
    static inline int next = 0;
    int a;
    int b;

    StatefulDefaults() : a(++next), b(a) {}
  };

  CCF_JSON_TYPE(StatefulDefaults, CCF_JSON_OPTIONAL(a, b));

  struct WithMode
  {
    Mode mode = Mode::Active;
  };

  CCF_JSON_TYPE(WithMode, CCF_JSON_REQUIRED(mode));

  struct NativeOnly
  {
    int id = 7;
  };

  CCF_JSON_FIELDS(NativeOnly, CCF_JSON_REQUIRED(id));

  void to_json(Json&, const NativeOnly&)
  {
    throw std::logic_error("DOM conversion must not be used");
  }

  void from_json(const Json&, NativeOnly&)
  {
    throw std::logic_error("DOM conversion must not be used");
  }

  struct Defaulted
  {
    static inline size_t constructions = 0;
    int a = 7;
    int b = 8;
    std::vector<int> values;

    Defaulted() : values(128, 1)
    {
      ++constructions;
    }
  };

  DECLARE_JSON_TYPE_WITH_OPTIONAL_FIELDS(Defaulted);
  DECLARE_JSON_REQUIRED_FIELDS(Defaulted);
  DECLARE_JSON_OPTIONAL_FIELDS(Defaulted, a, b, values);

  struct Flags
  {
    uint32_t mode : 2;
    uint32_t count : 6;
  };

  DECLARE_JSON_TYPE(Flags);
  DECLARE_JSON_REQUIRED_FIELDS(Flags, mode, count);

  struct CustomBase
  {
    int value = 3;
  };

  void to_json(Json& j, const CustomBase& value)
  {
    j["wire_name"] = value.value;
  }

  void from_json(const Json& j, CustomBase& value)
  {
    value.value = j.at("wire_name").get<int>();
  }

  std::string schema_name([[maybe_unused]] const CustomBase* tag)
  {
    return "CustomBase";
  }

  void fill_json_schema(Json& schema, [[maybe_unused]] const CustomBase* tag)
  {
    schema = {
      {"type", "object"},
      {"properties", {{"wire_name", {{"type", "integer"}}}}},
      {"required", Json::array({"wire_name"})}};
  }

  struct CustomDerived : CustomBase
  {
    bool enabled = false;
  };

  DECLARE_JSON_TYPE_WITH_BASE(CustomDerived, CustomBase);
  DECLARE_JSON_REQUIRED_FIELDS(CustomDerived, enabled);
}

TEST_CASE("JSON macro frontends share direct IO and DOM contracts")
{
  for (
    const auto& input :
    {R"({"id":8,"transaction_id":null,"bytes":"AQID","labels":{}})",
     R"({"id":9,"retries":8,"transaction_id":"2.3","bytes":"","labels":{"a":"b"},"@nextLink":"/next"})"})
  {
    const auto json = Json::parse(input);
    const auto direct = ccf::json::read<Record>(input);
    const auto dom = json.get<Record>();
    CHECK(Json::parse(ccf::json::write(direct)) == json);
    CHECK(Json(direct) == Json(dom));
  }
  const auto added =
    ccf::json::read<NewRecord>(R"({"id":8,"message":"hello","future":true})");
  CHECK(added.priority == 7);
  CHECK(
    Json::parse(ccf::json::write(added)) ==
    Json{{"id", 8}, {"message", "hello"}, {"priority", 7}});
}

TEST_CASE("Direct JSON IO enforces field policies and buffer boundaries")
{
  for (const auto* input :
       {R"({"message":"hello"})",
        R"({"id":8,"message":null})",
        R"({"id":8,"message":"hello"}{})",
        R"({"id":8,"message":"hello"} false)"})
  {
    CHECK_THROWS_AS(ccf::json::read<NewRecord>(input), ccf::JsonParseError);
  }
  const std::string storage = "1234";
  CHECK(ccf::json::read<int>(std::string_view(storage).substr(0, 2)) == 12);
  const std::array<char, 2> exact{'1', '2'};
  CHECK(
    ccf::json::read<int>(std::string_view(exact.data(), exact.size())) == 12);
  const std::string surrounded = R"(xx{"id":8,"message":"hello"}false)";
  const std::string_view bounded(surrounded.data() + 2, surrounded.size() - 7);
  CHECK(ccf::json::read<NewRecord>(bounded).id == 8);
  CHECK_THROWS_AS(
    ccf::json::read<NewRecord>(bounded.substr(0, bounded.size() - 1)),
    ccf::JsonParseError);
  CHECK_THROWS_AS(
    ccf::json::read<Record>(
      R"({"id":8,"transaction_id":null,"bytes":"","labels":{})"),
    ccf::JsonParseError);
  CHECK(
    ccf::json::read<NewRecord>(R"({"id":8,"message":"hello"} )"
                               "\t\r\n")
      .id == 8);
  std::string reused = "previous output";
  ccf::json::write(NewRecord{8, "hello", 9}, reused);
  CHECK(Json::parse(reused).at("priority") == 9);
}

TEST_CASE("Dense inherited field policies preserve defaults and updates")
{
  const auto input =
    R"({"id":8,"transaction_id":null,"bytes":[1,2,3],"labels":{}})";
  auto dense = ccf::json::read<DenseRecord>(input);
  const auto legacy = ccf::json::read<Record>(input);
  CHECK(Json(dense) == Json(legacy));
  CHECK(Json::parse(ccf::json::write(dense)) == Json(legacy));
  CHECK(dense.retries == 7);
  CHECK_FALSE(Json(dense).contains("@nextLink"));
  CHECK(Json(dense).at("transaction_id").is_null());
  dense.retries = 9;
  dense.next_link = "/next";
  ccf::json::read(dense, input);
  CHECK(dense.retries == 9);
  CHECK(dense.next_link == "/next");
  CHECK(Json::parse(ccf::json::write(dense)).at("retries") == 9);
  CHECK(Json::parse(ccf::json::write(dense)).at("@nextLink") == "/next");
  CHECK_THROWS_AS(
    ccf::json::read<DenseRecord>(R"({"id":8,"bytes":"","labels":{}})"),
    ccf::JsonParseError);
}

TEST_CASE("DOM conversions preserve updates and persisted KV encoding")
{
  NewRecord value{8, "hello", 9};
  Json output = {{"keep", true}};
  to_json(output, value);
  CHECK(output.at("keep") == true);
  Json{{"id", 9}, {"message", "updated"}}.get_to(value);
  CHECK(value.priority == 9);
  CHECK(value.message == "updated");
  const auto bytes =
    ccf::kv::serialisers::JsonSerialiser<NewRecord>::to_serialised(value);
  CHECK(std::string(bytes.begin(), bytes.end()) == Json(value).dump());
}

TEST_CASE("Macro schema generation preserves required and nullable fields")
{
  const auto schema = ccf::json::schema<Record>();
  const auto& required = schema.at("required");
  CHECK(
    std::find(required.begin(), required.end(), "transaction_id") !=
    required.end());
  CHECK(schema.at("properties").contains("@nextLink"));
  CHECK(schema.at("properties").contains("bytes"));
  CHECK_FALSE(schema.at("properties").contains("next_link"));
}

TEST_CASE("DOM serialization constructs optional defaults once per owner")
{
  const Defaulted value{};
  Defaulted::constructions = 0;
  CHECK(Json(value) == Json::object());
  CHECK(Defaulted::constructions == 1);
  Defaulted::constructions = 0;
  CHECK(ccf::json::write(value) == "{}");
  CHECK(Defaulted::constructions == 1);
  StatefulDefaults::next = 0;
  const StatefulDefaults stateful{};
  StatefulDefaults::next = 0;
  const auto dom = Json(stateful);
  StatefulDefaults::next = 0;
  CHECK(Json::parse(ccf::json::write(stateful)) == dom);
}

TEST_CASE("Present maps replace defaults while absent fields retain values")
{
  auto value =
    ccf::json::read<WithMapDefaults>(R"({"labels":{},"nested":{"outer":{}}})");
  CHECK(value.labels.empty());
  CHECK(value.nested.at("outer").empty());
  value.labels["keep"] = 9;
  ccf::json::read(value, "{}");
  CHECK(value.labels.at("keep") == 9);
  ccf::json::read(value, R"({"labels":{}})");
  CHECK(value.labels.empty());
  std::map<std::string, int> reused{{"stale", 1}};
  ccf::json::read(reused, "{}");
  CHECK(reused.empty());
}

TEST_CASE("Ignored values remain validated JSON with bounded depth")
{
  for (const auto* invalid : {"[garbage]", R"({"x":})", "tru", R"("\q")"})
  {
    const auto input =
      std::string(R"({"id":8,"message":"ok","unknown":)") + invalid + "}";
    CHECK_THROWS_AS(ccf::json::read<NewRecord>(input), ccf::JsonParseError);
  }
  const auto nested = [](size_t depth) {
    return std::string(R"({"id":8,"message":"ok","unknown":)") +
      std::string(depth, '[') + "0" + std::string(depth, ']') + "}";
  };
  CHECK(ccf::json::read<NewRecord>(nested(16)).id == 8);
  CHECK_THROWS_AS(
    ccf::json::read<NewRecord>(nested(glz::max_recursive_depth_limit + 1)),
    ccf::JsonParseError);
}

TEST_CASE("DOM-backed read failures use the public parse exception")
{
  CHECK_THROWS_AS(ccf::json::read<Json>(""), ccf::JsonParseError);
  CHECK_THROWS_AS(ccf::json::read<Json>(R"({"x":})"), ccf::JsonParseError);
  CHECK_THROWS_AS(
    ccf::json::read<Json>(R"({"x":1}false)"), ccf::JsonParseError);
  CHECK(ccf::json::read<Json>(" \t{\"x\":1} \n") == Json{{"x", 1}});
  CHECK_THROWS_AS(ccf::json::read<ccf::TxID>("{}"), ccf::JsonParseError);
  CHECK_THROWS_AS(
    (ccf::json::read<std::pair<int, int>>(R"(["wrong",2])")),
    ccf::JsonParseError);
  CHECK_THROWS_AS(
    ccf::json::read<CustomLeaves>(
      R"({"mode":"custom-active","specialized":{}})"),
    ccf::JsonParseError);
}

TEST_CASE("Legacy custom shapes remain valid on the direct API")
{
  const Flags flags{2, 17};
  const auto decoded = ccf::json::read<Flags>(ccf::json::write(flags));
  CHECK(decoded.mode == 2);
  CHECK(decoded.count == 17);
  const CustomDerived custom{};
  const auto bytes = ccf::json::write(custom);
  CHECK(Json::parse(bytes) == Json{{"wire_name", 3}, {"enabled", false}});
  CHECK(ccf::json::read<CustomDerived>(bytes).value == 3);
  using NumericMap = std::map<int, std::string>;
  const NumericMap numeric_keys{{7, "seven"}};
  const auto map_bytes = ccf::json::write(numeric_keys);
  CHECK(Json::parse(map_bytes) == Json(numeric_keys));
  CHECK(ccf::json::read<NumericMap>(map_bytes) == numeric_keys);
  using UnorderedNumericMap = std::unordered_map<int, std::string>;
  const UnorderedNumericMap unordered_keys{{7, "seven"}};
  CHECK(
    ccf::json::read<UnorderedNumericMap>(ccf::json::write(unordered_keys)) ==
    unordered_keys);
  CHECK_THROWS(ccf::json::read<std::vector<uint8_t>>(R"("not base64!")"));
}

TEST_CASE("Declared enum writers reject unmapped values at every nesting level")
{
  CHECK(ccf::json::write(Mode::Active) == R"("active")");
  CHECK(ccf::json::read<Mode>(R"("retired")") == Mode::Retired);
  CHECK_THROWS_AS(ccf::json::read<Mode>(R"("unknown")"), ccf::JsonParseError);
  CHECK_THROWS_AS(ccf::json::read<Mode>("0"), ccf::JsonParseError);
  for (const auto invalid : {Mode::Unmapped, static_cast<Mode>(99)})
  {
    CHECK_THROWS(Json(invalid));
    CHECK_THROWS(ccf::json::write(invalid));
    CHECK_THROWS(Json(WithMode{invalid}));
    CHECK_THROWS(ccf::json::write(WithMode{invalid}));
    CHECK_THROWS(ccf::json::write(std::vector<Mode>{Mode::Active, invalid}));
    CHECK_THROWS(
      ccf::json::write(std::map<std::string, Mode>{{"mode", invalid}}));
  }
}

TEST_CASE("Direct IO does not invoke DOM conversion hooks")
{
  CHECK_THROWS(Json(NativeOnly{}));
  CHECK_THROWS(Json{{"id", 8}}.get<NativeOnly>());
  CHECK(ccf::json::write(NativeOnly{}) == R"({"id":7})");
  CHECK(ccf::json::read<NativeOnly>(R"({"id":8})").id == 8);
}

TEST_CASE("Direct IO preserves custom enum and specialized serializer leaves")
{
  const CustomLeaves value{};
  const auto expected = Json(value);
  CHECK(Json::parse(ccf::json::write(value)) == expected);
  const auto parsed = ccf::json::read<CustomLeaves>(expected.dump());
  CHECK(parsed.mode == CustomMode::Active);
  CHECK(parsed.specialized.value == 3);
  CHECK(
    ccf::json::write(CustomMode::Active) == Json(CustomMode::Active).dump());
  CHECK(
    ccf::json::write(custom_json::Specialized{}) ==
    Json(custom_json::Specialized{}).dump());
}

TEST_CASE("Custom nullable ADL leaves retain their wire contract")
{
  const LeafPtr value = std::make_shared<ConcreteLeaf>();
  CHECK(ccf::json::write(value) == "7");
  CHECK(ccf::json::read<LeafPtr>("7")->get() == 7);
  CHECK(ccf::json::write(LeafPtr{}) == "null");
  CHECK_FALSE(ccf::json::read<LeafPtr>("null"));
  const std::vector<LeafPtr> nested{value, nullptr};
  CHECK(Json::parse(ccf::json::write(nested)) == Json::array({7, nullptr}));
}

TEST_CASE("Response writing opts into legacy UTF8 replacement at every leaf")
{
  for (const auto& text :
       {std::string("\xff"),
        std::string("\xc3"),
        std::string("\xc3("),
        std::string("\xed\xa0\x80"),
        std::string("\xf0\x9f\x98\x80")})
  {
    const TextRecord value{text, TextLeaf{text}, Json{{text, text}}};
    const auto expected =
      Json(value).dump(-1, ' ', false, Json::error_handler_t::replace);
    CHECK(
      Json::parse(ccf::json::write_for_response(value)) ==
      Json::parse(expected));
    CHECK(
      Json::parse(ccf::json::write_for_response(text)) ==
      Json::parse(
        Json(text).dump(-1, ' ', false, Json::error_handler_t::replace)));
  }
  CHECK_THROWS(ccf::json::write(TextLeaf{std::string("\xff")}));
  CHECK_THROWS_AS(
    ccf::json::read<std::string>(std::string("\"\xff\"")), ccf::JsonParseError);
}

TEST_CASE("Direct IO preserves character and pair array wire formats")
{
  const char character = 'x';
  CHECK(ccf::json::write(character) == Json(character).dump());
  CHECK(ccf::json::read<char>(Json(character).dump()) == character);
  const std::pair<std::string, int> pair{"key", 7};
  CHECK(Json::parse(ccf::json::write(pair)) == Json(pair));
  CHECK(
    ccf::json::read<std::pair<std::string, int>>(Json(pair).dump()) == pair);
  const std::vector<std::pair<int, int>> pairs{{1, 7}, {2, 8}};
  CHECK(Json::parse(ccf::json::write(pairs)) == Json(pairs));
  CHECK(
    ccf::json::read<std::vector<std::pair<int, int>>>(Json(pairs).dump()) ==
    pairs);
  const PrimitiveLeaves nested{};
  CHECK(Json::parse(ccf::json::write(nested)) == Json(nested));
  const auto decoded = ccf::json::read<PrimitiveLeaves>(Json(nested).dump());
  CHECK(Json(decoded) == Json(nested));
  const std::array<char, 0> empty{};
  CHECK(ccf::json::write(empty) == "[]");
  CHECK(ccf::json::read<std::array<char, 0>>("[]") == empty);
}

TEST_CASE("Direct writers escape all JSON control characters")
{
  std::string controls;
  for (char c = 0; c < ' '; ++c)
  {
    controls.push_back(c);
  }
  for (const auto& text : {controls, std::string(128, 'x') + controls})
  {
    CHECK(Json::parse(ccf::json::write(text)) == text);
    const std::map<std::string, std::string> object{{text, text}};
    CHECK(Json::parse(ccf::json::write(object)) == Json(object));
  }
}
