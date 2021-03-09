// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

// Local
#include "base_endpoint_registry.h"

// CCF
#include "http/http_consts.h"
#include "http/ws_consts.h"
#include "node/code_id.h"
#include "node/rpc/json_handler.h"

namespace ccf
{
  /*
   * Extends the BaseEndpointRegistry by installing common endpoints we expect
   * to be available on most services. Override init_handlers or inherit from
   * BaseEndpointRegistry directly if you wish to wrap some of this
   * functionality in different Endpoints.
   */
  class CommonEndpointRegistry : public BaseEndpointRegistry
  {
  public:
    CommonEndpointRegistry(
      const std::string& method_prefix_, AbstractNodeState& node_state) :
      BaseEndpointRegistry(method_prefix_, node_state)
    {}

    void init_handlers() override
    {
      BaseEndpointRegistry::init_handlers();

      auto get_tx_status = [this](auto&, nlohmann::json&& params) {
        const auto in = params.get<GetTxStatus::In>();

        GetTxStatus::Out out;
        const auto result =
          get_status_for_txid_v1(in.view, in.seqno, out.status);
        if (result == ccf::ApiResult::OK)
        {
          out.view = in.view;
          out.seqno = in.seqno;
          return make_success(out);
        }
        else
        {
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            fmt::format("Error code: {}", ccf::api_result_to_str(result)));
        }
      };
      make_command_endpoint(
        "tx", HTTP_GET, json_command_adapter(get_tx_status), no_auth_required)
        .set_auto_schema<GetTxStatus>()
        .install();

      make_command_endpoint(
        "local_tx",
        HTTP_GET,
        json_command_adapter(get_tx_status),
        no_auth_required)
        .set_auto_schema<GetTxStatus>()
        .set_execute_outside_consensus(
          ccf::endpoints::ExecuteOutsideConsensus::Locally)
        .install();

      auto get_code = [](auto& args, nlohmann::json&&) {
        GetCode::Out out;

        auto codes_ids = args.tx.template ro<CodeIDs>(Tables::NODE_CODE_IDS);
        codes_ids->foreach(
          [&out](const ccf::CodeDigest& cd, const ccf::CodeStatus& cs) {
            auto digest = fmt::format("{:02x}", fmt::join(cd, ""));
            out.versions.push_back({digest, cs});
            return true;
          });

        return make_success(out);
      };
      make_read_only_endpoint(
        "code", HTTP_GET, json_read_only_adapter(get_code), no_auth_required)
        .set_auto_schema<void, GetCode::Out>()
        .install();

      auto openapi = [this](kv::Tx& tx, nlohmann::json&&) {
        nlohmann::json document;
        const auto result = generate_openapi_document_v1(
          tx,
          openapi_info.title,
          openapi_info.description,
          openapi_info.document_version,
          document);

        if (result == ccf::ApiResult::OK)
        {
          return make_success(document);
        }
        else
        {
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            fmt::format("Error code: {}", ccf::api_result_to_str(result)));
        }
      };
      make_endpoint("api", HTTP_GET, json_adapter(openapi), no_auth_required)
        .set_auto_schema<void, GetAPI::Out>()
        .install();

      auto endpoint_metrics_fn = [this](auto&, nlohmann::json&&) {
        EndpointMetrics::Out out;
        endpoint_metrics(out);
        return make_success(out);
      };
      make_command_endpoint(
        "api/metrics",
        HTTP_GET,
        json_command_adapter(endpoint_metrics_fn),
        no_auth_required)
        .set_auto_schema<void, EndpointMetrics::Out>()
        .set_execute_outside_consensus(
          ccf::endpoints::ExecuteOutsideConsensus::Locally)
        .install();

      auto get_receipt = [this](auto&, nlohmann::json&& params) {
        const auto in = params.get<GetReceipt::In>();

        GetReceipt::Out out;
        const auto result = get_receipt_for_seqno_v1(in.commit, out.receipt);
        if (result == ccf::ApiResult::OK)
        {
          return make_success(out);
        }
        else
        {
          return make_error(
            HTTP_STATUS_INTERNAL_SERVER_ERROR,
            ccf::errors::InternalError,
            fmt::format("Error code: {}", ccf::api_result_to_str(result)));
        }
      };
      make_command_endpoint(
        "receipt",
        HTTP_GET,
        json_command_adapter(get_receipt),
        no_auth_required)
        .set_auto_schema<GetReceipt>()
        .install();

      auto verify_receipt = [this](auto&, nlohmann::json&& params) {
        const auto in = params.get<VerifyReceipt::In>();

        if (history != nullptr)
        {
          try
          {
            bool v = history->verify_receipt(in.receipt);
            const VerifyReceipt::Out out{v};

            return make_success(out);
          }
          catch (const std::exception& e)
          {
            return make_error(
              HTTP_STATUS_BAD_REQUEST,
              ccf::errors::InvalidInput,
              fmt::format("Unable to verify receipt: {}", e.what()));
          }
        }

        return make_error(
          HTTP_STATUS_INTERNAL_SERVER_ERROR,
          ccf::errors::InternalError,
          "Unable to verify receipt.");
      };
      make_command_endpoint(
        "receipt/verify",
        HTTP_POST,
        json_command_adapter(verify_receipt),
        no_auth_required)
        .set_auto_schema<VerifyReceipt>()
        .install();
    }
  };
}
