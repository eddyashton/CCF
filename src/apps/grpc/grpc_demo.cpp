// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "ccf/app_interface.h"
#include "ccf/common_auth_policies.h"
#include "ccf/pal/locking.h"
#include "demo.pb.h"
#include "endpoints/grpc/grpc.h"
#include "http/http_builder.h"
#include "node/endpoint_context_impl.h"
#include "node/rpc/rpc_context_impl.h"

#define FMT_HEADER_ONLY
#include <fmt/format.h>

namespace grpcdemo
{
  class EndpointRegistry : public ccf::UserEndpointRegistry
  {
    // Ssed for streaming endpoints
    ccf::pal::Mutex subscribed_events_lock;
    std::
      unordered_map<std::string, ccf::grpc::DetachedStreamPtr<demo::SubResult>>
        subscribed_events;

  public:
    EndpointRegistry(ccfapp::AbstractNodeContext& context) :
      ccf::UserEndpointRegistry(context)
    {
      auto run_string_ops = [this](
                              ccf::endpoints::CommandEndpointContext& ctx,
                              std::vector<demo::OpIn>&& payload,
                              ccf::grpc::StreamPtr<demo::OpOut>&& out_stream) {
        for (demo::OpIn& op : payload)
        {
          demo::OpOut result;
          switch (op.op_case())
          {
            case (demo::OpIn::OpCase::kEcho):
            {
              LOG_INFO_FMT("Got kEcho");
              auto* echo_op = op.mutable_echo();
              auto* echoed = result.mutable_echoed();
              echoed->set_allocated_body(echo_op->release_body());
              break;
            }

            case (demo::OpIn::OpCase::kReverse):
            {
              LOG_INFO_FMT("Got kReverse");
              auto* reverse_op = op.mutable_reverse();
              std::string* s = reverse_op->release_body();
              std::reverse(s->begin(), s->end());
              auto* reversed = result.mutable_reversed();
              reversed->set_allocated_body(s);
              break;
            }

            case (demo::OpIn::OpCase::kTruncate):
            {
              LOG_INFO_FMT("Got kTruncate");
              auto* truncate_op = op.mutable_truncate();
              std::string* s = truncate_op->release_body();
              *s = s->substr(
                truncate_op->start(),
                truncate_op->end() - truncate_op->start());
              auto* truncated = result.mutable_truncated();
              truncated->set_allocated_body(s);
              break;
            }

            case (demo::OpIn::OpCase::OP_NOT_SET):
            {
              LOG_INFO_FMT("Got OP_NOT_SET");
              // oneof may always be null. If the input OpIn was null, then
              // the resulting OpOut is also null
              break;
            }
          }

          out_stream->stream_msg(result);
        }

        ctx.rpc_ctx->set_response_trailer(
          ccf::grpc::make_status_trailer(GRPC_STATUS_OK));
        ctx.rpc_ctx->set_response_trailer(
          ccf::grpc::make_message_trailer(grpc_status_str(GRPC_STATUS_OK)));

        return ccf::grpc::make_pending();
      };

      make_command_endpoint(
        "/demo.Demo/RunOps",
        HTTP_POST,
        ccf::grpc_command_unary_stream_adapter<
          std::vector<demo::OpIn>,
          demo::OpOut>(run_string_ops),
        ccf::no_auth_required)
        .install();

      auto sub = [this](
                   ccf::endpoints::CommandEndpointContext& ctx,
                   demo::Event&& payload,
                   ccf::grpc::StreamPtr<demo::SubResult>&& out_stream) {
        std::unique_lock<ccf::pal::Mutex> guard(subscribed_events_lock);

        auto it = subscribed_events.find(payload.name());
        if (it != subscribed_events.end())
        {
          LOG_INFO_FMT(
            "Returning subscription error - already have a subscriber for {}",
            payload.name());
          return ccf::grpc::GrpcAdapterStreamingResponse{ccf::grpc::make_error(
            GRPC_STATUS_FAILED_PRECONDITION,
            fmt::format(
              "Already have a subscriber for {} - only support a single "
              "subscriber per-event",
              payload.name()))};
        }
        else
        {
          // Signal to the caller that the subscription has been accepted
          demo::SubResult result;
          result.mutable_started();
          out_stream->stream_msg(result);

          subscribed_events.emplace_hint(
            it,
            payload.name(),
            ccf::grpc::detach_stream(
              ctx.rpc_ctx, std::move(out_stream), [this, event = payload]() {
                std::unique_lock<ccf::pal::Mutex> guard(subscribed_events_lock);

                auto search = subscribed_events.find(event.name());
                if (search != subscribed_events.end())
                {
                  LOG_INFO_FMT(
                    "Successfully cleaned up event: {}", event.name());
                  subscribed_events.erase(search);
                }
              }));
          LOG_INFO_FMT("Subscribed to event {}", payload.name());

          return ccf::grpc::GrpcAdapterStreamingResponse{
            ccf::grpc::make_pending()};
        }
      };
      make_endpoint(
        "/demo.Demo/Sub",
        HTTP_POST,
        ccf::grpc_command_unary_stream_adapter<demo::Event, demo::SubResult>(
          sub),
        {ccf::no_auth_required})
        .install();

      auto ack = [this](
                   ccf::endpoints::CommandEndpointContext& ctx,
                   demo::EventInfo&& payload) {
        LOG_INFO_FMT("Received ack for message: {}", payload.message());

        return ccf::grpc::make_success();
      };
      make_endpoint(
        "/demo.Demo/Ack",
        HTTP_POST,
        ccf::grpc_command_adapter<demo::EventInfo, google::protobuf::Empty>(
          ack),
        {ccf::no_auth_required})
        .install();

      auto pub = [this](
                   ccf::endpoints::CommandEndpointContext& ctx,
                   demo::EventInfo&& payload)
        -> ccf::grpc::GrpcAdapterResponse<google::protobuf::Empty> {
        std::unique_lock<ccf::pal::Mutex> guard(subscribed_events_lock);

        auto search = subscribed_events.find(payload.name());
        if (search != subscribed_events.end())
        {
          demo::SubResult result;
          *result.mutable_event_info() = std::move(payload);

          if (!search->second->stream_msg(result))
          {
            // Subscriber streams should be automatically cleaned up from
            // subscribed_events when underlying stream is closed so failure to
            // stream a message to an existing subscriber is considered an
            // error
            throw std::logic_error(fmt::format(
              "Error sending update to subscriber for event {}",
              payload.name()));
          }
        }
        else
        {
          return ccf::grpc::make_error(
            GRPC_STATUS_NOT_FOUND,
            fmt::format(
              "Updates for event {} has no subscriber", payload.name()));
        }

        return ccf::grpc::make_success();
      };

      make_endpoint(
        "/demo.Demo/Pub",
        HTTP_POST,
        ccf::grpc_command_adapter<demo::EventInfo, google::protobuf::Empty>(
          pub),
        ccf::no_auth_required)
        .set_forwarding_required(ccf::endpoints::ForwardingRequired::Never)
        .install();

      auto terminate = [this](
                         ccf::endpoints::CommandEndpointContext& ctx,
                         demo::Event&& payload) {
        std::unique_lock<ccf::pal::Mutex> guard(subscribed_events_lock);

        auto subscriber_it = subscribed_events.find(payload.name());
        if (subscriber_it != subscribed_events.end())
        {
          auto& response_stream = subscriber_it->second;

          demo::SubResult result;
          result.mutable_terminated();
          response_stream->stream_msg(result);
          LOG_INFO_FMT("Terminated subscriber for event {}", payload.name());

          subscribed_events.erase(subscriber_it);
        }

        return ccf::grpc::make_success();
      };
      make_endpoint(
        "/demo.Demo/Terminate",
        HTTP_POST,
        ccf::grpc_command_adapter<demo::Event, google::protobuf::Empty>(
          terminate),
        {ccf::no_auth_required})
        .install();
    }
  };
} // namespace grpcdemo

namespace ccfapp
{
  std::unique_ptr<ccf::endpoints::EndpointRegistry> make_user_endpoints(
    ccfapp::AbstractNodeContext& context)
  {
    return std::make_unique<grpcdemo::EndpointRegistry>(context);
  }
} // namespace ccfapp