#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIAGRAM_DIR="$SCRIPT_DIR/presentation-diagrams"

# Render Mermaid diagrams to SVG
echo "==> Rendering Mermaid diagrams..."
for f in "$DIAGRAM_DIR"/*.mmd; do
    out="${f%.mmd}.svg"
    name="$(basename "$f")"
    if [[ "$f" -nt "$out" ]] || [[ ! -f "$out" ]]; then
        echo "    $name -> $(basename "$out")"
        npx mmdc -i "$f" -o "$out" -b transparent --scale 2
    else
        echo "    $name (up to date)"
    fi
done

# Build PPTX from slides
echo "==> Building presentation PPTX..."
npx @marp-team/marp-cli "$SCRIPT_DIR/presentation-slides.md" --pptx --allow-local-files

echo "==> Done: presentation-slides.pptx"
