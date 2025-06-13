// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "node/rpc_context_impl.h"

// Extremely stub implementation for dispatch-benchmarking
struct StubRpcContext : public ccf::RpcContextImpl
{
  std::string request_path;
  ccf::RESTVerb verb;

  using ccf::RpcContextImpl::RpcContextImpl;

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
  virtual void set_tx_id(const ccf::TxID& tx_id)
  {
    throw std::logic_error(__func__);
  }
  virtual bool should_apply_writes() const
  {
    throw std::logic_error(__func__);
  }
  virtual void reset_response()
  {
    throw std::logic_error(__func__);
  }
  virtual std::vector<uint8_t> serialise_response() const
  {
    throw std::logic_error(__func__);
  }
  virtual const std::vector<uint8_t>& get_serialised_request()
  {
    throw std::logic_error(__func__);
  }
};