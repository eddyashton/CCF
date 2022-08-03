// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#include "ccf/app_interface.h"
#include "ccf/common_auth_policies.h"
#include "ccf/http_query.h"
#include "ccf/json_handler.h"

#define FMT_HEADER_ONLY
#include <fmt/format.h>

namespace grpc
{
  using ExecutorCodeID = std::vector<uint8_t>;

  namespace requestbodies
  {
    struct RegisterExecutor
    {
      ExecutorCodeID code_id;
      crypto::Pem identity;
    };
    DECLARE_JSON_TYPE(RegisterExecutor);
    DECLARE_JSON_REQUIRED_FIELDS(RegisterExecutor, code_id, identity);
  }

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
        auto executor_attestations_handle =
          ctx.tx.template wo<BLInfos>(BL_INFOS);
        executor_attestations_handle->put(
          body.code_id, body.supported_operations);
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
        auto executor_attestations_handle =
          ctx.tx.template wo<BLInfos>(BL_INFOS);
        const auto deleted = executor_attestations_handle->remove(body.code_id);
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
        auto executor_attestations_handle =
          ctx.tx.template ro<BLInfos>(BL_INFOS);
        std::map<ExecutorCodeID, Dispatchables> response;
        executor_attestations_handle->foreach(
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

  class DispatcherHandlers : public ccf::UserEndpointRegistry
  {
  protected:
    struct ExecutorInfo
    {};

    std::unordered_map<crypto::Pem, ExecutorInfo> registered_executors;

  public:
    DispatcherHandlers(ccfapp::AbstractNodeContext& context) :
      ccf::UserEndpointRegistry(context)
    {
      gov::TODO::register_attestation_handlers(this);

      auto register_executor = [this](auto& ctx, nlohmann::json&& params) {
        CCF_APP_INFO("Registering new executor: {}", params.dump());
        const auto body = params.get<requestbodies::RegisterExecutor>();

        // Check code_id is known and trusted
        auto executor_attestations_handle =
          ctx.tx.template ro<gov::TODO::BLInfos>(gov::TODO::BL_INFOS);
        if (!executor_attestations_handle->has(body.code_id))
        {
          CCF_APP_INFO("Unrecognised code_id");
          return ccf::make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::InvalidInput,
            fmt::format(
              "Unrecognised code_id: {:02x}", fmt::join(body.code_id, " ")));
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

        registered_executors.emplace_hint(it, body.identity, ExecutorInfo{});

        CCF_APP_INFO("Successfully registered");
        return ccf::make_success();
      };
      make_read_only_endpoint(
        "/register",
        HTTP_POST,
        ccf::json_read_only_adapter(register_executor),
        ccf::no_auth_required)
        .install();
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