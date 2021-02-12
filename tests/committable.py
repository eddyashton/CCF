# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.
import infra.e2e_args
import infra.network
import infra.proc
import time
import http
from ccf.tx_status import TxStatus

from loguru import logger as LOG


def wait_for_pending(client, view, seqno, timeout=3):
    end_time = time.time() + timeout
    while time.time() < end_time:
        r = client.get(f"/node/tx?view={view}&seqno={seqno}")
        assert (
            r.status_code == http.HTTPStatus.OK
        ), f"tx request returned HTTP status {r.status_code}"
        status = TxStatus(r.body.json()["status"])
        if status == TxStatus.Pending:
            return
        elif status == TxStatus.Invalid:
            raise RuntimeError(
                f"Transaction ID {view}.{seqno} is marked invalid and will never be committed"
            )
        elif status == TxStatus.Committed:
            raise RuntimeError(
                f"Transaction ID {view}.{seqno} is unexpectedly marked committed"
            )
        else:
            time.sleep(0.1)
    raise TimeoutError("Timed out waiting for commit")


def run(args):
    # This is deliberately 5, because the rest of the test depends on this
    # to grow a prefix and allow just enough nodes to resume to reach the
    # desired election result. Conversion to a general f isn't trivial.
    hosts = ["local://localhost"] * 5

    with infra.network.network(
        hosts, args.binary_dir, args.debug_nodes, args.perf_nodes, pdb=args.pdb
    ) as network:
        network.start_and_join(args)
        primary, backups = network.find_nodes()

        # Suspend three of the backups to prevent commit
        backups[1].suspend()
        backups[2].suspend()
        backups[3].stop()

        committable_txs = []
        # Run some transactions that can't be committed now
        with primary.client("user0") as uc:
            for i in range(3):
                committable_txs.append(
                    uc.post("/app/log/private", {"id": 100 + i, "msg": "Hello world"})
                )

        last_tx = committable_txs[-1]
        sig_view, sig_seqno = last_tx.view, last_tx.seqno + 1
        with backups[0].client() as bc:
            wait_for_pending(bc, sig_view, sig_seqno)

        # Suspend the final backup and run some transactions which only the primary hears, which should be discarded by the new primary
        backups[0].suspend()
        uncommittable_txs = []
        with primary.client("user0") as uc:
            for i in range(3):
                uncommittable_txs.append(
                    uc.post("/app/log/private", {"id": 100 + i, "msg": "Hello world"})
                )

        # Sleep long enough that this primary should be instantly replaced when nodes wake
        sleep_time = 2 * args.raft_election_timeout_ms / 1000
        LOG.info(f"Sleeping {sleep_time}s")
        time.sleep(sleep_time)

        # Suspend the primary, resume other backups
        primary.suspend()
        backups[0].resume()
        backups[1].resume()
        backups[2].resume()
        new_primary, new_term = network.wait_for_new_primary(
            primary.node_id, timeout_multiplier=6
        )
        assert new_primary == backups[0]
        LOG.debug(f"New primary is {new_primary.node_id} in term {new_term}")

        with new_primary.client("user0") as uc:
            # Check that uncommitted but committable suffix is preserved
            check_commit = infra.checker.Checker(uc)
            for tx in committable_txs:
                check_commit(tx)

            # Check that uncommittable suffix was lost
            for tx in uncommittable_txs:
                r = uc.get(f"/node/tx?view={tx.view}&seqno={tx.seqno}")
                assert (
                    r.status_code == http.HTTPStatus.OK
                ), f"tx request returned HTTP status {r.status_code}"
                status = TxStatus(r.body.json()["status"])
                assert (
                    status == TxStatus.Invalid
                ), f"Expected new primary to have invalidated {tx.view}.{tx.seqno}"

        # Check that new transactions can be committed
        with new_primary.client("user0") as uc:
            for i in range(3):
                r = uc.post("/app/log/private", {"id": 100 + i, "msg": "Hello world"})
                assert r.status_code == 200
                uc.wait_for_commit(r)

        # Resume original primary, check that they rejoin correctly, including new transactions
        primary.resume()
        network.wait_for_node_commit_sync()


if __name__ == "__main__":

    args = infra.e2e_args.cli_args()
    args.package = "liblogging"
    run(args)
