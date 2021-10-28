# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.
import infra.network
import infra.proc
import infra.net
import suite.test_requirements as reqs
import infra.e2e_args
import tempfile
import os
import ccf.ledger
import shutil
from e2e_logging import get_all_entries
import glob
import time
import json

from loguru import logger as LOG


@reqs.description("Running Raft malicious cchost")
def test(network, args):
    primary, backups = network.find_nodes()

    log_id = 0

    # Partition one of the followers, so it's stuck in the past
    follower_partition_rules = network.partitioner.partition(
        [primary, *backups[:-1]], [backups[-1]]
    )

    # Just in case, write a bunch of entries each time to try to avoid the node's cached files
    n_entries = 10

    # Send some transactions to the primary, wait for them to commit
    common_entries = []
    with primary.client("user0") as c:
        for i in range(n_entries):
            r = c.post("/app/log/private", {"id": log_id, "msg": f"Common entries {i}"})
            common_entries.append((r.view, r.seqno))
        c.wait_for_commit(r)

    # Partition the primary from the remaining backups
    primary_partition_rules = network.partitioner.partition([primary], backups)

    # Submit some more transactions on the primary, creating a dead suffix
    dead_entries = []
    with primary.client("user0") as c:
        for i in range(n_entries):
            r = c.post("/app/log/private", {"id": log_id, "msg": f"Dead entries {i}"})
            dead_entries.append((r.view, r.seqno))

    # Copy this dead suffix elsewhere
    tmp_dir = tempfile.TemporaryDirectory(prefix="ccf")
    from_seqno = dead_entries[0][1]
    for dir in primary.remote.ledger_paths():
        for chunk_filename in os.listdir(dir):
            first, _ = ccf.ledger.Ledger._range_from_filename(chunk_filename)
            if first >= from_seqno:
                from_path = os.path.join(dir, chunk_filename)
                LOG.warning(
                    f"Copying chunk {chunk_filename} because it is after dead branch begins at {from_seqno}"
                )
                shutil.copy(from_path, tmp_dir.name)

    # Send some transactions to the remaining partition, so they create a new valid suffix
    new_primary, _ = network.wait_for_new_primary(primary, nodes=backups)
    new_entries = []
    with new_primary.client("user0") as c:
        for i in range(2 * n_entries):
            r = c.post("/app/log/private", {"id": log_id, "msg": f"New entries {i}"})
            new_entries.append((r.view, r.seqno))
        c.wait_for_commit(r)

    # Confirm that new primary can currently access its valid ledger
    with new_primary.client("user0") as c:
        entries, _ = get_all_entries(c, log_id)
        LOG.warning(f"Historical fetch returned {len(entries)} entries")

    # Partially replace these nodes' ledgers with the dead suffix
    for live_backup in backups[:-1]:
        for dir in live_backup.remote.ledger_paths():
            for dead_chunk_filename in os.listdir(tmp_dir.name):
                matches = glob.glob(f"{os.path.join(dir, dead_chunk_filename)}*")
                # Sometimes this fails, because the surviving ledger has started its term with a signature. I don't know why! Just retrying the whole test when this happens
                assert (
                    len(matches) == 1
                ), f"{len(matches)} matches for {dead_chunk_filename}: {matches}"
                # NB: As well as copying someone else's ledger here, we're renaming it as committed!
                # Big assumption that it contains the same transaction seqnos, that's what the snapshot+sig args are for
                from_path = os.path.join(tmp_dir.name, dead_chunk_filename)
                to_path = os.path.join(dir, matches[0])
                LOG.warning(
                    f"Replacing {to_path} with contents from {dead_chunk_filename}"
                )
                shutil.copy(from_path, to_path)

    # Confirm that new primary refuses to serve these historical entries from its current ledger
    with new_primary.client("user0") as c:
        fetched = False
        try:
            entries, _ = get_all_entries(c, log_id)
            fetched = True
        except Exception as e:
            LOG.error(e)
        assert not fetched

    # Confirm that progress can still be made on new primary
    with new_primary.client("user0") as c:
        for i in range(n_entries):
            r = c.post("/app/log/private", {"id": log_id, "msg": f"New entries {i}"})
            new_entries.append((r.view, r.seqno))
        c.wait_for_commit(r)

    # Reconnect the originally partitioned follower
    with backups[-1].client() as c:
        c.get("/node/commit")
    follower_partition_rules.drop()

    # Wait until they've agreed to start catching up
    with backups[-1].client() as c:
        while True:
            r = c.get("/node/network")
            body = r.body.json()
            view = body["current_view"]
            primary_id = body["primary_id"]
            if primary_id is None or primary_id == primary.node_id:
                LOG.warning(
                    f"Recovered backup still thinks they should talk to the old primary"
                )
                time.sleep(1)
            else:
                LOG.warning(
                    f"Recovered backup accepts a new primary in view {view}: {primary_id}"
                )
                break

    time.sleep(1)

    # See where this node is at, expect they have stalled
    with backups[-1].client() as c:
        for i in range(10):
            r = c.get("/node/commit")
            stalled_commit = r.body.json()["transaction_id"]

    # Compare with the rest of the network
    with new_primary.client() as c:
        r = c.get("/node/commit")
        network_commit = r.body.json()["transaction_id"]

    with primary.client() as c:
        r = c.get("/node/commit")
        dead_commit = r.body.json()["transaction_id"]

    LOG.error(
        f"Network has reached {network_commit}, but node given bad AppendEntries has stalled at {stalled_commit}"
    )
    LOG.error(
        f"Agreed on transactions from {common_entries[0]} to {common_entries[-1]}"
    )
    LOG.error(
        f"Produced dead branch from {dead_entries[0]} to {dead_entries[-1]}, and left a dead node at {dead_commit}"
    )
    LOG.error(f"Live branch continues from {new_entries[0]} to {new_entries[-1]}")

    primary_partition_rules.drop()

def run(args):
    with infra.network.network(
        args.nodes,
        args.binary_dir,
        args.debug_nodes,
        args.perf_nodes,
        pdb=args.pdb,
        init_partitioner=True,
    ) as network:
        network.start_and_join(args)

        test(network, args)


if __name__ == "__main__":
    args = infra.e2e_args.cli_args()
    args.package = "samples/apps/logging/liblogging"
    args.nodes = infra.e2e_args.nodes(args, 5)

    # Sign and chunk ASAP, after every transaction
    args.sig_tx_interval = 1
    args.ledger_chunk_bytes = 1

    # Avoid snapshots, in case they get in the way
    args.snapshot_tx_interval = 1000000
    run(args)
