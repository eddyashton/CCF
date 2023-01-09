# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.
import infra.network
import infra.e2e_args
import infra.interfaces
import suite.test_requirements as reqs

# pylint: disable=import-error
import demo_pb2 as Demo

# pylint: disable=import-error
import demo_pb2_grpc as DemoService

# pylint: disable=no-name-in-module
from google.protobuf.empty_pb2 import Empty as Empty

import queue
import grpc
import os
import random
import threading
import time

from loguru import logger as LOG


@reqs.description("Test gRPC streaming APIs")
def test_streaming(network, args):
    primary, _ = network.find_primary()

    # Create new anonymous credentials
    credentials = grpc.ssl_channel_credentials(
        open(os.path.join(network.common_dir, "service_cert.pem"), "rb").read()
    )

    def echo_op(s):
        return (Demo.OpIn(echo=Demo.EchoOp(body=s)), ("echoed", s))

    def reverse_op(s):
        return (
            Demo.OpIn(reverse=Demo.ReverseOp(body=s)),
            ("reversed", s[::-1]),
        )

    def truncate_op(s):
        start = random.randint(0, len(s))
        end = random.randint(start, len(s))
        return (
            Demo.OpIn(truncate=Demo.TruncateOp(body=s, start=start, end=end)),
            ("truncated", s[start:end]),
        )

    def empty_op(s):
        # oneof may always be null - generate some like this to make sure they're handled "correctly"
        return (Demo.OpIn(), None)

    def generate_ops(n):
        for _ in range(n):
            s = f"I'm random string {n}: {random.random()}"
            yield random.choice((echo_op, reverse_op, truncate_op, empty_op))(s)

    def compare_op_results(stub, n_ops):
        LOG.info(f"Sending streaming request containing {n_ops} operations")
        ops = []
        expected_results = []
        for op, expected_result in generate_ops(n_ops):
            ops.append(op)
            expected_results.append(expected_result)

        for actual_result in stub.RunOps(op for op in ops):
            assert len(expected_results) > 0, "More responses than requests"
            expected_result = expected_results.pop(0)
            if expected_result is None:
                assert not actual_result.HasField("result"), actual_result
            else:
                field_name, expected = expected_result
                actual = getattr(actual_result, field_name).body
                assert (
                    actual == expected
                ), f"Wrong {field_name} op: {actual} != {expected}"

        assert len(expected_results) == 0, "Fewer responses than requests"

    with grpc.secure_channel(
        target=primary.get_public_rpc_address(),
        credentials=credentials,
    ) as channel:
        stub = DemoService.DemoStub(channel)

        compare_op_results(stub, 0)
        compare_op_results(stub, 1)
        compare_op_results(stub, 20)
        compare_op_results(stub, 1000)

    return network


@reqs.description("Test server async gRPC streaming APIs")
def test_async_streaming(network, args):
    primary, _ = network.find_primary()

    credentials = grpc.ssl_channel_credentials(
        open(os.path.join(network.common_dir, "service_cert.pem"), "rb").read()
    )
    with grpc.secure_channel(
        target=f"{primary.get_public_rpc_host()}:{primary.get_public_rpc_port()}",
        credentials=credentials,
    ) as channel:
        s = DemoService.DemoStub(channel)

        event_name = "name_of_my_event"

        events = queue.Queue()
        subscription_started = threading.Event()

        def subscribe(event_name):
            credentials = grpc.ssl_channel_credentials(
                open(os.path.join(network.common_dir, "service_cert.pem"), "rb").read()
            )
            with grpc.secure_channel(
                target=f"{primary.get_public_rpc_host()}:{primary.get_public_rpc_port()}",
                credentials=credentials,
            ) as subscriber_channel:
                sub_stub = DemoService.DemoStub(subscriber_channel)
                LOG.debug(f"Waiting for event {event_name}...")
                for e in sub_stub.Sub(Demo.Event(name=event_name)):  # Blocking
                    if e.HasField("started"):

                        # While we're here, confirm that errors can be returned when calling a streaming RPC.
                        # In this case, from trying to subscribe multiple times
                        try:
                            for e in sub_stub.Sub(Demo.Event(name=event_name)):
                                assert False, "Expected this to be unreachable"
                        except grpc.RpcError as e:
                            # pylint: disable=no-member
                            assert e.code() == grpc.StatusCode.FAILED_PRECONDITION, e
                            assert (
                                f"Already have a subscriber for {event_name}"
                                in e.details()
                            ), e

                        subscription_started.set()
                    elif e.HasField("terminated"):
                        break
                    else:
                        LOG.info(f"Received update for event {event_name}")
                        events.put(("sub", e.event_info))
                        sub_stub.Ack(e.event_info)

        t = threading.Thread(target=subscribe, args=(event_name,))
        t.start()

        # Wait for subscription thread to actually start, and the server has confirmed it is ready
        assert subscription_started.wait(timeout=3), "Subscription wait timed out"

        event_count = 5
        event_contents = [f"contents {i}" for i in range(event_count)]
        LOG.info(f"Publishing events for {event_name}")

        for contents in event_contents:
            e = Demo.EventInfo(name=event_name, message=contents)
            LOG.info("Adding pub event")
            events.put(("pub", e))
            s.Pub(e)
            # Sleep to try and ensure that the sub happens next, rather than the next pub in this loop
            time.sleep(0.2)
        s.Terminate(Demo.Event(name=event_name))

        t.join()

        # Note: Subscriber stream is now closed but session is still open

        # Assert that all the published events were received by the subscriber,
        # and the pubs and subs were correctly interleaved
        sub_events_left = len(event_contents)
        expect_pub = True
        while events.qsize() > 0:
            kind, next_event = events.get()
            assert next_event.name == event_name
            assert next_event.message == event_contents[0]

            if expect_pub:
                assert kind == "pub"
            else:
                assert kind == "sub"
                event_contents.pop(0)
                sub_events_left -= 1
            expect_pub = not expect_pub
        assert sub_events_left == 0

        # Check that subscriber was automatically unregistered on server when subscriber
        # client stream was closed
        try:
            s.Pub(Demo.EventInfo(name=event_name, message="Hello"))
            assert False, "Publishing event without subscriber should return an error"
        except grpc.RpcError as e:
            # pylint: disable=no-member
            assert e.code() == grpc.StatusCode.NOT_FOUND, e
            # pylint: disable=no-member
            assert e.details() == f"Updates for event {event_name} has no subscriber"

    return network


def run(args):
    with infra.network.network(
        args.nodes,
        args.binary_dir,
        args.debug_nodes,
        args.perf_nodes,
    ) as network:
        network.start_and_open(args)

        primary, _ = network.find_primary()
        LOG.info("Check that endpoint supports HTTP/2")
        with primary.client() as c:
            r = c.get("/node/network/nodes").body.json()
            assert (
                r["nodes"][0]["rpc_interfaces"][infra.interfaces.PRIMARY_RPC_INTERFACE][
                    "app_protocol"
                ]
                == "HTTP2"
            ), "Target node does not support HTTP/2"

        network = test_streaming(network, args)
        network = test_async_streaming(network, args)


if __name__ == "__main__":
    args = infra.e2e_args.cli_args()

    args.package = "src/apps/grpc/libgrpc_demo"
    args.http2 = True  # gRPC interface
    args.nodes = infra.e2e_args.min_nodes(args, f=1)
    # Note: set following envvar for debug logs:
    # GRPC_VERBOSITY=DEBUG GRPC_TRACE=client_channel,http2_stream_state,http

    run(args)
