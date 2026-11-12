// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "ccf/json_handler.h"

#include "ccf/rpc_exception.h"
#include "node/rpc/http_rpc_context.h"

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

namespace response_test
{
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
}

namespace nlohmann
{
  template <>
  struct adl_serializer<std::shared_ptr<response_test::AbstractLeaf>>
  {
    static void to_json(
      json& j, const std::shared_ptr<response_test::AbstractLeaf>& value)
    {
      if (value)
      {
        j = {{"specialized_wire", value->get()}};
      }
      else
      {
        j = nullptr;
      }
    }
  };
}

namespace
{
  struct Response
  {
    std::string message;
    bool enabled = false;
  };

  DECLARE_JSON_TYPE_WITH_OPTIONAL_FIELDS(Response);
  DECLARE_JSON_REQUIRED_FIELDS(Response, message);
  DECLARE_JSON_OPTIONAL_FIELDS(Response, enabled);

  struct DirectOnly
  {
    int value = 7;
  };

  CCF_JSON_FIELDS(DirectOnly, CCF_JSON_REQUIRED(value));

  void to_json(nlohmann::json&, const DirectOnly&)
  {
    throw std::logic_error("DOM conversion must not be used");
  }

  struct OutputOnly
  {
    int value = 7;
  };

  void to_json(nlohmann::json& j, const OutputOnly& value)
  {
    j = {{"wire_name", value.value}};
  }

  struct WithCustomOutput
  {
    OutputOnly leaf;
  };

  CCF_JSON_TYPE(WithCustomOutput, CCF_JSON_REQUIRED(leaf));

  struct CustomTextOutput
  {
    std::string text;
  };

  void to_json(nlohmann::json& j, const CustomTextOutput& value)
  {
    j = {{"wire_name", value.text}};
  }

  struct WithCustomTextOutput
  {
    CustomTextOutput leaf;
    nlohmann::json dom;
  };

  CCF_JSON_TYPE(WithCustomTextOutput, CCF_JSON_REQUIRED(leaf, dom));

  struct WithSpecializedPointer
  {
    std::shared_ptr<response_test::AbstractLeaf> leaf;
  };

  CCF_JSON_TYPE(WithSpecializedPointer, CCF_JSON_REQUIRED(leaf));

  enum class CustomEnum
  {
    Value
  };

  void to_json(nlohmann::json& j, CustomEnum)
  {
    j = "custom_enum";
  }

  auto make_context(std::optional<std::string> accept = std::nullopt)
  {
    ccf::http::HeaderMap headers;
    if (accept.has_value())
    {
      headers[ccf::http::headers::ACCEPT] = accept.value();
    }
    return std::make_shared<http::HttpRpcContext>(
      nullptr,
      ccf::HttpVersion::HTTP1,
      HTTP_GET,
      "/",
      std::move(headers),
      std::vector<uint8_t>{});
  }
}

TEST_CASE("Typed JSON responses preserve DOM response semantics")
{
  for (const auto& accept :
       {std::string("application/json"),
        std::string("*/*"),
        std::string("application/*"),
        std::string("text/plain;q=1, application/json;q=0.1")})
  {
    const auto context = make_context(accept);
    std::shared_ptr<ccf::RpcContext> base = context;
    auto response = ccf::make_success(Response{"hello", false});
    REQUIRE(std::holds_alternative<ccf::jsonhandler::SerialisedJson>(response));
    ccf::jsonhandler::set_response(std::move(response), base);
    CHECK(context->get_response_status() == HTTP_STATUS_OK);
    CHECK(
      context->get_response_headers().at(ccf::http::headers::CONTENT_TYPE) ==
      ccf::http::headervalues::contenttype::JSON);
    CHECK(
      ccf::parse_json_safe(context->get_response_body()) ==
      nlohmann::json{{"message", "hello"}});
  }
}

TEST_CASE("DOM and typed responses retain Accept rejection and null status")
{
  for (auto response :
       {ccf::make_success(Response{"hello", false}),
        ccf::make_success(nlohmann::json{{"message", "hello"}})})
  {
    const auto context = make_context("text/plain");
    std::shared_ptr<ccf::RpcContext> base = context;
    try
    {
      ccf::jsonhandler::set_response(std::move(response), base);
      FAIL("Unsupported content type was accepted");
    }
    catch (const ccf::RpcException& error)
    {
      CHECK(error.error.status == HTTP_STATUS_NOT_ACCEPTABLE);
      CHECK(error.error.code == ccf::errors::UnsupportedContentType);
    }
  }
  for (auto response :
       {ccf::make_success(),
        ccf::make_success(nullptr),
        ccf::make_success(nlohmann::json(nullptr)),
        ccf::make_success(std::optional<int>{})})
  {
    const auto context = make_context("text/plain");
    std::shared_ptr<ccf::RpcContext> base = context;
    ccf::jsonhandler::set_response(std::move(response), base);
    CHECK(context->get_response_status() == HTTP_STATUS_NO_CONTENT);
    CHECK(context->get_response_body().empty());
    CHECK_FALSE(context->get_response_headers().contains(
      ccf::http::headers::CONTENT_TYPE));
  }
}

TEST_CASE("make_success selects typed values without converting them to a DOM")
{
  using ccf::jsonhandler::SerialisedJson;
  Response value{"hello", false};
  const Response const_value = value;
  for (const auto& response :
       {ccf::make_success(value),
        ccf::make_success(const_value),
        ccf::make_success(Response{"hello", false}),
        ccf::make_success<Response>(value),
        ccf::make_success<Response>({"hello", false})})
  {
    REQUIRE(std::holds_alternative<SerialisedJson>(response));
    CHECK(
      nlohmann::json::parse(std::get<SerialisedJson>(response).body) ==
      nlohmann::json(value));
  }
  CHECK_THROWS(nlohmann::json(DirectOnly{}));
  const auto direct = ccf::make_success(DirectOnly{});
  REQUIRE(std::holds_alternative<SerialisedJson>(direct));
  CHECK(std::get<SerialisedJson>(direct).body == R"({"value":7})");
}

TEST_CASE("make_success retains DOM overloads and scalar/custom wire formats")
{
  using Json = nlohmann::json;
  using namespace std::string_literals;
  Json value = {{"message", "hello"}};
  const Json const_value = value;
  for (const auto& response :
       {ccf::make_success(value),
        ccf::make_success(const_value),
        ccf::make_success(Json(value)),
        ccf::make_success({{"message", "hello"}})})
  {
    REQUIRE(std::holds_alternative<Json>(response));
    CHECK(std::get<Json>(response) == value);
  }
  const std::string text = "line\n\"quoted\"";
  const auto controls = "prefix\0\x01\x1f\"\\\nsuffix"s;
  for (auto [response, expected] :
       std::vector<std::pair<ccf::jsonhandler::JsonAdapterResponse, Json>>{
         {ccf::make_success(text), Json(text)},
         {ccf::make_success(controls), Json(controls)},
         {ccf::make_success(Response{controls, false}),
          Json{{"message", controls}}},
         {ccf::make_success("hello"), Json("hello")},
         {ccf::make_success(true), Json(true)},
         {ccf::make_success(42), Json(42)},
         {ccf::make_success('A'), Json('A')},
         {ccf::make_success(std::array<char, 2>{'A', 'B'}),
          Json(std::array<char, 2>{'A', 'B'})},
         {ccf::make_success(std::array<char, 0>{}),
          Json(std::array<char, 0>{})},
         {ccf::make_success(std::pair<std::string, int>{"key", 7}),
          Json(std::pair<std::string, int>{"key", 7})},
         {ccf::make_success(std::vector<std::pair<int, int>>{{1, 7}, {2, 8}}),
          Json(std::vector<std::pair<int, int>>{{1, 7}, {2, 8}})},
         {ccf::make_success(OutputOnly{}), Json{{"wire_name", 7}}},
         {ccf::make_success(CustomEnum::Value), Json("custom_enum")},
         {ccf::make_success(std::vector<uint8_t>{0, 1, 255}),
          Json(std::vector<uint8_t>{0, 1, 255})},
         {ccf::make_success(std::map<int, std::string>{{7, "seven"}}),
          Json(std::map<int, std::string>{{7, "seven"}})},
         {ccf::make_success(WithCustomOutput{}),
          Json{{"leaf", {{"wire_name", 7}}}}},
         {ccf::make_success(std::vector<OutputOnly>{{9}, {10}}),
          Json::array({Json{{"wire_name", 9}}, Json{{"wire_name", 10}}})}})
  {
    const auto context = make_context();
    std::shared_ptr<ccf::RpcContext> base = context;
    REQUIRE(std::holds_alternative<ccf::jsonhandler::SerialisedJson>(response));
    ccf::jsonhandler::set_response(std::move(response), base);
    CHECK(context->get_response_status() == HTTP_STATUS_OK);
    CHECK(ccf::parse_json_safe(context->get_response_body()) == expected);
  }
}

TEST_CASE("Typed success and errors share a deducible handler return type")
{
  const auto handler = [](bool success) {
    if (success)
    {
      return ccf::make_success(Response{"hello", false});
    }
    return ccf::make_error(
      HTTP_STATUS_BAD_REQUEST, ccf::errors::InvalidInput, "invalid");
  };
  static_assert(
    std::
      same_as<decltype(handler(true)), ccf::jsonhandler::JsonAdapterResponse>);
  CHECK(
    std::holds_alternative<ccf::jsonhandler::SerialisedJson>(handler(true)));
  CHECK(std::holds_alternative<ccf::ErrorDetails>(handler(false)));
}

TEST_CASE("Typed JSON responses preserve replacement of malformed UTF-8")
{
  const auto check_parity = [](const auto& value) {
    const auto dom_context = make_context();
    const auto typed_context = make_context();
    std::shared_ptr<ccf::RpcContext> dom_base = dom_context;
    std::shared_ptr<ccf::RpcContext> typed_base = typed_context;
    ccf::jsonhandler::set_response(
      ccf::make_success(nlohmann::json(value)), dom_base);
    ccf::jsonhandler::set_response(ccf::make_success(value), typed_base);
    CHECK(
      ccf::parse_json_safe(typed_context->get_response_body()) ==
      ccf::parse_json_safe(dom_context->get_response_body()));
  };
  for (const auto& text :
       {std::string("\xff"),
        std::string("\xc3"),
        std::string("\xc3("),
        std::string("\xed\xa0\x80"),
        std::string("\xf0\x9f\x98\x80")})
  {
    check_parity(Response{text, false});
    check_parity(CustomTextOutput{text});
    check_parity(WithCustomTextOutput{{text}, {{text, text}}});
  }
}

TEST_CASE("Automatic responses retain nullable explicit serializers")
{
  const std::shared_ptr<response_test::AbstractLeaf> value =
    std::make_shared<response_test::ConcreteLeaf>();
  const auto check_parity = [](const auto& typed) {
    const auto context = make_context();
    std::shared_ptr<ccf::RpcContext> base = context;
    ccf::jsonhandler::set_response(ccf::make_success(typed), base);
    CHECK(context->get_response_status() == HTTP_STATUS_OK);
    CHECK(
      ccf::parse_json_safe(context->get_response_body()) ==
      nlohmann::json(typed));
  };
  check_parity(value);
  check_parity(WithSpecializedPointer{value});
  const auto context = make_context("text/plain");
  std::shared_ptr<ccf::RpcContext> base = context;
  ccf::jsonhandler::set_response(
    ccf::make_success(std::shared_ptr<response_test::AbstractLeaf>{}), base);
  CHECK(context->get_response_status() == HTTP_STATUS_NO_CONTENT);
}
