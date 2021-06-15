import argparse
import re
from contextlib import ExitStack
from datetime import datetime
from colour import Color, RGB_color_picker


def make_rescaler(from_min, from_max, to_min, to_max):
    from_range = from_max - from_min
    to_range = to_max - to_min

    def rescale(n):
        return to_min + (((n - from_min) / from_range) * to_range)

    return rescale


# Rescale saturation and luminance of all auto-selected colours, to (hopefully) end up with a palette visible on most terminals
saturation_rescaler = make_rescaler(0, 1, 0.3, 0.6)
luminance_rescaler = make_rescaler(0, 1, 0.2, 0.3)


def colour_highlight(*args, **kwargs):
    c = Color(*args, **kwargs)
    c.saturation = saturation_rescaler(c.saturation)
    c.luminance = luminance_rescaler(c.luminance)

    red, green, blue = (int(n * 255) for n in c.rgb)

    def fn(s):
        return f"\033[48;2;{red};{green};{blue}m{s}\033[0m"

    return fn


assigned_decorators = {}


def find_id_in_log(obj):
    id_for_colour = obj
    try:
        # If obj looks like a file, try searching it for a line declaring its ID
        # Open a second copy by name, to avoid having to reset the original obj
        with open(obj.name) as fp:
            print(f"Looking in {obj.name}")
            regex = re.compile(
                r"Created new node n\[(.*)\]|Created join node n\[(.*)\]"
            )
            for line in fp:
                match = regex.search(line)
                if match:
                    print(f"Found a match!")
                    id_for_colour = match.group(1) or match.group(2)
                    break
    except Exception:
        pass
    return RGB_color_picker(id_for_colour)


def decorated_line(line, key):
    if key not in assigned_decorators:
        assigned_decorators[key] = colour_highlight(pick_for=key, picker=find_id_in_log, pick_key=None)
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
        lines.append(decorated_line(nextline.rstrip(), fp))
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


def print_interleaved_files(args):
    with ExitStack() as stack:
        files = [stack.enter_context(open(file)) for file in args.files]

        for _, block in next_block_from_files(files):
            for line in block:
                print(line)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Interleave the output of several log files according to their timestamps, colouring each uniquely"
    )

    parser.add_argument("files", nargs="+", help="List of files to interleave")

    args = parser.parse_args()

    print_interleaved_files(args)
