# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.

import sys
import json
import rich
import argparse

LEADERSHIP_STATUS = {
    "None": ":beginner:",
    "Leader": ":crown:",
    "Follower": ":guard:",
    "Candidate": ":person_raising_hand:",
}

MEMBERSHIP_STATUS = {"Active": "A", "Retired": "R"}

RETIREMENT_PHASE = {"Ordered": "o", "Signed": "s", "Completed": "c", None: " "}

FUNCTIONS = {
    "add_configuration": "Cfg",
    "replicate": "Rpl",
    "send_append_entries": "SAe",
    "recv_append_entries": "RAe",
    "execute_append_entries_sync": "EAe",
    "send_append_entries_response": "SAeR",
    "recv_append_entries_response": "RAeR",
    "send_request_vote": "SRv",
    "recv_request_vote": "RRv",
    "recv_request_vote_response": "RRvR",
    "recv_propose_request_vote": "RPRv",
    "become_candidate": "BCan",
    "become_leader": "BLea",
    "become_follower": "BFol",
    "commit": "Cmt",
    "bootstrap": "Boot",
    None: "",
}

TAG = {"Y": ":white_check_mark:", "N": ":x:", " ": "  ", "S": ":pencil:"}


def digits(value):
    return len(str(value))


def diffed_key(old, new, key, suffix, size, sub=lambda x: x):
    if old is None or old[key] == new[key]:
        return f"{sub(new[key]):>{size}}{suffix}"
    color = "bright_white on red"
    return f"[{color}]{sub(new[key]):>{size}}{suffix}[/{color}]"


def diffed_opt_key(old, new, key, suffix, size, sub=lambda x: x):
    if old is None or old.get(key) == new.get(key):
        return f"{sub(new.get(key)):>{size}}{suffix}"
    color = "bright_white on red"
    return f"[{color}]{sub(new.get(key)):>{size}}{suffix}[/{color}]"


def render_state(state, func, old_state, tag, cfg):
    if state is None:
        return " "
    ls = LEADERSHIP_STATUS[state["leadership_state"]]
    ms = diffed_key(old_state, state, "membership_state", "", 1, MEMBERSHIP_STATUS.get)
    rp = diffed_opt_key(
        old_state, state, "retirement_phase", "", 1, RETIREMENT_PHASE.get
    )
    nid = state["node_id"]
    v = diffed_key(old_state, state, "current_view", "", cfg.view)
    i = diffed_key(old_state, state, "last_idx", "", cfg.index)
    c = diffed_key(old_state, state, "commit_idx", "", cfg.commit)
    f = FUNCTIONS[func]
    opc = "bold bright_white on red" if func else "normal"
    return (
        f"[{opc}]{nid:>{cfg.nodes}}{ls}[/{opc}] {ms}{rp} {v}.{i} {c}"
    )


def message_summary(packet):
    checks = {True: ":white_check_mark:", False: ":x:"}
    if packet["msg"] == "raft_append_entries":
        return f"AE  t{packet['term']} c{packet['leader_commit_idx']} ({packet['prev_term']}.{packet['prev_idx']}..{packet['term_of_idx']}.{packet['idx']}]"
    elif packet["msg"] == "raft_append_entries_response":
        return f"AER t{packet['term']} i{packet['last_log_idx']} {checks[packet['success'] == 'OK']}"
    elif packet["msg"] == "raft_request_vote":
        return f"RV  t{packet['term']} {packet['term_of_last_committable_idx']}.{packet['last_committable_idx']}"
    elif packet["msg"] == "raft_request_vote_response":
        return f"RVR t{packet['term']} {checks[packet['vote_granted']]}"


def action_summary(entry):
    msg = entry["msg"]
    if "packet" in msg:
        packet = msg["packet"]
        if "to_node_id" in msg:
            src = msg["state"]["node_id"]
            dst = msg["to_node_id"]
            return f"{src}->{dst} {message_summary(packet)}"
        elif "from_node_id" in msg:
            src = msg["from_node_id"]
            dst = msg["state"]["node_id"]
            return f"{dst}<-{src} {message_summary(packet)}"
    return FUNCTIONS[msg["function"]]


class DigitsCfg:
    nodes = 0
    view = 0
    index = 0
    commit = 0
    ts = 0

from collections import defaultdict

def table(args, lines):
    entries = [json.loads(line) for line in lines]
    nodes = []
    max_view = 0
    max_index = 0
    max_commit = 0
    max_ts = 0
    for entry in entries:
        node_id = entry["msg"]["state"]["node_id"]
        if node_id not in nodes:
            nodes.append(node_id)
        max_view = max(max_view, entry["msg"]["state"]["current_view"])
        max_index = max(max_index, entry["msg"]["state"]["last_idx"])
        max_commit = max(max_commit, entry["msg"]["state"]["commit_idx"])
        max_ts = max(max_ts, int(entry["h_ts"]))
    dcfg = DigitsCfg()
    dcfg.nodes = len(max(nodes, key=len))
    dcfg.view = digits(max_view)
    dcfg.index = digits(max_index)
    dcfg.commit = digits(max_commit)
    dcfg.ts = digits(max_ts)
    node_to_state = {nid: defaultdict(lambda: None) for nid in nodes}
    rows = []
    display_nodes = args.display_nodes or nodes
    for entry in entries:
        node_id = entry["msg"]["state"]["node_id"]
        old_state = node_to_state.get(node_id)
        node_to_state[node_id] = entry["msg"]["state"]
        tag = " "
        if "packet" in entry["msg"]:
            if "success" in entry["msg"]["packet"]:
                tag = "Y" if entry["msg"]["packet"]["success"] == "OK" else "N"
            if "vote_granted" in entry["msg"]["packet"]:
                tag = "Y" if entry["msg"]["packet"]["vote_granted"] else "N"
        if entry["msg"].get("globally_committable"):
            tag = "S"
        # Display commit index changes on the Cmt line itself
        if "args" in entry["msg"] and "commit_idx" in entry["msg"]["args"]:
            entry["msg"]["state"]["commit_idx"] = entry["msg"]["args"]["commit_idx"]

        # Filter entries to h_ts in [min_ts..max_ts]
        h_ts = int(entry["h_ts"])
        if args.min_ts is not None and args.min_ts > h_ts:
            continue
        if args.max_ts is not None and args.max_ts < h_ts:
            continue

        states = [
            (
                node_to_state.get(node),
                entry["msg"]["function"] if node == node_id else None,
                old_state if node == node_id else None,
                tag if node == node_id else " ",
            )
            for node in display_nodes
        ]
        rows.append(
            f"[{entry['h_ts']:>{dcfg.ts}}] "
            + "     ".join(render_state(*state, dcfg) if state[0] else " " * 12 for state in states)
            + "   "
            + action_summary(entry)
            + "   "
            + entry["cmd"]
        )
    return rows


def main():
    parser = argparse.ArgumentParser(
        description="Display summary of .ndjson trace from raft_driver",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    parser.add_argument("tracefile", type=str, help="Path to .ndjson trace file")
    parser.add_argument(
        "--min-ts", type=int, help="Do not display any entries before this ts"
    )
    parser.add_argument(
        "--max-ts", type=int, help="Do not display any entries after this ts"
    )
    parser.add_argument(
        "--display-nodes", nargs="+", help="Filter which nodes are displayed"
    )

    args = parser.parse_args()

    with open(args.tracefile) as tf:
        for line in table(args, tf.readlines()):
            rich.print(line)


if __name__ == "__main__":
    main()
