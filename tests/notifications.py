# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.
import os
import getpass
import time
import logging
import json
import multiprocessing
import shutil
from random import seed
import infra.ccf
import infra.proc
import infra.jsonrpc
import infra.notification
import infra.net
import e2e_args

from loguru import logger as LOG


def parse_commit_notifications(current_commit, notification_queue):
    print("!!! Parsing !!!")
    print(notification_queue.queue)
    while not notification_queue.empty():
        e = notification_queue.get()
        j = json.loads(e)
        next_commit = j["commit"]
        assert (
            next_commit > current_commit
        ), f"Received notification of commit {next_commit}, expected this to be higher than current commit {current_commit}"
        current_commit = next_commit
    return current_commit


def run(args):
    hosts = ["localhost", "localhost"]

    notify_host = "localhost"
    port_a, port_b, port_c = infra.net.n_different(
        3, infra.net.probably_free_local_port, notify_host
    )
    receiver_address_a = f"{notify_host}:{port_a}"
    receiver_address_b = f"{notify_host}:{port_b}"
    receiver_address_c = f"{notify_host}:{port_c}"

    with infra.notification.notification_server(
        receiver_address_a
    ) as notifications_a, infra.notification.notification_server(
        receiver_address_b
    ) as notifications_b, infra.notification.notification_server(
        receiver_address_c
    ) as notifications_c:

        queue_a = notifications_a.get_queue()
        queue_b = notifications_b.get_queue()
        queue_c = notifications_c.get_queue()

        commit_a = 0
        commit_b = 0
        commit_c = 0

        def parse_all_commit_notifications():
            nonlocal commit_a, commit_b, commit_c
            commit_a = parse_commit_notifications(commit_a, queue_a)
            commit_b = parse_commit_notifications(commit_b, queue_b)
            commit_c = parse_commit_notifications(commit_c, queue_c)

        with infra.ccf.network(
            hosts, args.build_dir, args.debug_nodes, args.perf_nodes, pdb=args.pdb
        ) as network:
            primary, (backup,) = network.start_and_join(args)

            with primary.management_client() as mc:
                check_commit = infra.ccf.Checker(mc)

                LOG.debug("Register A for commit notifications")
                with primary.user_client(format="json") as c:
                    check_commit(
                        c.rpc(
                            "LOG_add_commit_receiver", {"address": receiver_address_a}
                        ),
                        result=True,
                    )

                msg_1 = "Hello world"
                LOG.debug("Write message 1")
                with primary.user_client(format="json") as c:
                    check_commit(
                        c.rpc("LOG_record", {"id": 42, "msg": msg_1}), result=True
                    )

                LOG.debug("Only A receives this commit notification")
                assert not queue_a.empty(), "A should have received commit notification"
                assert queue_b.empty(), "B should not have received commit notification"
                assert queue_c.empty(), "C should not have received commit notification"

                parse_all_commit_notifications()

                LOG.debug("Register B and C for commit notifications")
                with primary.user_client(format="json") as c:
                    check_commit(
                        c.rpc(
                            "LOG_add_commit_receiver", {"address": receiver_address_b}
                        ),
                        result=True,
                    )
                    check_commit(
                        c.rpc(
                            "LOG_add_commit_receiver", {"address": receiver_address_c}
                        ),
                        result=True,
                    )

                LOG.debug("Check notifications from these registrations")
                parse_all_commit_notifications()

                msg_2 = "Saluton mondo"
                LOG.debug("Write message 2")
                with primary.user_client(format="json") as c:
                    check_commit(
                        c.rpc("LOG_record", {"id": 43, "msg": msg_2}), result=True
                    )

                LOG.debug("All should receive a single notification for message 2")
                assert (
                    queue_a.qsize() == 1
                ), "A should have received a single notification"
                assert (
                    queue_b.qsize() == 1
                ), "B should have received a single notification"
                assert (
                    queue_c.qsize() == 1
                ), "C should have received a single notification"

                parse_all_commit_notifications()

                LOG.debug("Unregister B from commit notifications")
                with primary.user_client(format="json") as c:
                    check_commit(
                        c.rpc(
                            "LOG_remove_commit_receiver",
                            {"address": receiver_address_b},
                        ),
                        result=True,
                    )

                assert queue_b.empty(), "B should not receive further notifications"
                parse_all_commit_notifications()

                msg_3 = "Wassup"
                LOG.debug("Write message 3")
                with primary.user_client(format="json") as c:
                    check_commit(
                        c.rpc("LOG_record", {"id": 43, "msg": msg_3}), result=True
                    )

                assert not queue_a.empty(), "A should have received a notification"
                assert queue_b.empty(), "B should not have received a notification"
                assert not queue_c.empty(), "C should have received a notification"
                parse_all_commit_notifications()


if __name__ == "__main__":

    args = e2e_args.cli_args()
    args.package = "libloggingenc"
    run(args)
