// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.

#include "host/signal.h"
#include "host/tcp.h"
#include "node/node_to_node_channel_manager.h"

#include <CLI11/CLI11.hpp>

size_t asynchost::TCPImpl::remaining_read_quota;

namespace fuzz
{
  class N2NImpl : public ccf::NodeToNode
  {
  public:
    virtual bool have_channel(const ccf::NodeId& nid)
    {
      throw std::logic_error("Not implemented");
    }

    virtual bool send_authenticated(
      const ccf::NodeId& to,
      ccf::NodeMsgType type,
      const uint8_t* data,
      size_t size)
    {
      throw std::logic_error("Not implemented");
    }

    virtual bool recv_authenticated_with_load(
      const ccf::NodeId& from, const uint8_t*& data, size_t& size)
    {
      throw std::logic_error("Not implemented");
    }

    virtual bool recv_authenticated(
      const ccf::NodeId& from,
      std::span<const uint8_t> header,
      const uint8_t*& data,
      size_t& size)
    {
      throw std::logic_error("Not implemented");
    }

    virtual bool recv_channel_message(
      const ccf::NodeId& from, const uint8_t* data, size_t size)
    {
      throw std::logic_error("Not implemented");
    }

    virtual bool send_encrypted(
      const ccf::NodeId& to,
      ccf::NodeMsgType type,
      std::span<const uint8_t> header,
      const std::vector<uint8_t>& data)
    {
      throw std::logic_error("Not implemented");
    }

    virtual std::vector<uint8_t> recv_encrypted(
      const ccf::NodeId& from,
      std::span<const uint8_t> header,
      const uint8_t* data,
      size_t size)
    {
      throw std::logic_error("Not implemented");
    }
  };

  namespace tcp
  {
    class PeerBehaviour : public asynchost::SocketBehaviour<asynchost::TCP>
    {
    public:
      PeerBehaviour() :
        asynchost::SocketBehaviour<asynchost::TCP>("Fuzz Peer", "TCP")
      {}

      virtual void on_read(size_t n, uint8_t*& d, sockaddr)
      {
        LOG_INFO_FMT("{} on_read([{}]): {}", conn_name, n, std::string((char const*)d, n));
      }
    };

    class ServerBehaviour : public asynchost::SocketBehaviour<asynchost::TCP>
    {
      size_t next_id = 0;
      std::map<size_t, asynchost::TCP> peers;

    public:
      ServerBehaviour() :
        asynchost::SocketBehaviour<asynchost::TCP>("Fuzz Server", "TCP")
      {}

      virtual void on_read(size_t n, uint8_t*&, sockaddr)
      {
        LOG_INFO_FMT("{} on_read({})", conn_name, n);
      }

      virtual void on_accept(asynchost::TCP& peer)
      {
        LOG_INFO_FMT("{} on_accept()", conn_name);
        const auto id = ++next_id;
        peer->set_behaviour(std::make_unique<PeerBehaviour>());
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
  tcp->set_behaviour(std::make_unique<fuzz::tcp::ServerBehaviour>());
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