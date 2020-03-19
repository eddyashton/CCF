// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#pragma once
#include "commonhandlerregistry.h"
#include "consensus/pbft/pbftrequests.h"
#include "consensus/pbft/pbfttables.h"
#include "consts.h"
#include "ds/buffer.h"
#include "ds/spinlock.h"
#include "enclave/rpchandler.h"
#include "forwarder.h"
#include "node/clientsignatures.h"
#include "node/nodes.h"
#include "notifierinterface.h"
#include "rpcexception.h"
#include "tls/verifier.h"

#include <fmt/format_header_only.h>
#include <mutex>
#include <utility>
#include <vector>

namespace ccf
{
  class RpcFrontend : public enclave::RpcHandler, public ForwardedRpcHandler
  {
  protected:
    Store& tables;
    HandlerRegistry& handlers;

    void disable_request_storing()
    {
      request_storing_disabled = true;
    }

    virtual std::string invalid_caller_error_message() const
    {
      return "Could not find matching actor certificate";
    }

  private:
    std::map<CallerId, tls::VerifierPtr> verifiers;
    SpinLock lock;
    bool is_open_ = false;

    Nodes* nodes;
    ClientSignatures* client_signatures;
    pbft::RequestsMap* pbft_requests_map;
    kv::Consensus* consensus;
    std::shared_ptr<enclave::AbstractForwarder> cmd_forwarder;
    kv::TxHistory* history;

    size_t sig_max_tx = 1000;
    std::atomic<size_t> tx_count = 0;
    std::chrono::milliseconds sig_max_ms = std::chrono::milliseconds(1000);
    std::chrono::milliseconds ms_to_sig = std::chrono::milliseconds(1000);
    bool request_storing_disabled = false;

    void update_consensus()
    {
      auto c = tables.get_consensus().get();

      if (consensus != c)
      {
        consensus = c;
        handlers.set_consensus(consensus);
      }
    }

    void update_history()
    {
      history = tables.get_history().get();
      handlers.set_history(history);
    }

    std::optional<std::vector<uint8_t>> forward_or_redirect_json(
      std::shared_ptr<enclave::RpcContext> ctx)
    {
      if (cmd_forwarder && !ctx->session->original_caller.has_value())
      {
        return std::nullopt;
      }
      else
      {
        // If this frontend is not allowed to forward or the command has already
        // been forwarded, redirect to the current primary
        ctx->set_response_status(HTTP_STATUS_TEMPORARY_REDIRECT);
        if ((nodes != nullptr) && (consensus != nullptr))
        {
          NodeId primary_id = consensus->primary();
          Store::Tx tx;
          auto nodes_view = tx.get_view(*nodes);
          auto info = nodes_view->get(primary_id);

          if (info)
          {
            ctx->set_response_header(
              http::headers::LOCATION,
              fmt::format("{}:{}", info->pubhost, info->rpcport));
          }
        }

        return ctx->serialise_response();
      }
    }

    void record_client_signature(
      Store::Tx& tx, CallerId caller_id, const SignedReq& signed_request)
    {
      auto client_sig_view = tx.get_view(*client_signatures);
      if (request_storing_disabled)
      {
        SignedReq no_req;
        no_req.sig = signed_request.sig;
        client_sig_view->put(caller_id, no_req);
      }
      else
      {
        client_sig_view->put(caller_id, signed_request);
      }
    }

    bool verify_client_signature(
      const std::vector<uint8_t>& caller,
      const CallerId caller_id,
      const SignedReq& signed_request)
    {
      if (!client_signatures)
      {
        return false;
      }

      auto v = verifiers.find(caller_id);
      if (v == verifiers.end())
      {
        std::vector<uint8_t> caller_cert(caller);
        verifiers.emplace(
          std::make_pair(caller_id, tls::make_verifier(caller_cert)));
      }
      if (!verifiers[caller_id]->verify(
            signed_request.req, signed_request.sig, signed_request.md))
      {
        return false;
      }

      return true;
    }

    void set_response_unauthorized(
      std::shared_ptr<enclave::RpcContext>& ctx,
      std::string&& msg = "Failed to verify client signature") const
    {
      ctx->set_response_status(HTTP_STATUS_UNAUTHORIZED);
      ctx->set_response_header(
        http::headers::WWW_AUTHENTICATE,
        fmt::format(
          "Signature realm=\"Signed request access\", "
          "headers=\"(request-target) {}",
          http::headers::DIGEST));
      ctx->set_response_body(std::move(msg));
    }

  public:
    RpcFrontend(
      Store& tables_,
      HandlerRegistry& handlers_,
      ClientSignatures* client_sigs_ = nullptr) :
      tables(tables_),
      nodes(tables.get<Nodes>(Tables::NODES)),
      client_signatures(client_sigs_),
      handlers(handlers_),
      pbft_requests_map(
        tables.get<pbft::RequestsMap>(pbft::Tables::PBFT_REQUESTS)),
      consensus(nullptr),
      history(nullptr)
    {}

    void set_sig_intervals(size_t sig_max_tx_, size_t sig_max_ms_) override
    {
      sig_max_tx = sig_max_tx_;
      sig_max_ms = std::chrono::milliseconds(sig_max_ms_);
      ms_to_sig = sig_max_ms;
    }

    void set_cmd_forwarder(
      std::shared_ptr<enclave::AbstractForwarder> cmd_forwarder_) override
    {
      cmd_forwarder = cmd_forwarder_;
    }

    void open() override
    {
      std::lock_guard<SpinLock> mguard(lock);
      is_open_ = true;

      handlers.init_handlers(tables);
    }

    bool is_open() override
    {
      std::lock_guard<SpinLock> mguard(lock);
      return is_open_;
    }

    /** Process a serialised command with the associated RPC context
     *
     * If an RPC that requires writing to the kv store is processed on a
     * backup, the serialised RPC is forwarded to the current network primary.
     *
     * @param ctx Context for this RPC
     * @returns nullopt if the result is pending (to be forwarded, or still
     * to-be-executed by consensus), else the response (may contain error)
     */
    std::optional<std::vector<uint8_t>> process(
      std::shared_ptr<enclave::RpcContext> ctx) override
    {
      update_consensus();

      Store::Tx tx;

      auto caller_id = handlers.get_caller_id(tx, ctx->session->caller_cert);

      if (consensus != nullptr && consensus->type() == ConsensusType::PBFT)
      {
        auto rep = process_if_local_node_rpc(ctx, tx, caller_id);
        if (rep.has_value())
        {
          return rep;
        }
        kv::TxHistory::RequestID reqid;

        update_history();
        reqid = {
          caller_id, ctx->session->client_session_id, ctx->get_request_index()};
        if (history)
        {
          if (!history->add_request(
                reqid,
                caller_id,
                ctx->session->caller_cert,
                ctx->get_serialised_request()))
          {
            LOG_FAIL_FMT(
              "Adding request failed: {}, {}, {}",
              std::get<0>(reqid),
              std::get<1>(reqid),
              std::get<2>(reqid));
            ctx->set_response_status(HTTP_STATUS_INTERNAL_SERVER_ERROR);
            ctx->set_response_body("PBFT could not process request.");
            return ctx->serialise_response();
          }
          tx.set_req_id(reqid);
          return std::nullopt;
        }
        else
        {
          ctx->set_response_status(HTTP_STATUS_INTERNAL_SERVER_ERROR);
          ctx->set_response_body("PBFT is not yet ready.");
          return ctx->serialise_response();
        }
      }
      else
      {
        auto rep = process_command(ctx, tx, caller_id);

        // If necessary, forward the RPC to the current primary
        if (!rep.has_value())
        {
          if (consensus != nullptr)
          {
            auto primary_id = consensus->primary();

            if (
              primary_id != NoNode && cmd_forwarder &&
              cmd_forwarder->forward_command(
                ctx, primary_id, caller_id, get_cert_to_forward(ctx)))
            {
              // Indicate that the RPC has been forwarded to primary
              LOG_DEBUG_FMT("RPC forwarded to primary {}", primary_id);
              return std::nullopt;
            }
          }
          ctx->set_response_status(HTTP_STATUS_INTERNAL_SERVER_ERROR);
          ctx->set_response_body("RPC could not be forwarded to primary.");
          return ctx->serialise_response();
        }
        return rep.value();
      }
    }

    virtual std::vector<uint8_t> get_cert_to_forward(
      std::shared_ptr<enclave::RpcContext> ctx)
    {
      return ctx->session->caller_cert;
    }

    /** Process a serialised command with the associated RPC context via PBFT
     *
     * @param ctx Context for this RPC
     */
    ProcessPbftResp process_pbft(
      std::shared_ptr<enclave::RpcContext> ctx) override
    {
      Store::Tx tx;
      return process_pbft(ctx, tx, false);
    }

    ProcessPbftResp process_pbft(
      std::shared_ptr<enclave::RpcContext> ctx,
      Store::Tx& tx,
      bool playback) override
    {
      kv::Version version = kv::NoVersion;

      update_consensus();

      if (!playback)
      {
        auto req_view = tx.get_view(*pbft_requests_map);
        req_view->put(
          0,
          {ctx->session->original_caller.value().caller_id,
           ctx->session->caller_cert,
           ctx->get_serialised_request(),
           ctx->pbft_raw});
      }

      auto rep =
        process_command(ctx, tx, ctx->session->original_caller->caller_id);

      version = tx.get_version();

      return {rep.value(), version};
    }

    crypto::Sha256Hash get_merkle_root() override
    {
      return history->get_replicated_state_root();
    }

    void update_merkle_tree() override
    {
      history->execute_pending();
    }

    /** Process a serialised input forwarded from another node
     *
     * This function assumes that ctx contains the caller_id as read by the
     * forwarding backup.
     *
     * @param ctx Context for this forwarded RPC
     *
     * @return Serialised reply to send back to forwarder node
     */
    std::vector<uint8_t> process_forwarded(
      std::shared_ptr<enclave::RpcContext> ctx) override
    {
      if (!ctx->session->original_caller.has_value())
      {
        throw std::logic_error(
          "Processing forwarded command with unitialised forwarded context");
      }

      update_consensus();

      Store::Tx tx;

      auto rep =
        process_command(ctx, tx, ctx->session->original_caller->caller_id);
      if (!rep.has_value())
      {
        // This should never be called when process_command is called with a
        // forwarded RPC context
        throw std::logic_error("Forwarded RPC cannot be forwarded");
      }

      return rep.value();
    }

    std::optional<std::vector<uint8_t>> process_if_local_node_rpc(
      std::shared_ptr<enclave::RpcContext> ctx,
      Store::Tx& tx,
      CallerId caller_id)
    {
      const auto method = ctx->get_method();
      const auto local_method = method.substr(method.find_first_not_of('/'));
      auto handler = handlers.find_handler(local_method);
      if (handler != nullptr && handler->execute_locally)
      {
        return process_command(ctx, tx, caller_id);
      }
      return std::nullopt;
    }

    std::optional<std::vector<uint8_t>> process_command(
      std::shared_ptr<enclave::RpcContext> ctx,
      Store::Tx& tx,
      CallerId caller_id)
    {
      const auto method = ctx->get_method();
      const auto local_method = method.substr(method.find_first_not_of('/'));
      auto handler = handlers.find_handler(local_method);
      if (handler == nullptr)
      {
        ctx->set_response_status(HTTP_STATUS_NOT_FOUND);
        return ctx->serialise_response();
      }

      if (handler->require_client_identity && handlers.has_certs())
      {
        // Only if handler requires client identity.
        // If a request is forwarded, check that the caller is known. Otherwise,
        // only check that the caller id is valid.
        if (
          (ctx->session->original_caller.has_value() &&
           !lookup_forwarded_caller_cert(ctx, tx)) ||
          caller_id == INVALID_ID)
        {
          ctx->set_response_status(HTTP_STATUS_FORBIDDEN);
          ctx->set_response_body(invalid_caller_error_message());
          return ctx->serialise_response();
        }
      }

      bool is_primary = (consensus == nullptr) || consensus->is_primary() ||
        ctx->is_create_request;

      const auto signed_request = ctx->get_signed_request();
      if (handler->require_client_signature && !signed_request.has_value())
      {
        set_response_unauthorized(
          ctx, fmt::format("'{}' RPC must be signed", method));
        return ctx->serialise_response();
      }

      // By default, signed requests are verified and recorded, even on
      // handlers that do not require client signatures
      if (signed_request.has_value())
      {
        // For forwarded requests (raft only), skip verification as it is
        // assumed that the verification was done by the forwarder node.
        if (
          (!ctx->is_create_request &&
           (!(consensus != nullptr &&
              consensus->type() == ConsensusType::RAFT) ||
            !ctx->session->original_caller.has_value())) &&
          !verify_client_signature(
            ctx->session->caller_cert, caller_id, signed_request.value()))
        {
          set_response_unauthorized(ctx);
          return ctx->serialise_response();
        }

        if (is_primary)
        {
          record_client_signature(tx, caller_id, signed_request.value());
        }
      }

      update_history();

      if (!is_primary && consensus->type() == ConsensusType::RAFT)
      {
        switch (handler->read_write)
        {
          case HandlerRegistry::Read:
          {
            if (ctx->session->is_forwarding)
            {
              return forward_or_redirect_json(ctx);
            }
            break;
          }

          case HandlerRegistry::Write:
          {
            ctx->session->is_forwarding = true;
            return forward_or_redirect_json(ctx);
          }

          case HandlerRegistry::MayWrite:
          {
            const auto read_only_it =
              ctx->get_request_header(http::headers::CCF_READ_ONLY);
            if (!read_only_it.has_value() || (read_only_it.value() != "true"))
            {
              ctx->session->is_forwarding = true;
              return forward_or_redirect_json(ctx);
            }
            else if (ctx->session->is_forwarding)
            {
              return forward_or_redirect_json(ctx);
            }
            break;
          }
        }
      }

      auto func = handler->func;
      auto args = RequestArgs{ctx, tx, caller_id};

      tx_count++;

      while (true)
      {
        try
        {
          func(args);

          if (ctx->response_is_error())
          {
            return ctx->serialise_response();
          }

          switch (tx.commit())
          {
            case kv::CommitSuccess::OK:
            {
              auto cv = tx.commit_version();
              if (cv == 0)
                cv = tx.get_read_version();
              if (cv == kv::NoVersion)
                cv = tables.current_version();
              ctx->set_response_header(http::headers::CCF_COMMIT, cv);
              if (consensus != nullptr)
              {
                ctx->set_response_header(
                  http::headers::CCF_TERM, consensus->get_view());
                ctx->set_response_header(
                  http::headers::CCF_GLOBAL_COMMIT,
                  consensus->get_commit_seqno());

                if (
                  history && consensus->is_primary() &&
                  (cv % sig_max_tx == sig_max_tx / 2))
                {
                  if (consensus->type() == ConsensusType::RAFT)
                  {
                    history->emit_signature();
                  }
                  else
                  {
                    consensus->emit_signature();
                  }
                }
              }

              return ctx->serialise_response();
            }

            case kv::CommitSuccess::CONFLICT:
            {
              break;
            }

            case kv::CommitSuccess::NO_REPLICATE:
            {
              ctx->set_response_status(HTTP_STATUS_INTERNAL_SERVER_ERROR);
              ctx->set_response_body("Transaction failed to replicate.");
              return ctx->serialise_response();
            }
          }
        }
        catch (const RpcException& e)
        {
          ctx->set_response_status(e.status);
          ctx->set_response_body(e.what());
          return ctx->serialise_response();
        }
        catch (JsonParseError& e)
        {
          auto err = fmt::format("At {}:\n\t{}", e.pointer(), e.what());
          ctx->set_response_status(HTTP_STATUS_BAD_REQUEST);
          ctx->set_response_body(std::move(err));
          return ctx->serialise_response();
        }
        catch (const kv::KvSerialiserException& e)
        {
          // If serialising the committed transaction fails, there is no way
          // to recover safely (https://github.com/microsoft/CCF/issues/338).
          // Better to abort.
          LOG_FATAL_FMT("Failed to serialise: {}", e.what());
          abort();
        }
        catch (const std::exception& e)
        {
          ctx->set_response_status(HTTP_STATUS_INTERNAL_SERVER_ERROR);
          ctx->set_response_body(e.what());
          return ctx->serialise_response();
        }
      }
    }

    void tick(std::chrono::milliseconds elapsed) override
    {
      update_consensus();

      handlers.tick(elapsed, tx_count);

      // reset tx_counter for next tick interval
      tx_count = 0;

      if ((consensus != nullptr) && consensus->is_primary())
      {
        if (elapsed < ms_to_sig)
        {
          ms_to_sig -= elapsed;
          return;
        }

        ms_to_sig = sig_max_ms;
        if (history && tables.commit_gap() > 0)
        {
          if (consensus->type() == ConsensusType::RAFT)
          {
            history->emit_signature();
          }
          else
          {
            consensus->emit_signature();
          }
        }
      }
    }

    // Return false if frontend believes it should be able to look up caller
    // certs, but couldn't find caller. Default behaviour is that there are no
    // caller certs, so nothing is changed but we return true
    virtual bool lookup_forwarded_caller_cert(
      std::shared_ptr<enclave::RpcContext> ctx, Store::Tx& tx)
    {
      return true;
    }
  };
}
