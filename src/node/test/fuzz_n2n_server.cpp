// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "crypto/certs.h"
#include "host/signal.h"
#include "host/tcp.h"
#include "node/node_to_node_channel_manager.h"

#include <CLI11/CLI11.hpp>

size_t asynchost::TCPImpl::remaining_read_quota;
std::chrono::microseconds ccf::Channel::min_gap_between_initiation_attempts(
  2'000'000);

namespace fuzz
{
  class DirectChannel : public ccf::Channel
  {
  protected:
    void write_channel_message(const std::span<uint8_t>& payload) override
    {
      LOG_INFO_FMT("Want to write channel message");
    }

    void write_close_message() override
    {
      LOG_INFO_FMT("Want to write close message");
    }

    void write_message(
      ccf::NodeMsgType msg_type,
      const serializer::ByteRange (&payload)[3]) override
    {
      LOG_INFO_FMT("Want to write 'normal' message");
    }

  public:
    DirectChannel(
      const crypto::Pem& service_cert_,
      crypto::KeyPairPtr node_kp_,
      const crypto::Pem& node_cert_,
      const ccf::NodeId& self_,
      const ccf::NodeId& peer_id_,
      size_t message_limit_) :
      Channel(
        service_cert_, node_kp_, node_cert_, self_, peer_id_, message_limit_)
    {}
  };

  class N2NImpl : public ccf::AbstractNodeToNodeChannelManager
  {
  private:
    static constexpr size_t certificate_validity_period_days = 365;

    static crypto::Pem generate_self_signed_cert(
      const crypto::KeyPairPtr& kp, const std::string& name)
    {
      using namespace std::literals;
      auto valid_from =
        ds::to_x509_time_string(std::chrono::system_clock::now() - 24h);

      return crypto::create_self_signed_cert(
        kp, name, {}, valid_from, certificate_validity_period_days);
    }

    static crypto::Pem endorse_cert(
      const crypto::KeyPairPtr& endorser,
      const crypto::Pem& endorser_cert,
      const crypto::KeyPairPtr& endorsee)
    {
      using namespace std::literals;
      auto valid_from =
        ds::to_x509_time_string(std::chrono::system_clock::now() - 24h);

      auto csr = endorsee->create_csr("CN=Fuzz node");
      return crypto::create_endorsed_cert(
        csr,
        valid_from,
        certificate_validity_period_days,
        endorser->private_key_pem(),
        endorser_cert);
    }

  public:
    N2NImpl()
    {
      ccf::NodeId self_node_id;
      auto service_kp = crypto::make_key_pair();
      auto service_cert = generate_self_signed_cert(service_kp, "CN=Fuzz service");

      auto node_kp = crypto::make_key_pair();
      auto endorsed_node_cert = endorse_cert(service_kp, service_cert, node_kp);

      initialize(self_node_id, service_cert, node_kp, endorsed_node_cert);
      set_message_limit(50);
    }

    virtual void associate_node_address(
      const ccf::NodeId& peer_id,
      const std::string& peer_hostname,
      const std::string& peer_service) override
    {
      throw std::logic_error("Not implemented: associate_node_address");
    }

    std::shared_ptr<ccf::Channel> make_channel(
      const ccf::NodeId& peer_id) override
    {
      CCF_ASSERT_FMT(
        this_node == nullptr || this_node->node_id != peer_id,
        "Requested channel with self {}",
        peer_id);

      CCF_ASSERT_FMT(
        message_limit.has_value(),
        "Node-to-node message limit has not yet been set");

      std::lock_guard<ccf::pal::Mutex> guard(lock);
      CCF_ASSERT_FMT(
        this_node != nullptr && this_node->endorsed_node_cert.has_value(),
        "Endorsed node certificate has not yet been set");

      return std::make_shared<DirectChannel>(
        this_node->service_cert,
        this_node->node_kp,
        this_node->endorsed_node_cert.value_or(crypto::Pem{}),
        this_node->node_id,
        peer_id,
        message_limit.value());
    }
  };

  namespace tcp
  {
    using ConnID = size_t;

    class PeerBehaviour : public asynchost::SocketBehaviour<asynchost::TCP>
    {
    private:
      fuzz::N2NImpl& n2n;
      ConnID id;

      std::optional<size_t> msg_size = std::nullopt;
      std::vector<uint8_t> pending;

    public:
      PeerBehaviour(fuzz::N2NImpl& n2n_, ConnID id_) :
        asynchost::SocketBehaviour<asynchost::TCP>("Fuzz Peer", "TCP"),
        n2n(n2n_),
        id(id_)
      {}

      virtual void on_read(size_t len, uint8_t*& incoming, sockaddr)
      {
        LOG_INFO_FMT("[{}] on_read([{} bytes])", id, len);

        pending.insert(pending.end(), incoming, incoming + len);

        const uint8_t* data = pending.data();
        size_t size = pending.size();
        const auto size_before = size;

        while (true)
        {
          if (!msg_size.has_value())
          {
            if (size < sizeof(uint32_t))
            {
              break;
            }

            msg_size = serialized::read<uint32_t>(data, size);
          }

          if (size < msg_size.value())
          {
            LOG_DEBUG_FMT("[{}] Have {}/{} bytes", id, size, msg_size.value());
            break;
          }

          const auto size_pre_headers = size;
          auto msg_type = serialized::read<ccf::NodeMsgType>(data, size);
          ccf::NodeId from = serialized::read<ccf::NodeId::Value>(data, size);
          const auto size_post_headers = size;
          const size_t payload_size =
            msg_size.value() - (size_pre_headers - size_post_headers);

          LOG_DEBUG_FMT(
            "Receiving message: from node {}, size {}, type {}",
            from,
            msg_size.value(),
            msg_type);

          switch (msg_type)
          {
            case ccf::NodeMsgType::channel_msg:
            {
              n2n.recv_channel_message(from, data, payload_size);
              break;
            }

            case ccf::NodeMsgType::consensus_msg:
            {
              LOG_INFO_FMT(
                "Processing (ignoring) consensus message of {} bytes",
                payload_size);
              break;
            }

            default:
            {
              LOG_FAIL_FMT("Unhandled node message type: {}", msg_type);
              break;
            }
          }

          data += payload_size;
          size -= payload_size;
          msg_size.reset();
        }

        const auto size_after = size;
        const auto used = size_before - size_after;
        if (used > 0)
        {
          pending.erase(pending.begin(), pending.begin() + used);
        }
      }
    };

    class ServerBehaviour : public asynchost::SocketBehaviour<asynchost::TCP>
    {
      fuzz::N2NImpl& n2n;

      ConnID next_id = 0;
      std::map<size_t, asynchost::TCP> peers;

    public:
      ServerBehaviour(fuzz::N2NImpl& n2n_) :
        asynchost::SocketBehaviour<asynchost::TCP>("Fuzz Server", "TCP"),
        n2n(n2n_)
      {}

      virtual void on_read(size_t n, uint8_t*&, sockaddr)
      {
        LOG_INFO_FMT("{} on_read({})", conn_name, n);
      }

      virtual void on_accept(asynchost::TCP& peer)
      {
        LOG_INFO_FMT("{} on_accept()", conn_name);
        const auto id = ++next_id;
        peer->set_behaviour(std::make_unique<PeerBehaviour>(n2n, id));
        peers[id] = peer;
        LOG_INFO_FMT("Saving peer as {}", id);
      }
    };
  }

  class EndLoopImpl
  {
  public:
    void on_signal(int signal)
    {
      uv_stop(uv_default_loop());
    }
  };

  template <size_t NSig>
  using EndLoopOnSig =
    asynchost::proxy_ptr<asynchost::Signal<NSig, EndLoopImpl>>;
}

int main(int argc, char** argv)
{
  logger::config::default_init();

  // Parse CLI arguments
  CLI::App app{"Runs a simple server hosting the CCF node-to-node protocol"};

  std::string port;
  app.add_option("-p,--port", port, "Network port to listen on")->required();

  try
  {
    app.parse(argc, argv);
  }
  catch (const CLI::ParseError& e)
  {
    return app.exit(e);
  }

  // TODO
  auto node_manager = fuzz::N2NImpl();

  // Listen for incoming TCP traffic
  asynchost::TCP tcp;
  tcp->set_behaviour(
    std::make_unique<fuzz::tcp::ServerBehaviour>(node_manager));
  tcp->listen("localhost", port);

  // Terminate loop politely
  fuzz::EndLoopOnSig<SIGTERM> sigterm;
  fuzz::EndLoopOnSig<SIGINT> sigint;

  // Reset the inbound-TCP processing quota each iteration
  asynchost::ResetTCPReadQuota reset_tcp_quota;

  // Run main uv loop
  LOG_INFO_FMT("Entering event loop");
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  LOG_INFO_FMT("Exited event loop");

  return 0;
}