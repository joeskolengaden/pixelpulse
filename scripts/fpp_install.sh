#!/bin/bash
set -e
PLUGINDIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PLUGINDIR"
FPPDIR="${FPPDIR:-/opt/fpp}"
echo "Building pixelpulse plugin (FPPDIR=$FPPDIR)..."
make clean FPPDIR="$FPPDIR" || true
make FPPDIR="$FPPDIR"
echo "pixelpulse build complete. Restart fppd to load it."
