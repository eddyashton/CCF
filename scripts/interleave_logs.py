import argparse
import functools
import re
from colour import Color
from contextlib import ExitStack
from datetime import datetime

args = None


def compose(*fs):
    def compose2(f, g):
        def fn(*args, **kwargs):
            return f(g(*args, **kwargs))

        return fn

    return functools.reduce(compose2, fs)


def make_rescaler(from_min, from_max, to_min, to_max):
    from_range = from_max - from_min
    to_range = to_max - to_min

    def rescale(n):
        return to_min + (((n - from_min) / from_range) * to_range)

    return rescale


# Rescale saturation and luminance of all auto-selected colours, to (hopefully) end up with a palette that is readable on most terminals
saturation_rescaler = make_rescaler(0, 1, 0.3, 0.6)
luminance_rescaler = make_rescaler(0, 1, 0.2, 0.3)


def rescale_colour(c):
    c.saturation = saturation_rescaler(c.saturation)
    c.luminance = luminance_rescaler(c.luminance)
    return c


def add_background_colour(*args, **kwargs):
    c = rescale_colour(Color(*args, **kwargs))

    red, green, blue = (int(n * 255) for n in c.rgb)

    escape_sequence_regex = re.compile(r"\033\[(\d+(?:;\d+)*)m")

    def fn(s):
        if s is None:
            return None
        # To add background throughout this string while maintaining existing colours, we need to replace any existing colour
        # escape codes with a new sequence which _additionally_ sets the background colour (including the escape codes _ending_
        # a coloured segment)
        background_colour_sequence = f"48;2;{red};{green};{blue}"
        replaced_s = escape_sequence_regex.sub(
            f"\033[\\1;{background_colour_sequence}m", s
        )
        return f"\033[{background_colour_sequence}m{replaced_s}\033[0m"

    return fn


found_ids = {}


def find_id_in_log(obj):
    if not obj in found_ids:
        found_id = None
        try:
            # If obj looks like a file, try searching it for a line declaring its ID
            # Open a second copy by name, to avoid having to reset the original obj
            with open(obj.name) as fp:
                regex = re.compile(
                    r"Created new node n\[(.*)\]|Created join node n\[(.*)\]"
                )
                for line in fp:
                    match = regex.search(line)
                    if match:
                        found_id = match.group(1) or match.group(2)
                        break
        except Exception:
            pass
        found_ids[obj] = found_id
    return found_ids[obj]


assigned_indents = {}
assigned_indices = {}


def regex_rewriter(key, regex_s, format_string):
    regex = re.compile(regex_s)
    if not key in assigned_indices:
        assigned_indices[key] = len(assigned_indices)
    if not key in assigned_indents:
        assigned_indents[key] = "  " * len(assigned_indents)

    def fn(s):
        m = regex.match(s)
        if not m:
            return None
        format_dict = {
            "indent": assigned_indents[key],
            "index": assigned_indices[key],
            "original": s,
        }
        for group_name in regex.groupindex.keys():
            format_dict[group_name] = m.group(group_name) or ""
        return format_string.format(**format_dict)

    return fn


id_replacers = {}


def populate_id_replacers(fps):
    for i, fp in enumerate(fps):
        log_id = find_id_in_log(fp)
        if log_id:
            # Match trimmed versions of this ID too. Only first N characters are required, remainder are optional
            log_regex_s = log_id[:10] + "".join(f"{c}?" for c in log_id[10:])
            c = rescale_colour(Color(pick_for=log_id, pick_key=hash))
            # To make fg colours more readable, invert their luminance
            c.luminance = 1.0 - c.luminance
            red, green, blue = (int(n * 255) for n in c.rgb)
            id_replacers[
                re.compile(log_regex_s)
            ] = f"\033[38;2;{red};{green};{blue}m{i}={log_id[:4]}\033[0m"


def replace_ids(s):
    for id_regex, repl in id_replacers.items():
        s = id_regex.sub(repl, s)
    return s


assigned_decorators = {}


def decorated_line(line, key):
    if key not in assigned_decorators:
        log_id = find_id_in_log(key) or key
        decorator_fns = []
        if args.colour:
            decorator_fns.append(add_background_colour(pick_for=log_id, pick_key=str))
        decorator_fns.append(
            regex_rewriter(key, args.line_parsing_regex, args.output_format)
        )
        if args.replace_ids:
            decorator_fns.append(replace_ids)
        assigned_decorators[key] = compose(*decorator_fns)
    return assigned_decorators[key](line)


def next_timestamped_block_in_file(fp):
    lines = []
    nextline = fp.readline()
    prev_time = datetime.now()
    while nextline:
        try:
            # Assumes the timestamped lines start with an ISO timestamp, and then a space
            date_s, _ = nextline.split(" ", maxsplit=1)
            # datetime doesn't like Z as a UTC timezone specifier
            date_s = date_s.replace("Z", "+00:00")
            next_time = datetime.fromisoformat(date_s)
            if lines:
                yield prev_time, lines
                lines = []
            prev_time = next_time
        except ValueError:
            pass
        decorated = decorated_line(nextline.rstrip(), fp)
        if decorated is not None:
            lines.append(decorated)
        nextline = fp.readline()
    yield prev_time, lines


def next_block_from_files(fps):
    gens = list(next_timestamped_block_in_file(fp) for fp in fps)
    available = [(next(gen), gen) for gen in gens]
    available.sort()
    while available:
        this_block, gen = available.pop(0)
        yield this_block
        try:
            next_block = next(gen)
            if next_block:
                available.append((next_block, gen))
                available.sort()
        except StopIteration:
            pass


def print_interleaved_files():
    with ExitStack() as stack:
        files = [stack.enter_context(open(file)) for file in args.files]

        populate_id_replacers(files)

        for _, block in next_block_from_files(files):
            for line in block:
                print(line)


CCF_LOG_LINE_REGEX = r"(?P<prefix>(?P<datetime>.*Z)\s+(?P<timeoffset>\S+)?\s+(?P<thread_id>\d+)\s+\[(?P<level>\w+)\s?\]\s+(?P<filename>.*):(?P<linenumber>\d+)\s+\| )?(?P<content>.*$)"
REWRITTEN_LOG_LINE = "{datetime:27} |{index:02}| {indent}{content}"

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Interleave the output of several log files according to their timestamps, colouring each uniquely"
    )

    parser.add_argument("files", nargs="+", help="List of files to interleave")
    parser.add_argument(
        "--colour",
        action="store_true",
        help="Add unique background colours for each input log",
        default=False,
    )
    parser.add_argument(
        "--replace-ids",
        action="store_true",
        help="Replace all IDs identified in the logs with a briefer form",
        default=False,
    )
    parser.add_argument(
        "--line-parsing-regex",
        help="Regex used to parse incoming log lines, producing groups which can be substituted into output lines in --output-format. Default parses expected lines from CCF node logs",
        default=CCF_LOG_LINE_REGEX,
    )
    parser.add_argument(
        "--output-format",
        help="Format string used to rewrite each input line",
        default=REWRITTEN_LOG_LINE,
    )

    # Deliberately global, visible to functions above. Python!
    args = parser.parse_args()

    print_interleaved_files()
