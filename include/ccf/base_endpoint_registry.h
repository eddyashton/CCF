// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once

#include "ccf/endpoint_registry.h"
#include "ds/json.h"
#include "enclave/node_context.h"
#include "node/quote.h"
#include "node/rpc/node_interface.h"

namespace ccf
{
  /** Lists the possible return codes from the versioned APIs in @see
   * ccf::BaseEndpointRegistry
   */
  enum class ApiResult
  {
    /** Call was successful, results can be used */
    OK = 0,
    /** The node is not yet initialised, and doesn't have access to the service
       needed to answer this call. Should only be returned if the API is called
       too early, during node construction. */
    Uninitialised,
    /** One of the arguments passed to the function is invalid. It may be
       outside the range of known values, or not be in the expected format. */
    InvalidArgs,
    /** The requsted value was not found. */
    NotFound,
    /** General error not covered by the cases above. Generally means that an
       unexpected exception was thrown during execution. */
    InternalError,
  };

  constexpr char const* api_result_to_str(ApiResult result)
  {
    switch (result)
    {
      case ApiResult::OK:
      {
        return "OK";
      }
      case ApiResult::Uninitialised:
      {
        return "Uninitialised";
      }
      case ApiResult::InvalidArgs:
      {
        return "InvalidArgs";
      }
      case ApiResult::NotFound:
      {
        return "NotFound";
      }
      case ApiResult::InternalError:
      {
        return "InternalError";
      }
      default:
      {
        return "Unhandled ApiResult";
      }
    }
  }

  /** Extends the basic @ref EndpointRegistry with helper API methods for
   * retrieving core CCF properties.
   *
   * The API methods are versioned with a @c _vN suffix. App developers should
   * use the latest version which provides the values they need. Note that the
   * @c N in these versions is specific to each method name, and is not related
   * to a specific CCF release version. These APIs will be stable and supported
   * for several CCF releases.
   *
   * The methods have a consistent calling pattern, taking their arguments first
   * and setting results to the later out-parameters, passed by reference. All
   * return an @ref ApiResult, with value OK if the call succeeded.
   */
  class BaseEndpointRegistry : public ccf::endpoints::EndpointRegistry
  {
  protected:
    ccfapp::AbstractNodeContext& context;

  public:
    BaseEndpointRegistry(
      const std::string& method_prefix_,
      ccfapp::AbstractNodeContext& context_) :
      ccf::endpoints::EndpointRegistry(method_prefix_),
      context(context_)
    {}

    /** Get the status of a transaction by ID, provided as a view+seqno pair.
     * This is a node-local property - while it will converge on all nodes in
     * a healthy network, it is derived from distributed state rather than
     * distributed itself.
     * @see ccf::TxStatus
     */
    ApiResult get_status_for_txid_v1(
      kv::Consensus::View view,
      kv::Consensus::SeqNo seqno,
      ccf::TxStatus& tx_status)
    {
      try
      {
        if (consensus != nullptr)
        {
          const auto tx_view = consensus->get_view(seqno);
          const auto committed_seqno = consensus->get_committed_seqno();
          const auto committed_view = consensus->get_view(committed_seqno);

          tx_status = ccf::evaluate_tx_status(
            view, seqno, tx_view, committed_view, committed_seqno);
        }
        else
        {
          tx_status = ccf::TxStatus::Unknown;
        }

        return ApiResult::OK;
      }
      catch (const std::exception& e)
      {
        LOG_TRACE_FMT("{}", e.what());
        return ApiResult::InternalError;
      }
    }

    /** Get the ID of latest transaction known to be committed.
     */
    ApiResult get_last_committed_txid_v1(
      kv::Consensus::View& view, kv::Consensus::SeqNo& seqno)
    {
      if (consensus != nullptr)
      {
        try
        {
          const auto [v, s] = consensus->get_committed_txid();
          view = v;
          seqno = s;
          return ApiResult::OK;
        }
        catch (const std::exception& e)
        {
          LOG_TRACE_FMT("{}", e.what());
          return ApiResult::InternalError;
        }
      }
      else
      {
        return ApiResult::Uninitialised;
      }
    }

    /** Generate an OpenAPI document describing the currently installed
     * endpoints.
     *
     * The document is compatible with OpenAPI version 3.0.0 - the _v1 suffix
     * describes the version of this API, not the returned document format.
     * Similarly, the document_version argument should be used to version the
     * returned document itself as the set of endpoints or their APIs change, it
     * does not affect the OpenAPI version of the format of the document.
     */
    ApiResult generate_openapi_document_v1(
      kv::ReadOnlyTx& tx,
      const std::string& title,
      const std::string& description,
      const std::string& document_version,
      nlohmann::json& document)
    {
      try
      {
        document =
          ds::openapi::create_document(title, description, document_version);
        build_api(document, tx);
        return ApiResult::OK;
      }
      catch (const std::exception& e)
      {
        LOG_TRACE_FMT("{}", e.what());
        return ApiResult::InternalError;
      }
    }

    /** Get a quote attesting to the hardware this node is running on.
     */
    ApiResult get_quote_for_this_node_v1(
      kv::ReadOnlyTx& tx, QuoteInfo& quote_info)
    {
      try
      {
        const auto node_id = context.get_node_state().get_node_id();
        auto nodes = tx.ro<ccf::Nodes>(Tables::NODES);
        const auto node_info = nodes->get(node_id);

        if (!node_info.has_value())
        {
          LOG_TRACE_FMT("{} is not a known node", node_id);
          return ApiResult::NotFound;
        }

        quote_info = node_info->quote_info;
        return ApiResult::OK;
      }
      catch (const std::exception& e)
      {
        LOG_TRACE_FMT("{}", e.what());
        return ApiResult::InternalError;
      }
    }

    /** Get the view associated with a given seqno, to construct a valid TxID
     */
    ApiResult get_view_for_seqno_v1(kv::SeqNo seqno, kv::Consensus::View& view)
    {
      try
      {
        if (consensus != nullptr)
        {
          const auto v = consensus->get_view(seqno);
          if (v != ccf::VIEW_UNKNOWN)
          {
            view = v;
            return ApiResult::OK;
          }
          else
          {
            return ApiResult::NotFound;
          }
        }
        else
        {
          return ApiResult::Uninitialised;
        }
      }
      catch (const std::exception& e)
      {
        LOG_TRACE_FMT("{}", e.what());
        return ApiResult::InternalError;
      }
    }
  };
}