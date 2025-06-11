// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "node/rpc/frontend.h"
// #include "node/rpc/test/frontend_test_infra.h"
// #include "node/rpc/test/node_stub.h"

#define PICOBENCH_IMPLEMENT_WITH_MAIN
#include <picobench/picobench.hpp>

struct StubRpcContext : public ccf::RpcContext
{
  std::string request_path;
  ccf::RESTVerb verb;

  virtual std::string get_method() const
  {
    return request_path;
  }

  virtual const ccf::RESTVerb& get_request_verb() const
  {
    return verb;
  }

  virtual std::shared_ptr<ccf::SessionContext> get_session_context() const
  {
    throw std::logic_error(__func__);
  }
  virtual void set_user_data(std::shared_ptr<void> data)
  {
    throw std::logic_error(__func__);
  }
  virtual void* get_user_data() const
  {
    throw std::logic_error(__func__);
  }
  virtual const std::vector<uint8_t>& get_request_body() const
  {
    throw std::logic_error(__func__);
  }
  virtual const std::string& get_request_query() const
  {
    throw std::logic_error(__func__);
  }
  virtual std::string get_request_path() const
  {
    throw std::logic_error(__func__);
  }
  virtual std::shared_ptr<ccf::http::HTTPResponder> get_responder() const
  {
    throw std::logic_error(__func__);
  }
  virtual const ccf::PathParams& get_request_path_params()
  {
    throw std::logic_error(__func__);
  }
  virtual const ccf::PathParams& get_decoded_request_path_params()
  {
    throw std::logic_error(__func__);
  }
  virtual const ccf::http::HeaderMap& get_request_headers() const
  {
    throw std::logic_error(__func__);
  }
  virtual std::optional<std::string> get_request_header(
    const std::string_view& name) const
  {
    throw std::logic_error(__func__);
  }
  virtual const std::string& get_request_url() const
  {
    throw std::logic_error(__func__);
  }
  virtual ccf::FrameFormat frame_format() const
  {
    throw std::logic_error(__func__);
  }
  virtual void set_response_body(const std::vector<uint8_t>& body)
  {
    throw std::logic_error(__func__);
  }
  virtual void set_response_body(std::vector<uint8_t>&& body)
  {
    throw std::logic_error(__func__);
  }
  virtual void set_response_body(std::string&& body)
  {
    throw std::logic_error(__func__);
  }
  virtual const std::vector<uint8_t>& get_response_body() const
  {
    throw std::logic_error(__func__);
  }
  virtual void set_response_status(int status)
  {
    throw std::logic_error(__func__);
  }
  virtual int get_response_status() const
  {
    throw std::logic_error(__func__);
  }
  virtual void set_response_header(
    const std::string_view& name, const std::string_view& value)
  {
    throw std::logic_error(__func__);
  }
  virtual void clear_response_headers()
  {
    throw std::logic_error(__func__);
  }
  virtual void set_response_trailer(
    const std::string_view& name, const std::string_view& value)
  {
    throw std::logic_error(__func__);
  }
  virtual void set_response_json(
    const nlohmann::json& body, ccf::http_status status)
  {
    throw std::logic_error(__func__);
  }
  virtual void set_error(
    ccf::http_status status,
    const std::string& code,
    std::string&& msg,
    const std::vector<nlohmann::json>& details = {})
  {
    throw std::logic_error(__func__);
  }
  virtual void set_error(ccf::ErrorDetails&& error)
  {
    throw std::logic_error(__func__);
  }
  virtual void set_apply_writes(bool apply)
  {
    throw std::logic_error(__func__);
  }
  virtual void set_claims_digest(ccf::ClaimsDigest::Digest&& digest)
  {
    throw std::logic_error(__func__);
  }
};

static void dispatch(picobench::state& s)
{
  ccf::endpoints::EndpointRegistry registry("/constant/prefix");

  std::vector<std::string> elements = {
    "foo", "bar", "baz", "qux", "quux", "corge", "grault", "garply"};
  std::sort(elements.begin(), elements.end());

  std::set<std::string> paths;

  auto first_it = elements.begin();
  while (first_it != elements.end())
  {
    auto second_it = first_it;
    while (second_it != elements.end())
    {
      auto third_it = second_it;
      while (third_it != elements.end())
      {
        std::vector<std::string> current;
        current.push_back(*first_it);
        current.push_back(*second_it);
        current.push_back(*third_it);

        do
        {
          paths.insert(fmt::format("/{}", fmt::join(current, "/")));
        } while (std::next_permutation(current.begin(), current.end()));

        ++third_it;
      }
      ++second_it;
    }
    ++first_it;
  }

  std::cout << fmt::format("Produced {} paths:", paths.size()) << std::endl;
  if (paths.size() <= 20)
  {
    for (const auto& path : paths)
    {
      std::cout << fmt::format("  {}", path) << std::endl;
    }
  }
  else
  {
    auto it = paths.begin();
    for (auto i = 0; i < 10; ++i)
    {
      std::cout << fmt::format("  {}", *it++) << std::endl;
    }
    std::cout << "  ..." << std::endl;
    it = paths.end();
    std::advance(it, -10);
    while (it != paths.end())
    {
      std::cout << fmt::format("  {}", *it++) << std::endl;
    }
  }

  for (const auto& path : paths)
  {
    registry.make_endpoint(path, HTTP_POST, [](auto& ctx) {}, {}).install();
  }

  ccf::kv::Store store;
  ccf::kv::CommittableTx tx = store.create_tx();
  StubRpcContext rpc_ctx;

  rpc_ctx.verb = HTTP_POST;

  s.start_timer();
  for (size_t i = 0; i < s.iterations(); ++i)
  {
    for (const auto& path : paths)
    {
      rpc_ctx.request_path = path;
      registry.find_endpoint(tx, rpc_ctx);
    }
    // TODO
  }
  s.stop_timer();
}

const std::vector<int> dispatch_sizes = {1};

PICOBENCH_SUITE("dispatch");
PICOBENCH(dispatch).iterations(dispatch_sizes).baseline();
