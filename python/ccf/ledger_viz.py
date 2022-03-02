# Copyright (c) Microsoft Corporation. All rights reserved.
# Licensed under the Apache 2.0 License.

import ccf.ledger
import argparse
import os
from stringcolor import cs  # type: ignore
import json


class Liner:
    _line = ""
    _annotations = []
    _len = 0
    MAX_LENGTH = os.get_terminal_size().columns

    def _merge_label_lines(self):
        label_lines = []
        for n, label in self._annotations:
            start_of_label = n - len(label) // 2
            for i, label_line in reversed(list(enumerate(label_lines))):
                if len(label_line) < start_of_label:
                    padding = " " * (start_of_label - len(label_line))
                    label_lines[i] = label_line + padding + label
                    break
            else:
                label_lines.insert(0, " " * start_of_label + label)
        return label_lines

    def _print_annotations(self):
        if len(self._annotations) > 0:
            caret_line = [" " for i in range(self.MAX_LENGTH)]
            for n, _ in self._annotations:
                caret_line[n] = "v"
            label_lines = self._merge_label_lines()
            for label_line in label_lines:
                print(label_line)
            print("".join(caret_line))

    def flush(self):
        if self._len > 0:
            self._print_annotations()
            print(self._line)
        self._line = ""
        self._annotations = []
        self._len = 0

    def append(self, s: str, colour: str, background_colour: str = None):
        self._line += cs(s, colour, background_colour)
        self._len += len(s)
        if self._len >= self.MAX_LENGTH:
            self.flush()

    def append_annotation(self, label):
        self._annotations.append((self._len, label))


class DefaultLiner(Liner):
    _bg_colour_mapping = {
        "New Service": "White",
        "Service Open": "Magenta",
        "Governance": "Red",
        "Signature": "Green",
        "Internal": "Orange",
        "User Public": "Blue",
        "User Private": "DarkBlue",
    }
    _last_view = None
    _fg_colour = "Black"
    _current_service_identity = None

    @staticmethod
    def view_to_char(view):
        return str(view)[-1]

    def __init__(self, write_views, split_views, annotate_txid_categories):
        self.write_views = write_views
        self.split_views = split_views
        self.annotate_txid_categories = annotate_txid_categories

    def entry(self, category, view, seqno):
        view_change = view != self._last_view
        self._last_view = view

        if view_change and self.split_views:
            self.flush()
            self.append(f"{view}: ", "White")

        char = " "
        if self.write_views:
            char = "‾" if not view_change else self.view_to_char(view)

        if category in self.annotate_txid_categories:
            self.append_annotation(f"{view}.{seqno}")

        fg_colour = self._fg_colour
        bg_colour = self._bg_colour_mapping[category]
        self.append(char, fg_colour, bg_colour)

    def append_tx(self, tx):
        public = tx.get_public_domain().get_tables()
        has_private = tx.get_private_domain_size()

        view = tx.gcm_header.view
        seqno = tx.gcm_header.seqno
        if not has_private:
            if ccf.ledger.SIGNATURE_TX_TABLE_NAME in public:
                self.entry("Signature", view, seqno)
            else:
                if all(table.startswith("public:ccf.internal.") for table in public):
                    self.entry("Internal", view, seqno)
                elif any(table.startswith("public:ccf.gov.") for table in public):
                    service_info = try_get_service_info(public)
                    if service_info is None:
                        self.entry("Governance", view, seqno)
                    elif service_info["status"] == "Opening":
                        self.entry("New Service", view, seqno)
                        self._current_service_identity = service_info["cert"]
                    elif (
                        service_info["cert"] == self._current_service_identity
                        and service_info["status"] == "Open"
                    ):
                        self.entry("Service Open", view, seqno)
                else:
                    self.entry("User Public", view, seqno)
        else:
            self.entry("User Private", view, seqno)

    def help(self):
        print(
            " | ".join(
                [
                    f"{category} {cs(' ', 'White', bg_colour)}"
                    for category, bg_colour in self._bg_colour_mapping.items()
                ]
            )
        )
        if self.write_views:
            print(
                " ".join(
                    [
                        f"Start of view 14: {cs(self.view_to_char(14), self._fg_colour, 'Grey')}"
                    ]
                )
            )
        print()


def try_get_service_info(public_tables):
    return (
        json.loads(
            public_tables[ccf.ledger.SERVICE_INFO_TABLE_NAME][
                ccf.ledger.WELL_KNOWN_SINGLETON_TABLE_KEY
            ]
        )
        if ccf.ledger.SERVICE_INFO_TABLE_NAME in public_tables
        else None
    )


def main():
    parser = argparse.ArgumentParser(
        description="Visualise content of CCF ledger",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "paths",
        help="Path to ledger directories or ledger chunks. "
        "Note that parsing individual ledger chunks requires the additional --insecure-skip-verification option",
        nargs="+",
    )
    parser.add_argument(
        "--uncommitted", help="Also parse uncommitted ledger files", action="store_true"
    )
    parser.add_argument(
        "--write-views",
        help="Include characters on each tile indicating their view",
        action="store_true",
    )
    parser.add_argument(
        "--split-views",
        help="Write each view on a new line, prefixed by the view number",
        action="store_true",
    )
    parser.add_argument(
        "--split-chunks",
        help="Write each chunk as a new section, prefixed by the chunk file name",
        action="store_true",
    )
    parser.add_argument(
        "--annotate-txid",
        help="Add TxID annotations to a category of transactions",
        action="append",
        default=[],
    )
    parser.add_argument(
        "--insecure-skip-verification",
        help="INSECURE: skip ledger Merkle tree integrity verification",
        action="store_true",
        default=False,
    )
    parser.add_argument(
        "--verbose",
        help="Equivalent to '--write-views --split-views --split-chunks --annotate-txid Signature'",
        action="store_true",
    )
    args = parser.parse_args()

    if args.verbose:
        args.write_views = True
        args.split_views = True
        args.split_chunks = True

    ledger_paths = args.paths
    ledger = ccf.ledger.Ledger(
        ledger_paths,
        committed_only=not args.uncommitted,
        insecure_skip_verification=args.insecure_skip_verification,
    )

    l = DefaultLiner(args.write_views, args.split_views, args.annotate_txid)
    l.help()
    for chunk in ledger:
        if args.split_chunks:
            l.flush()
            l.append(f"{chunk.filename()}:", "White")
            l.flush()

        for tx in chunk:
            l.append_tx(tx)

    l.flush()


if __name__ == "__main__":
    main()
