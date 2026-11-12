#!/usr/bin/env bash
# Links this project's simulation sources into an ns-3 tree's scratch/ folder
# so `./ns3 build` compiles them. Usage:
#   scripts/link-into-ns3.sh /path/to/ns-3.49
set -euo pipefail
NS3_DIR="${1:?usage: $0 /path/to/ns-3.49}"
PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

if [[ ! -x "$NS3_DIR/ns3" ]]; then
    echo "error: $NS3_DIR does not look like an ns-3 directory (no ./ns3 script)" >&2
    exit 1
fi

ln -sfn "$PROJECT_DIR/simulation/minimal-two-node.cc" "$NS3_DIR/scratch/minimal-two-node.cc"
ln -sfn "$PROJECT_DIR/simulation/office-network" "$NS3_DIR/scratch/small-office"
# Remember where ns-3 lives so the Python scripts can find it.
echo "$NS3_DIR" > "$PROJECT_DIR/.ns3-path"
echo "Linked simulation sources into $NS3_DIR/scratch"
