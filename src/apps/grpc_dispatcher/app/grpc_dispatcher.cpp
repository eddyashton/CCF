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
  using ExecutorAttestation = std::vector<uint8_t>;

  namespace requestbodies
  {
    struct RegisterExecutor
    {
      ExecutorAttestation attestation;
      crypto::Pem identity;
    };
    DECLARE_JSON_TYPE(RegisterExecutor);
    DECLARE_JSON_REQUIRED_FIELDS(RegisterExecutor, attestation, identity);
  }

  struct ExecutorInfo
  {};
  DECLARE_JSON_TYPE(ExecutorInfo);
  DECLARE_JSON_REQUIRED_FIELDS(ExecutorInfo);

  namespace tables
  {
    using Executors = kv::Map<crypto::Pem, ExecutorInfo>;
    static constexpr auto EXECUTORS = "executors";
  }

  // TODO: This should all be in governance. Placeholder implementations here
  // for prototyping.
  namespace gov::TODO
  {
    using Dispatchables = std::vector<ccf::endpoints::EndpointKey>;
    struct Attestation
    {
      ExecutorAttestation attestation;
    };
    DECLARE_JSON_TYPE(Attestation);
    DECLARE_JSON_REQUIRED_FIELDS(Attestation, attestation);

    struct BLInfo : public Attestation
    {
      Dispatchables supported_operations;
    };
    DECLARE_JSON_TYPE_WITH_BASE(BLInfo, Attestation);
    DECLARE_JSON_REQUIRED_FIELDS(BLInfo, supported_operations);

    using BLInfos = kv::Map<ExecutorAttestation, Dispatchables>;
    static constexpr auto BL_INFOS = "business_logic_attestations";

    static void register_attestation_handlers(
      ccf::endpoints::EndpointRegistry* er)
    {
      auto add_attestation = [](auto& ctx, nlohmann::json&& params) {
        const auto body = params.get<BLInfo>();
        CCF_APP_INFO(
          "Inserting attestation: {:02x}", fmt::join(body.attestation, " "));
        auto executor_attestations_handle =
          ctx.tx.template wo<BLInfos>(BL_INFOS);
        executor_attestations_handle->put(
          body.attestation, body.supported_operations);
        return ccf::make_success();
      };
      er->make_endpoint(
          "/executors/attestations",
          HTTP_POST,
          ccf::json_adapter(add_attestation),
          ccf::no_auth_required)
        .install();

      auto remove_attestation = [](auto& ctx, nlohmann::json&& params) {
        const auto body = params.get<Attestation>();
        CCF_APP_INFO(
          "Removing attestation: {:02x}", fmt::join(body.attestation, " "));
        auto executor_attestations_handle =
          ctx.tx.template wo<BLInfos>(BL_INFOS);
        const auto deleted =
          executor_attestations_handle->remove(body.attestation);
        if (!deleted)
        {
          return ccf::make_error(
            HTTP_STATUS_NOT_FOUND, ccf::errors::ResourceNotFound, "TODO");
        }
        return ccf::make_success();
      };
      er->make_endpoint(
          "/executors/attestations",
          HTTP_DELETE,
          ccf::json_adapter(remove_attestation),
          ccf::no_auth_required)
        .install();

      auto list_attestations = [](auto& ctx, nlohmann::json&& params) {
        CCF_APP_INFO("Listing attestations");
        auto executor_attestations_handle =
          ctx.tx.template ro<BLInfos>(BL_INFOS);
        std::map<ExecutorAttestation, Dispatchables> response;
        executor_attestations_handle->foreach(
          [&response](const auto& attestation, const auto& dispatch) {
            response[attestation] = dispatch;
            return true;
          });
        return ccf::make_success(response);
      };
      er->make_endpoint(
          "/executors/attestations",
          HTTP_GET,
          ccf::json_adapter(list_attestations),
          ccf::no_auth_required)
        .install();
    }
  }

  class DispatcherHandlers : public ccf::UserEndpointRegistry
  {
  public:
    DispatcherHandlers(ccfapp::AbstractNodeContext& context) :
      ccf::UserEndpointRegistry(context)
    {
      gov::TODO::register_attestation_handlers(this);

      auto register_executor = [this](auto& ctx, nlohmann::json&& params) {
        CCF_APP_INFO("Registering new executor: {}", params.dump());
        const auto body = params.get<requestbodies::RegisterExecutor>();

        // Check attestation is known and trusted
        auto executor_attestations_handle =
          ctx.tx.template ro<gov::TODO::BLInfos>(gov::TODO::BL_INFOS);
        if (!executor_attestations_handle->has(body.attestation))
        {
          CCF_APP_INFO("Unrecognised attestation");
          return ccf::make_error(
            HTTP_STATUS_FORBIDDEN,
            ccf::errors::InvalidInput,
            fmt::format(
              "Unrecognised attestation: {:02x}",
              fmt::join(body.attestation, " ")));
        }

        auto executors_handle =
          ctx.tx.template rw<tables::Executors>(tables::EXECUTORS);
        if (executors_handle->has(body.identity))
        {
          CCF_APP_INFO("Already exists");
          return ccf::make_error(
            HTTP_STATUS_CONFLICT,
            "AlreadyExists",
            "An executor with this identity already exists.");
        }
        executors_handle->put(body.identity, {});

        CCF_APP_INFO("Successfully registered");
        return ccf::make_success();
      };
      make_endpoint(
        "/register",
        HTTP_POST,
        ccf::json_adapter(register_executor),
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