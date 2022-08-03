// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include "ccf/app_interface.h"
#include "ccf/common_auth_policies.h"
#include "ccf/http_query.h"
#include "ccf/json_handler.h"

#include <deque>
#define FMT_HEADER_ONLY
#include <fmt/format.h>

namespace grpc
{
  using ExecutorCodeID = std::vector<uint8_t>;

  // TODO: This should all be in governance. Placeholder implementations here
  // for prototyping.
  namespace gov::TODO
  {
    using Dispatchables = std::vector<ccf::endpoints::EndpointKey>;
    struct Attestation
    {
      ExecutorCodeID code_id;
    };
    DECLARE_JSON_TYPE(Attestation);
    DECLARE_JSON_REQUIRED_FIELDS(Attestation, code_id);

    struct BLInfo : public Attestation
    {
      Dispatchables supported_operations;
    };
    DECLARE_JSON_TYPE_WITH_BASE(BLInfo, Attestation);
    DECLARE_JSON_REQUIRED_FIELDS(BLInfo, supported_operations);

    using BLInfos = kv::Map<ExecutorCodeID, Dispatchables>;
    static constexpr auto BL_INFOS = "business_logic_attestations";

    static void register_attestation_handlers(
      ccf::endpoints::EndpointRegistry* er)
    {
      auto add_code_ids = [](auto& ctx, nlohmann::json&& params) {
        const auto body = params.get<BLInfo>();
        CCF_APP_INFO("Inserting code_id: {:02x}", fmt::join(body.code_id, " "));
        auto bl_info_handle = ctx.tx.template wo<BLInfos>(BL_INFOS);
        bl_info_handle->put(body.code_id, body.supported_operations);
        return ccf::make_success();
      };
      er->make_endpoint(
          "/executors/code_ids",
          HTTP_POST,
          ccf::json_adapter(add_code_ids),
          ccf::no_auth_required)
        .install();

      auto remove_code_ids = [](auto& ctx, nlohmann::json&& params) {
        const auto body = params.get<Attestation>();
        CCF_APP_INFO("Removing code_id: {:02x}", fmt::join(body.code_id, " "));
        auto bl_info_handle = ctx.tx.template wo<BLInfos>(BL_INFOS);
        const auto deleted = bl_info_handle->remove(body.code_id);
        if (!deleted)
        {
          return ccf::make_error(
            HTTP_STATUS_NOT_FOUND, ccf::errors::ResourceNotFound, "TODO");
        }
        return ccf::make_success();
      };
      er->make_endpoint(
          "/executors/code_ids",
          HTTP_DELETE,
          ccf::json_adapter(remove_code_ids),
          ccf::no_auth_required)
        .install();

      auto list_code_ids = [](auto& ctx, nlohmann::json&& params) {
        CCF_APP_INFO("Listing code_ids");
        auto bl_info_handle = ctx.tx.template ro<BLInfos>(BL_INFOS);
        std::map<ExecutorCodeID, Dispatchables> response;
        bl_info_handle->foreach(
          [&response](const auto& code_id, const auto& dispatch) {
            response[code_id] = dispatch;
            return true;
          });
        return ccf::make_success(response);
      };
      er->make_endpoint(
          "/executors/code_ids",
          HTTP_GET,
          ccf::json_adapter(list_code_ids),
          ccf::no_auth_required)
        .install();
    }
  }

  namespace requestbodies
  {
    struct RegisterExecutor
    {
      gov::TODO::Attestation attestation;
      crypto::Pem identity;
    };
    DECLARE_JSON_TYPE(RegisterExecutor);
    DECLARE_JSON_REQUIRED_FIELDS(RegisterExecutor, attestation, identity);
  }

  class DispatcherHandlers : public ccf::UserEndpointRegistry
  {
  protected:
    struct ExecutorInfo
    {
      gov::TODO::Attestation attestation;
    };

    std::unordered_map<crypto::Pem, ExecutorInfo> registered_executors;

    // TODO: See if this cache as a reusable pattern in the KV
    // TODO: Doesn't handle URIs with slugs/path parameters in
    // TODO: Use r3 for this https://github.com/c9s/r3
    using SerialisedDispatchKey = std::string;
    SerialisedDispatchKey get_sdk(
      const ccf::RESTVerb& method, const std::string_view& uri)
    {
      return fmt::format("{} {}", method.c_str(), uri);
    }
    using DispatchTable =
      std::unordered_map<SerialisedDispatchKey, std::deque<crypto::Pem>>;
    using VersionedDispatchTable = std::pair<kv::Version, DispatchTable>;
    VersionedDispatchTable dispatcher;

    void rebuild_dispatcher(kv::ReadOnlyTx& tx, bool force_rebuild = false)
    {
      CCF_APP_INFO("Rebuilding dispatch table");

      auto bl_info_handle =
        tx.template ro<gov::TODO::BLInfos>(gov::TODO::BL_INFOS);
      kv::Version latest_version = 0;

      {
        // TODO: Better way to get version for this cached instance than
        // foreach?
        bl_info_handle->foreach_key(
          [&bl_info_handle, &latest_version](const auto& k) {
            const auto v = bl_info_handle->get_version_of_previous_write(k);
            latest_version = std::max(latest_version, v.value_or(0));
            return true;
          });
      }

      if (force_rebuild || (dispatcher.first != latest_version))
      {
        CCF_APP_INFO("Rebuilding dispatch table at {}", latest_version);
        DispatchTable dt;
        bl_info_handle->foreach(
          [this, &dt](const auto& code_id, const auto& dispatchables) {
            std::deque<crypto::Pem> dispatchees;
            for (const auto& [pem, ei] : registered_executors)
            {
              if (ei.attestation.code_id == code_id)
              {
                dispatchees.push_back(pem);
              }
            }
            for (const ccf::endpoints::EndpointKey& dispatch_key :
                 dispatchables)
            {
              const auto sdk =
                get_sdk(dispatch_key.verb, dispatch_key.uri_path);
              auto it = dt.find(sdk);
              if (it != dt.end())
              {
                CCF_APP_FAIL(
                  "Overwriting dispatch info - already have something "
                  "registered for {}",
                  sdk);
              }
              dt.emplace_hint(it, sdk, dispatchees);
            }
            return true;
          });
        CCF_APP_INFO("Resulting dispatch table contains {} entries", dt.size());
        for (const auto& [k, v] : dt)
        {
          CCF_APP_INFO("  '{}' can go to {} executors", k, v.size());
          for (const auto& p : v)
          {
            CCF_APP_INFO("    {}", p.str());
          }
        }
        dispatcher = std::make_pair(latest_version, dt);
      }
      else
      {
        CCF_APP_INFO("Dispatch table is still valid");
      }
    }

    template <typename Ctx>
    bool check_attestation(
      Ctx& ctx,
      const gov::TODO::Attestation& attestation,
      ExecutorCodeID& code_id)
    {
      // Currently a nop, could shim something temporarily, should eventually go
      // to PAL
      code_id = attestation.code_id;
      return true;
    }

    struct DispatchedEndpoint : public ccf::endpoints::EndpointDefinition
    {
      crypto::Pem chosen_executor;

      DispatchedEndpoint(crypto::Pem&& p) : chosen_executor(std::move(p)) {}
    };

  public:
    DispatcherHandlers(ccfapp::AbstractNodeContext& context) :
      ccf::UserEndpointRegistry(context)
    {
      gov::TODO::register_attestation_handlers(this);

      auto register_executor = [this](auto& ctx, nlohmann::json&& params) {
        CCF_APP_INFO("Registering new executor: {}", params.dump());
        const auto body = params.get<requestbodies::RegisterExecutor>();

        // Check attestation is good
        ExecutorCodeID code_id;
        if (!check_attestation(ctx, body.attestation, code_id))
        {
          CCF_APP_INFO("Bad attestation");
          return ccf::make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::InvalidInput,
            "Bad attestation");
        }

        // Check code_id is trusted
        auto bl_info_handle =
          ctx.tx.template ro<gov::TODO::BLInfos>(gov::TODO::BL_INFOS);
        if (!bl_info_handle->has(code_id))
        {
          CCF_APP_INFO("Unrecognised code_id");
          return ccf::make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::InvalidInput,
            fmt::format(
              "Unrecognised code_id: {:02x}", fmt::join(code_id, " ")));
        }

        auto it = registered_executors.find(body.identity);
        if (it != registered_executors.end())
        {
          CCF_APP_INFO("Already exists");
          return ccf::make_error(
            HTTP_STATUS_CONFLICT,
            "AlreadyExists",
            "An executor with this identity already exists.");
        }

        ExecutorInfo ei{code_id};
        registered_executors.emplace_hint(
          it, body.identity, ExecutorInfo{body.attestation});

        CCF_APP_INFO("Successfully registered");

        rebuild_dispatcher(ctx.tx, true);

        return ccf::make_success();
      };
      make_read_only_endpoint(
        "/register",
        HTTP_POST,
        ccf::json_read_only_adapter(register_executor),
        ccf::no_auth_required)
        .install();
    }

    ccf::endpoints::EndpointDefinitionPtr find_endpoint(
      kv::Tx& tx, ccf::RpcContext& rpc_ctx) override
    {
      // Prefer built-in and locally installed endpoints, before looking at
      // dispatch table
      auto base_endpoint =
        ccf::endpoints::EndpointRegistry::find_endpoint(tx, rpc_ctx);
      if (base_endpoint != nullptr)
      {
        return base_endpoint;
      }

      rebuild_dispatcher(tx);

      const auto key =
        get_sdk(rpc_ctx.get_request_verb(), rpc_ctx.get_method());

      auto& dispatch_table = dispatcher.second;
      const auto it = dispatch_table.find(key);
      if (it == dispatch_table.end())
      {
        // TODO: Return a better 404 impl?
        CCF_APP_FAIL("This app has no handlers for {}", key);
        return nullptr;
      }

      auto& executor_idents = it->second;
      if (executor_idents.empty())
      {
        CCF_APP_FAIL(
          "This node knows the code ID which should handle {}, but has no "
          "such executors currently registered",
          key);
        return nullptr;
      }

      CCF_APP_INFO("Have {} possible executors", executor_idents.size());

      // Dispatch to first executor in queue, pop this and move it to the back
      auto chosen_executor = executor_idents.front();
      if (executor_idents.size() > 1)
      {
        executor_idents.pop_front();
        executor_idents.push_back(chosen_executor);
      }

      CCF_APP_INFO("Chose to execute on {}", chosen_executor.str());

      auto ret =
        std::make_shared<DispatchedEndpoint>(std::move(chosen_executor));
      return ret;
    }

    void execute_endpoint(
      ccf::endpoints::EndpointDefinitionPtr e,
      ccf::endpoints::EndpointContext& endpoint_ctx) override
    {
      auto endpoint = dynamic_cast<const DispatchedEndpoint*>(e.get());
      if (endpoint != nullptr)
      {
        CCF_APP_INFO(
          "Executing {} {} on {}",
          endpoint_ctx.rpc_ctx->get_request_verb().c_str(),
          endpoint_ctx.rpc_ctx->get_request_path(),
          endpoint->chosen_executor.str());
        return;
      }

      ccf::endpoints::EndpointRegistry::execute_endpoint(e, endpoint_ctx);
    }
  };
} // namespace grpc

namespace ccfapp
{
  std::unique_ptr<ccf::endpoints::EndpointRegistry> make_user_endpoints(
    ccfapp::AbstractNodeContext& context)
  {
    return std::make_unique<grpc::DispatcherHandlers>(context);
  }
} // namespace ccfapp