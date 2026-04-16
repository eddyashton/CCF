#!/usr/bin/env python3
"""Stack multiple images vertically into a single PNG.

Usage:
    python3 stack_images.py -o combined.png img1.png img2.png img3.png

    # Stack all 3 runs of a plot type using shell glob:
    python3 stack_images.py -o fs_all_events.png experiments/*/fs_run{1,2,3}/trace_all_events.png
"""

import argparse
import sys

from PIL import Image


def stack_images(paths, output, gap=0):
    images = [Image.open(p) for p in paths]
    width = max(img.width for img in images)
    total_height = sum(img.height for img in images) + gap * (len(images) - 1)

    combined = Image.new("RGB", (width, total_height), (255, 255, 255))
    y = 0
    for img in images:
        combined.paste(img, (0, y))
        y += img.height + gap

    combined.save(output)
    print(f"Saved {output} ({width}x{total_height})")


def main():
    parser = argparse.ArgumentParser(description="Stack images vertically")
    parser.add_argument("images", nargs="+", help="Input images in order")
    parser.add_argument("-o", "--output", required=True, help="Output PNG path")
    parser.add_argument("--gap", type=int, default=0, help="Pixel gap between images")
    args = parser.parse_args()
    stack_images(args.images, args.output, args.gap)


if __name__ == "__main__":
    main()
