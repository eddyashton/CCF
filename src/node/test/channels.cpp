// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the Apache 2.0 License.
#include "../channel.h"
#include "crypto/verifier.h"
#include "ds/hex.h"
#include "node/entities.h"
#include "node/node_to_node_channel_manager.h"
#include "node/node_types.h"

#include <algorithm>
#include <cstring>
#include <queue>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

threading::ThreadMessaging threading::ThreadMessaging::thread_messaging;
std::atomic<uint16_t> threading::ThreadMessaging::thread_count = 0;

constexpr auto buffer_size = 1024 * 8;

auto in_buffer_1 = std::make_unique<ringbuffer::TestBuffer>(buffer_size);
auto out_buffer_1 = std::make_unique<ringbuffer::TestBuffer>(buffer_size);
ringbuffer::Circuit eio1(in_buffer_1->bd, out_buffer_1->bd);

auto in_buffer_2 = std::make_unique<ringbuffer::TestBuffer>(buffer_size);
auto out_buffer_2 = std::make_unique<ringbuffer::TestBuffer>(buffer_size);
ringbuffer::Circuit eio2(in_buffer_1->bd, out_buffer_2->bd);

auto wf1 = ringbuffer::WriterFactory(eio1);
auto wf2 = ringbuffer::WriterFactory(eio2);

using namespace ccf;

// Use fixed-size messages as channels messages are not length-prefixed since
// the type of the authenticated header is known in advance (e.g. AppendEntries)
static constexpr auto msg_size = 64;
using MsgType = std::array<uint8_t, msg_size>;

static NodeId nid1 = std::string("nid1");
static NodeId nid2 = std::string("nid2");

static constexpr auto default_curve = crypto::CurveID::SECP384R1;

template <typename T>
struct NodeOutboundMsg
{
  NodeId from;
  NodeId to;
  NodeMsgType type;
  T authenticated_hdr;
  std::vector<uint8_t> payload;

  std::vector<uint8_t> data() const
  {
    std::vector<uint8_t> r;
    r.insert(r.end(), authenticated_hdr.begin(), authenticated_hdr.end());
    r.insert(r.end(), payload.begin(), payload.end());
    return r;
  }

  std::vector<uint8_t> unauthenticated_data() const
  {
    auto r = data();
    auto type_hdr_bytes = std::vector<uint8_t>(r.begin(), r.begin() + 8);
    CBuffer type_hdr(type_hdr_bytes.data(), type_hdr_bytes.size());
    ChannelMsg channel_msg_type =
      serialized::read<ChannelMsg>(type_hdr.p, type_hdr.n);
    auto data = std::vector<uint8_t>(r.begin() + 8, r.end());
    return data;
  }
};

template <typename T>
auto read_outbound_msgs(ringbuffer::Circuit& circuit)
{
  std::vector<NodeOutboundMsg<T>> msgs;

  // A call to ringbuffer::Reader::read() may return 0 when there are still
  // messages to read, when it reaches the end of the buffer. The next call to
  // read() will correctly start at the beginning of the buffer and read these
  // messages. So to make sure we always get the messages we expect in this
  // test, read twice.
  for (size_t i = 0; i < 2; ++i)
  {
    circuit.read_from_inside().read(
      -1, [&](ringbuffer::Message m, const uint8_t* data, size_t size) {
        switch (m)
        {
          case node_outbound:
          {
            NodeId to = serialized::read<NodeId::Value>(data, size);
            auto msg_type = serialized::read<NodeMsgType>(data, size);
            NodeId from = serialized::read<NodeId::Value>(data, size);
            T aad;
            if (size > sizeof(T))
              aad = serialized::read<T>(data, size);
            auto payload = serialized::read(data, size, size);
            msgs.push_back(
              NodeOutboundMsg<T>{from, to, msg_type, aad, payload});
            break;
          }
          default:
          {
            LOG_INFO_FMT("Outbound message is not expected: {}", m);
            REQUIRE(false);
          }
        }
      });
  }

  return msgs;
}

auto read_node_msgs(ringbuffer::Circuit& circuit)
{
  std::vector<std::tuple<NodeId, std::string, std::string>> add_node_msgs;

  circuit.read_from_inside().read(
    -1, [&](ringbuffer::Message m, const uint8_t* data, size_t size) {
      switch (m)
      {
        case ccf::associate_node_address:
        {
          auto [id, hostname, service] =
            ringbuffer::read_message<ccf::associate_node_address>(data, size);
          add_node_msgs.push_back(std::make_tuple(id, hostname, service));

          break;
        }
        default:
        {
          LOG_INFO_FMT("Outbound message is not expected: {}", m);
          REQUIRE(false);
        }
      }
    });

  return add_node_msgs;
}

NodeOutboundMsg<MsgType> get_first(
  ringbuffer::Circuit& circuit, NodeMsgType msg_type)
{
  auto outbound_msgs = read_outbound_msgs<MsgType>(circuit);
  REQUIRE(outbound_msgs.size() >= 1);
  auto msg = outbound_msgs[0];
  const auto* data_ = msg.payload.data();
  auto size_ = msg.payload.size();
  REQUIRE(msg.type == msg_type);
  return msg;
}

TEST_CASE("Client/Server key exchange")
{
  auto network_kp = crypto::make_key_pair(default_curve);
  auto network_cert = network_kp->self_sign("CN=Network");

  auto channel1_kp = crypto::make_key_pair(default_curve);
  auto channel1_csr = channel1_kp->create_csr("CN=Node1");
  auto channel1_cert = network_kp->sign_csr(network_cert, channel1_csr, {});

  auto channel2_kp = crypto::make_key_pair(default_curve);
  auto channel2_csr = channel2_kp->create_csr("CN=Node2");
  auto channel2_cert = network_kp->sign_csr(network_cert, channel2_csr, {});

  auto v = crypto::make_verifier(channel1_cert);
  REQUIRE(v->verify_certificate({&network_cert}));
  v = crypto::make_verifier(channel2_cert);
  REQUIRE(v->verify_certificate({&network_cert}));

  REQUIRE(!make_verifier(channel2_cert)->is_self_signed());

  auto channels1 = NodeToNodeChannelManager(wf1);
  channels1.initialize(nid1, network_cert, channel1_kp, channel1_cert);
  auto channels2 = NodeToNodeChannelManager(wf2);
  channels2.initialize(nid2, network_cert, channel2_kp, channel2_cert);

  MsgType msg;
  msg.fill(0x42);

  INFO("Trying to tag/verify before channel establishment");
  {
    // Try sending on channel1 twice
    REQUIRE_FALSE(channels1.send_authenticated(
      nid2, NodeMsgType::consensus_msg, msg.begin(), msg.size()));
    REQUIRE_FALSE(channels1.send_authenticated(
      nid2, NodeMsgType::consensus_msg, msg.begin(), msg.size()));
  }

  std::vector<uint8_t> channel1_signed_key_share;

  INFO("Extract key share, signature, certificate from messages");
  {
    // Every send has been replaced with a new channel establishment message
    auto msgs = read_outbound_msgs<MsgType>(eio1);
    REQUIRE(msgs.size() == 2);
    REQUIRE(msgs[0].type == channel_msg);
    REQUIRE(msgs[1].type == channel_msg);
    REQUIRE(read_outbound_msgs<MsgType>(eio2).size() == 0);

#ifndef DETERMINISTIC_ECDSA
    // Signing twice should have produced different signatures
    REQUIRE(msgs[0].data() != msgs[1].data());
#endif

    channel1_signed_key_share = msgs[0].data();
  }

  INFO("Load peer key share and check signature");
  {
    REQUIRE(channels2.recv_message(nid1, std::move(channel1_signed_key_share)));
    REQUIRE(channels1.get_status(nid2) == INITIATED);
    REQUIRE(channels2.get_status(nid1) == WAITING_FOR_FINAL);
  }

  std::vector<uint8_t> channel2_signed_key_share;

  INFO("Extract responder signature over both key shares from messages");
  {
    // Messages sent before channel was established are flushed, so only 1 each.
    auto msgs = read_outbound_msgs<MsgType>(eio2);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].type == channel_msg);
    channel2_signed_key_share = msgs[0].data();
    REQUIRE(read_outbound_msgs<MsgType>(eio1).size() == 0);
  }

  INFO("Load responder key share and check signature");
  {
    REQUIRE(channels1.recv_message(nid2, std::move(channel2_signed_key_share)));
    REQUIRE(channels1.get_status(nid2) == ESTABLISHED);
    REQUIRE(channels2.get_status(nid1) == WAITING_FOR_FINAL);
  }

  std::vector<uint8_t> initiator_signature;
  NodeOutboundMsg<MsgType> queued_msg;

  INFO("Extract responder signature from message");
  {
    auto msgs = read_outbound_msgs<MsgType>(eio1);
    REQUIRE(msgs.size() == 2);
    REQUIRE(msgs[0].type == channel_msg);
    REQUIRE(msgs[1].type == consensus_msg);
    initiator_signature = msgs[0].data();

    auto md = msgs[1].data();
    REQUIRE(md.size() == msg.size() + sizeof(GcmHdr));
    REQUIRE(memcmp(md.data(), msg.data(), msg.size()) == 0);

    queued_msg = msgs[1]; // save for later
  }

  INFO("Cross-check responder signature and establish channels");
  {
    REQUIRE(channels2.recv_message(nid1, std::move(initiator_signature)));
    REQUIRE(channels1.get_status(nid2) == ESTABLISHED);
    REQUIRE(channels2.get_status(nid1) == ESTABLISHED);
  }

  INFO("Receive queued message");
  {
    // Receive the queued message to ensure the sequence numbers are contiguous.
    auto hdr = queued_msg.authenticated_hdr;
    auto payload = queued_msg.payload;
    const auto* data = payload.data();
    auto size = payload.size();
    REQUIRE(channels2.recv_authenticated(
      nid1, {hdr.begin(), hdr.size()}, data, size));
  }

  INFO("Protect integrity of message (peer1 -> peer2)");
  {
    REQUIRE(channels1.send_authenticated(
      nid2, NodeMsgType::consensus_msg, msg.begin(), msg.size()));
    auto outbound_msgs = read_outbound_msgs<MsgType>(eio1);
    REQUIRE(outbound_msgs.size() == 1);
    auto msg_ = outbound_msgs[0];
    const auto* data_ = msg_.payload.data();
    auto size_ = msg_.payload.size();
    REQUIRE(msg_.type == NodeMsgType::consensus_msg);

    REQUIRE(channels2.recv_authenticated(
      nid1,
      {msg_.authenticated_hdr.begin(), msg_.authenticated_hdr.size()},
      data_,
      size_));
  }

  INFO("Protect integrity of message (peer2 -> peer1)");
  {
    REQUIRE(channels2.send_authenticated(
      nid1, NodeMsgType::consensus_msg, msg.begin(), msg.size()));
    auto outbound_msgs = read_outbound_msgs<MsgType>(eio2);
    REQUIRE(outbound_msgs.size() == 1);
    auto msg_ = outbound_msgs[0];
    const auto* data_ = msg_.payload.data();
    auto size_ = msg_.payload.size();
    REQUIRE(msg_.type == NodeMsgType::consensus_msg);

    REQUIRE(channels1.recv_authenticated(
      nid2,
      {msg_.authenticated_hdr.begin(), msg_.authenticated_hdr.size()},
      data_,
      size_));
  }

  INFO("Tamper with message");
  {
    REQUIRE(channels1.send_authenticated(
      nid2, NodeMsgType::consensus_msg, msg.begin(), msg.size()));
    auto outbound_msgs = read_outbound_msgs<MsgType>(eio1);
    REQUIRE(outbound_msgs.size() == 1);
    auto msg_ = outbound_msgs[0];
    msg_.payload[0] += 1; // Tamper with message
    const auto* data_ = msg_.payload.data();
    auto size_ = msg_.payload.size();
    REQUIRE(msg_.type == NodeMsgType::consensus_msg);

    REQUIRE_FALSE(channels2.recv_authenticated(
      nid1,
      {msg_.authenticated_hdr.begin(), msg_.authenticated_hdr.size()},
      data_,
      size_));
  }

  INFO("Encrypt message (peer1 -> peer2)");
  {
    std::vector<uint8_t> plain_text(128, 0x1);
    REQUIRE(channels1.send_encrypted(
      nid2, NodeMsgType::consensus_msg, {msg.begin(), msg.size()}, plain_text));

    auto msg_ = get_first(eio1, NodeMsgType::consensus_msg);
    auto decrypted = channels2.recv_encrypted(
      nid1,
      {msg_.authenticated_hdr.data(), msg_.authenticated_hdr.size()},
      msg_.payload.data(),
      msg_.payload.size());

    REQUIRE(decrypted == plain_text);
  }

  INFO("Encrypt message (peer2 -> peer1)");
  {
    std::vector<uint8_t> plain_text(128, 0x2);
    REQUIRE(channels2.send_encrypted(
      nid1, NodeMsgType::consensus_msg, {msg.begin(), msg.size()}, plain_text));

    auto msg_ = get_first(eio2, NodeMsgType::consensus_msg);
    auto decrypted = channels1.recv_encrypted(
      nid2,
      {msg_.authenticated_hdr.data(), msg_.authenticated_hdr.size()},
      msg_.payload.data(),
      msg_.payload.size());

    REQUIRE(decrypted == plain_text);
  }
}

TEST_CASE("Replay and out-of-order")
{
  auto network_kp = crypto::make_key_pair(default_curve);
  auto network_cert = network_kp->self_sign("CN=Network");

  auto channel1_kp = crypto::make_key_pair(default_curve);
  auto channel1_csr = channel1_kp->create_csr("CN=Node1");
  auto channel1_cert = network_kp->sign_csr(network_cert, channel1_csr, {});

  auto channel2_kp = crypto::make_key_pair(default_curve);
  auto channel2_csr = channel2_kp->create_csr("CN=Node2");
  auto channel2_cert = network_kp->sign_csr(network_cert, channel2_csr, {});

  auto channels1 = NodeToNodeChannelManager(wf1);
  channels1.initialize(nid1, network_cert, channel1_kp, channel1_cert);
  auto channels2 = NodeToNodeChannelManager(wf2);
  channels2.initialize(nid2, network_cert, channel2_kp, channel2_cert);

  MsgType msg;
  msg.fill(0x42);

  INFO("Establish channels");
  {
    channels1.send_authenticated(
      nid2, NodeMsgType::consensus_msg, msg.data(), msg.size());

    auto msgs = read_outbound_msgs<MsgType>(eio1);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].type == channel_msg);
    auto channel1_signed_key_share = msgs[0].data();

    REQUIRE(channels2.recv_message(nid1, std::move(channel1_signed_key_share)));

    msgs = read_outbound_msgs<MsgType>(eio2);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].type == channel_msg);
    auto channel2_signed_key_share = msgs[0].data();
    REQUIRE(channels1.recv_message(nid2, std::move(channel2_signed_key_share)));

    msgs = read_outbound_msgs<MsgType>(eio1);
    REQUIRE(msgs.size() == 2);
    REQUIRE(msgs[0].type == channel_msg);
    auto initiator_signature = msgs[0].data();

    REQUIRE(channels2.recv_message(nid1, std::move(initiator_signature)));
    REQUIRE(channels1.get_status(nid2) == ESTABLISHED);
    REQUIRE(channels2.get_status(nid1) == ESTABLISHED);

    REQUIRE(msgs[1].type == consensus_msg);

    const auto* payload_data = msgs[1].payload.data();
    auto payload_size = msgs[1].payload.size();
    REQUIRE(channels2.recv_authenticated(
      nid1,
      {msgs[1].authenticated_hdr.data(), msgs[1].authenticated_hdr.size()},
      payload_data,
      payload_size));
  }

  NodeOutboundMsg<MsgType> first_msg, first_msg_copy;

  INFO("Replay same message");
  {
    REQUIRE(channels1.send_authenticated(
      nid2, NodeMsgType::consensus_msg, msg.begin(), msg.size()));
    auto outbound_msgs = read_outbound_msgs<MsgType>(eio1);
    REQUIRE(outbound_msgs.size() == 1);
    first_msg = outbound_msgs[0];
    REQUIRE(first_msg.from == nid1);
    REQUIRE(first_msg.to == nid2);
    auto msg_copy = first_msg;
    first_msg_copy = first_msg;
    const auto* data_ = first_msg.payload.data();
    auto size_ = first_msg.payload.size();
    REQUIRE(first_msg.type == NodeMsgType::consensus_msg);

    REQUIRE(channels2.recv_authenticated(
      nid1,
      {first_msg.authenticated_hdr.begin(), first_msg.authenticated_hdr.size()},
      data_,
      size_));

    // Replay
    data_ = msg_copy.payload.data();
    size_ = msg_copy.payload.size();
    REQUIRE_FALSE(channels2.recv_authenticated(
      nid1,
      {msg_copy.authenticated_hdr.begin(), msg_copy.authenticated_hdr.size()},
      data_,
      size_));
  }

  INFO("Issue more messages and replay old one");
  {
    REQUIRE(channels1.send_authenticated(
      nid2, NodeMsgType::consensus_msg, msg.begin(), msg.size()));
    REQUIRE(read_outbound_msgs<MsgType>(eio1).size() == 1);

    REQUIRE(channels1.send_authenticated(
      nid2, NodeMsgType::consensus_msg, msg.begin(), msg.size()));
    REQUIRE(read_outbound_msgs<MsgType>(eio1).size() == 1);

    REQUIRE(channels1.send_authenticated(
      nid2, NodeMsgType::consensus_msg, msg.begin(), msg.size()));
    auto outbound_msgs = read_outbound_msgs<MsgType>(eio1);
    REQUIRE(outbound_msgs.size() == 1);
    auto msg_ = outbound_msgs[0];
    const auto* data_ = msg_.payload.data();
    auto size_ = msg_.payload.size();
    REQUIRE(msg_.type == NodeMsgType::consensus_msg);

    REQUIRE(channels2.recv_authenticated(
      nid1,
      {msg_.authenticated_hdr.begin(), msg_.authenticated_hdr.size()},
      data_,
      size_));

    const auto* first_msg_data_ = first_msg_copy.payload.data();
    auto first_msg_size_ = first_msg_copy.payload.size();
    REQUIRE_FALSE(channels2.recv_authenticated(
      nid1,
      {first_msg_copy.authenticated_hdr.begin(),
       first_msg_copy.authenticated_hdr.size()},
      first_msg_data_,
      first_msg_size_));
  }

  INFO("Trigger new key exchange");
  {
    auto n = read_outbound_msgs<MsgType>(eio1).size() +
      read_outbound_msgs<MsgType>(eio2).size();
    REQUIRE(n == 0);

    channels1.close_channel(nid2);
    REQUIRE(channels1.get_status(nid2) == INACTIVE);
    REQUIRE(channels2.get_status(nid1) == ESTABLISHED);

    channels1.send_authenticated(
      nid2, NodeMsgType::consensus_msg, msg.data(), msg.size());
    REQUIRE(channels1.get_status(nid2) == INITIATED);
    REQUIRE(channels2.get_status(nid1) == ESTABLISHED);

    REQUIRE(channels2.recv_message(
      nid1, get_first(eio1, NodeMsgType::channel_msg).data()));
    REQUIRE(channels1.get_status(nid2) == INITIATED);
    REQUIRE(channels2.get_status(nid1) == WAITING_FOR_FINAL);

    REQUIRE(channels1.recv_message(
      nid2, get_first(eio2, NodeMsgType::channel_msg).data()));
    REQUIRE(channels1.get_status(nid2) == ESTABLISHED);
    REQUIRE(channels2.get_status(nid1) == WAITING_FOR_FINAL);

    auto messages_1to2 = read_outbound_msgs<MsgType>(eio1);
    REQUIRE(messages_1to2.size() == 2);
    REQUIRE(messages_1to2[0].type == NodeMsgType::channel_msg);
    REQUIRE(channels2.recv_message(nid1, messages_1to2[0].data()));
    REQUIRE(channels1.get_status(nid2) == ESTABLISHED);
    REQUIRE(channels2.get_status(nid1) == ESTABLISHED);

    REQUIRE(messages_1to2[1].type == NodeMsgType::consensus_msg);
    auto final_msg = messages_1to2[1];
    const auto* payload_data = final_msg.payload.data();
    auto payload_size = final_msg.payload.size();

    REQUIRE(channels2.recv_authenticated(
      nid1,
      {final_msg.authenticated_hdr.data(), final_msg.authenticated_hdr.size()},
      payload_data,
      payload_size));
  }
}

TEST_CASE("Host connections")
{
  auto network_kp = crypto::make_key_pair(default_curve);
  auto network_cert = network_kp->self_sign("CN=Network");

  auto channel_kp = crypto::make_key_pair(default_curve);
  auto channel_csr = channel_kp->create_csr("CN=Node");
  auto channel_cert = network_kp->sign_csr(network_cert, channel_csr, {});

  auto channel_manager = NodeToNodeChannelManager(wf1);
  channel_manager.initialize(nid1, network_cert, channel_kp, channel_cert);

  INFO("New node association is sent as ringbuffer message");
  {
    channel_manager.associate_node_address(nid2, "hostname", "port");
    auto add_node_msgs = read_node_msgs(eio1);
    REQUIRE(add_node_msgs.size() == 1);
    REQUIRE(std::get<0>(add_node_msgs[0]) == nid2);
    REQUIRE(std::get<1>(add_node_msgs[0]) == "hostname");
    REQUIRE(std::get<2>(add_node_msgs[0]) == "port");
  }

  INFO(
    "Trying to talk to node will initiate key exchange, regardless of IP "
    "association");
  {
    NodeId unknown_peer_id = std::string("unknown_peer");
    MsgType msg;
    msg.fill(0x42);
    channel_manager.send_authenticated(
      unknown_peer_id, NodeMsgType::consensus_msg, msg.data(), msg.size());
    auto outbound = read_outbound_msgs<MsgType>(eio1);
    REQUIRE(outbound.size() == 1);
    REQUIRE(outbound[0].type == channel_msg);
  }
}

TEST_CASE("Concurrent key exchange init")
{
  auto network_kp = crypto::make_key_pair(default_curve);
  auto network_cert = network_kp->self_sign("CN=Network");

  auto channel1_kp = crypto::make_key_pair(default_curve);
  auto channel1_csr = channel1_kp->create_csr("CN=Node1");
  auto channel1_cert = network_kp->sign_csr(network_cert, channel1_csr, {});

  auto channel2_kp = crypto::make_key_pair(default_curve);
  auto channel2_csr = channel2_kp->create_csr("CN=Node2");
  auto channel2_cert = network_kp->sign_csr(network_cert, channel2_csr, {});

  auto channels1 = NodeToNodeChannelManager(wf1);
  channels1.initialize(nid1, network_cert, channel1_kp, channel1_cert);
  auto channels2 = NodeToNodeChannelManager(wf2);
  channels2.initialize(nid2, network_cert, channel2_kp, channel2_cert);

  MsgType msg;
  msg.fill(0x42);

  INFO("Channel 2 wins");
  {
    channels1.send_authenticated(
      nid2, NodeMsgType::consensus_msg, msg.data(), msg.size());
    channels2.send_authenticated(
      nid1, NodeMsgType::consensus_msg, msg.data(), msg.size());

    REQUIRE(channels1.get_status(nid2) == INITIATED);
    REQUIRE(channels2.get_status(nid1) == INITIATED);

    auto fst1 = get_first(eio1, NodeMsgType::channel_msg);
    auto fst2 = get_first(eio2, NodeMsgType::channel_msg);

    REQUIRE(channels1.recv_message(nid2, fst2.data()));
    REQUIRE(channels2.recv_message(nid1, fst1.data()));

    REQUIRE(channels1.get_status(nid2) == WAITING_FOR_FINAL);
    REQUIRE(channels2.get_status(nid1) == INITIATED);

    fst1 = get_first(eio1, NodeMsgType::channel_msg);

    REQUIRE(channels2.recv_message(nid1, fst1.data()));

    fst2 = get_first(eio2, NodeMsgType::channel_msg);

    REQUIRE(channels1.recv_message(nid2, fst2.data()));

    REQUIRE(channels1.get_status(nid2) == ESTABLISHED);
    REQUIRE(channels2.get_status(nid1) == ESTABLISHED);
  }

  channels1.close_channel(nid2);
  channels2.close_channel(nid1);

  INFO("Channel 1 wins");
  {
    // Node 2 is higher priority, so will usually win. However if node 1 has
    // made many connection attempts it will have a higher connection attempt
    // nonce, so will win

    for (size_t i = 0; i < 3; ++i)
    {
      channels1.send_authenticated(
        nid2, NodeMsgType::consensus_msg, msg.data(), msg.size());
      get_first(eio1, NodeMsgType::channel_msg); // Discard message to simulate
                                                 // it being dropped
      channels1.close_channel(nid2);
    }

    channels1.send_authenticated(
      nid2, NodeMsgType::consensus_msg, msg.data(), msg.size());
    channels2.send_authenticated(
      nid1, NodeMsgType::consensus_msg, msg.data(), msg.size());

    REQUIRE(channels1.get_status(nid2) == INITIATED);
    REQUIRE(channels2.get_status(nid1) == INITIATED);

    auto fst1 = get_first(eio1, NodeMsgType::channel_msg);
    auto fst2 = get_first(eio2, NodeMsgType::channel_msg);

    REQUIRE_FALSE(channels1.recv_message(nid2, fst2.data()));
    REQUIRE(channels2.recv_message(nid1, fst1.data()));

    REQUIRE(channels1.get_status(nid2) == INITIATED);
    REQUIRE(channels2.get_status(nid1) == WAITING_FOR_FINAL);

    fst2 = get_first(eio2, NodeMsgType::channel_msg);

    REQUIRE(channels1.recv_message(nid2, fst2.data()));

    fst1 = get_first(eio1, NodeMsgType::channel_msg);

    REQUIRE(channels2.recv_message(nid1, fst1.data()));

    REQUIRE(channels1.get_status(nid2) == ESTABLISHED);
    REQUIRE(channels2.get_status(nid1) == ESTABLISHED);
  }
}

static std::vector<NodeOutboundMsg<MsgType>> get_all_msgs(
  std::set<ringbuffer::Circuit*> eios)
{
  std::vector<NodeOutboundMsg<MsgType>> res;
  for (auto& eio : eios)
  {
    auto msgs = read_outbound_msgs<MsgType>(*eio);
    res.insert(res.end(), msgs.begin(), msgs.end());
  }
  return res;
}

struct CurveChoices
{
  crypto::CurveID network;
  crypto::CurveID node_1;
  crypto::CurveID node_2;
};

TEST_CASE("Full NodeToNode test")
{
  constexpr auto all_256 = CurveChoices{crypto::CurveID::SECP256R1,
                                        crypto::CurveID::SECP256R1,
                                        crypto::CurveID::SECP256R1};
  constexpr auto all_384 = CurveChoices{crypto::CurveID::SECP384R1,
                                        crypto::CurveID::SECP384R1,
                                        crypto::CurveID::SECP384R1};
  // One node on a different curve
  constexpr auto mixed_0 = CurveChoices{crypto::CurveID::SECP256R1,
                                        crypto::CurveID::SECP256R1,
                                        crypto::CurveID::SECP384R1};
  // Both nodes on a different curve
  constexpr auto mixed_1 = CurveChoices{crypto::CurveID::SECP384R1,
                                        crypto::CurveID::SECP256R1,
                                        crypto::CurveID::SECP256R1};

  size_t i = 0;
  for (const auto& curves : {all_256, all_384, mixed_0, mixed_1})
  {
    LOG_DEBUG_FMT("Iteration: {}", i++);

    auto network_kp = crypto::make_key_pair(curves.network);
    auto network_cert = network_kp->self_sign("CN=Network");

    auto ni1 = std::string("N1");
    auto channel1_kp = crypto::make_key_pair(curves.node_1);
    auto channel1_csr = channel1_kp->create_csr("CN=Node1");
    auto channel1_cert = network_kp->sign_csr(network_cert, channel1_csr, {});

    auto ni2 = std::string("N2");
    auto channel2_kp = crypto::make_key_pair(curves.node_2);
    auto channel2_csr = channel2_kp->create_csr("CN=Node2");
    auto channel2_cert = network_kp->sign_csr(network_cert, channel2_csr, {});

    size_t message_limit = 32;

    MsgType msg;
    msg.fill(0x42);

    INFO("Set up channels");
    NodeToNodeChannelManager n2n1(wf1), n2n2(wf2);

    n2n1.initialize(ni1, network_cert, channel1_kp, channel1_cert);
    n2n1.set_message_limit(message_limit);
    n2n2.initialize(ni2, network_cert, channel2_kp, channel2_cert);
    n2n1.set_message_limit(message_limit);

    srand(0); // keep it deterministic

    INFO("Send/receive a number of messages");
    {
      size_t desired_rollovers = 5;
      size_t actual_rollovers = 0;

      for (size_t i = 0; i < message_limit * desired_rollovers; i++)
      {
        if (rand() % 2 == 0)
        {
          n2n1.send_authenticated(
            ni2, NodeMsgType::consensus_msg, msg.data(), msg.size());
        }
        else
        {
          n2n2.send_authenticated(
            ni1, NodeMsgType::consensus_msg, msg.data(), msg.size());
        }

        auto msgs = get_all_msgs({&eio1, &eio2});
        do
        {
          for (auto msg : msgs)
          {
            auto& n2n = (msg.from == ni2) ? n2n1 : n2n2;

            switch (msg.type)
            {
              case NodeMsgType::channel_msg:
              {
                n2n.recv_message(msg.from, msg.data());

                auto d = msg.data();
                const uint8_t* data = d.data();
                size_t sz = d.size();
                auto type = serialized::read<ChannelMsg>(data, sz);
                if (type == key_exchange_final)
                  actual_rollovers++;
                break;
              }
              case NodeMsgType::consensus_msg:
              {
                auto hdr = msg.authenticated_hdr;
                const auto* data = msg.payload.data();
                auto size = msg.payload.size();

                REQUIRE(n2n.recv_authenticated(
                  msg.from, {hdr.data(), hdr.size()}, data, size));
                break;
              }
              default:
                REQUIRE(false);
            }
          }

          msgs = get_all_msgs({&eio1, &eio2});
        } while (msgs.size() > 0);
      }

      REQUIRE(actual_rollovers >= desired_rollovers);
    }
  }
}

TEST_CASE("Interrupted key exchange")
{
  auto network_kp = crypto::make_key_pair(default_curve);
  auto network_cert = network_kp->self_sign("CN=Network");

  auto channel1_kp = crypto::make_key_pair(default_curve);
  auto channel1_csr = channel1_kp->create_csr("CN=Node1");
  auto channel1_cert = network_kp->sign_csr(network_cert, channel1_csr, {});

  auto channel2_kp = crypto::make_key_pair(default_curve);
  auto channel2_csr = channel2_kp->create_csr("CN=Node2");
  auto channel2_cert = network_kp->sign_csr(network_cert, channel2_csr, {});

  auto channels1 = NodeToNodeChannelManager(wf1);
  channels1.initialize(nid1, network_cert, channel1_kp, channel1_cert);
  auto channels2 = NodeToNodeChannelManager(wf2);
  channels2.initialize(nid2, network_cert, channel2_kp, channel2_cert);

  std::vector<uint8_t> msg;
  msg.push_back(0x1);
  msg.push_back(0x0);
  msg.push_back(0x10);
  msg.push_back(0x42);

  enum class DropStage
  {
    InitiationMessage,
    ResponseMessage,
    FinalMessage,
    NoDrops,
  };

  DropStage drop_stage;
  for (const auto drop_stage : {
         DropStage::NoDrops,
         DropStage::FinalMessage,
         DropStage::ResponseMessage,
         DropStage::InitiationMessage,
       })
  {
    INFO("Drop stage is ", (size_t)drop_stage);

    auto n = read_outbound_msgs<MsgType>(eio1).size() +
      read_outbound_msgs<MsgType>(eio2).size();
    REQUIRE(n == 0);

    channels1.close_channel(nid2);
    channels2.close_channel(nid1);
    REQUIRE(channels1.get_status(nid2) == INACTIVE);
    REQUIRE(channels2.get_status(nid1) == INACTIVE);

    channels1.send_authenticated(
      nid2, NodeMsgType::consensus_msg, msg.data(), msg.size());

    REQUIRE(channels1.get_status(nid2) == INITIATED);
    REQUIRE(channels2.get_status(nid1) == INACTIVE);

    auto initiator_key_share_msg = get_first(eio1, NodeMsgType::channel_msg);
    if (drop_stage > DropStage::InitiationMessage)
    {
      REQUIRE(channels2.recv_message(nid1, initiator_key_share_msg.data()));

      REQUIRE(channels1.get_status(nid2) == INITIATED);
      REQUIRE(channels2.get_status(nid1) == WAITING_FOR_FINAL);

      auto responder_key_share_msg = get_first(eio2, NodeMsgType::channel_msg);
      if (drop_stage > DropStage::ResponseMessage)
      {
        REQUIRE(channels1.recv_message(nid2, responder_key_share_msg.data()));

        REQUIRE(channels1.get_status(nid2) == ESTABLISHED);
        REQUIRE(channels2.get_status(nid1) == WAITING_FOR_FINAL);

        auto initiator_key_exchange_final_msg =
          get_first(eio1, NodeMsgType::channel_msg);
        if (drop_stage > DropStage::FinalMessage)
        {
          REQUIRE(channels2.recv_message(
            nid1, initiator_key_exchange_final_msg.data()));

          REQUIRE(channels1.get_status(nid2) == ESTABLISHED);
          REQUIRE(channels2.get_status(nid1) == ESTABLISHED);
        }
      }
    }

    INFO("Later attempts to connect should succeed");
    {
      SUBCASE("")
      {
        INFO("Node 1 attempts to connect");
        channels1.close_channel(nid2);
        channels1.send_authenticated(
          nid2, NodeMsgType::consensus_msg, msg.data(), msg.size());

        REQUIRE(channels2.recv_message(
          nid1, get_first(eio1, NodeMsgType::channel_msg).data()));
        REQUIRE(channels1.recv_message(
          nid2, get_first(eio2, NodeMsgType::channel_msg).data()));
        REQUIRE(channels2.recv_message(
          nid1, get_first(eio1, NodeMsgType::channel_msg).data()));
      }
      else
      {
        INFO("Node 2 attempts to connect");
        channels2.close_channel(nid1);
        channels2.send_authenticated(
          nid1, NodeMsgType::consensus_msg, msg.data(), msg.size());

        REQUIRE(channels1.recv_message(
          nid2, get_first(eio2, NodeMsgType::channel_msg).data()));
        REQUIRE(channels2.recv_message(
          nid1, get_first(eio1, NodeMsgType::channel_msg).data()));
        REQUIRE(channels1.recv_message(
          nid2, get_first(eio2, NodeMsgType::channel_msg).data()));
      }

      REQUIRE(channels1.get_status(nid2) == ESTABLISHED);
      REQUIRE(channels2.get_status(nid1) == ESTABLISHED);

      MsgType aad;
      aad.fill(0x10);

      REQUIRE(channels1.send_encrypted(
        nid2, NodeMsgType::consensus_msg, {aad.data(), aad.size()}, msg));
      auto msg1 = get_first(eio1, NodeMsgType::consensus_msg);
      auto decrypted1 = channels2.recv_encrypted(
        nid1,
        {msg1.authenticated_hdr.data(), msg1.authenticated_hdr.size()},
        msg1.payload.data(),
        msg1.payload.size());
      REQUIRE(decrypted1 == msg);

      REQUIRE(channels2.send_encrypted(
        nid1, NodeMsgType::consensus_msg, {aad.data(), aad.size()}, msg));
      auto msg2 = get_first(eio2, NodeMsgType::consensus_msg);
      auto decrypted2 = channels1.recv_encrypted(
        nid2,
        {msg2.authenticated_hdr.data(), msg2.authenticated_hdr.size()},
        msg2.payload.data(),
        msg2.payload.size());
      REQUIRE(decrypted2 == msg);
    }
  }
}