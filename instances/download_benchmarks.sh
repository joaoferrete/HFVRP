#!/usr/bin/env bash
# Download a curated set of VRP benchmarks and convert them to the project's
# .vrp dialect.  The converted files are written to instances/benchmarks/.
#
# Usage:
#   instances/download_benchmarks.sh [PRIORITY_SEED]
#
# PRIORITY_SEED (default 42) controls the synthetic PRIORITY_SECTION added
# to every instance.  Running with different seeds produces comparable
# priority variants of the same benchmark.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH="$ROOT/instances/benchmarks"
CONVERT="$ROOT/instances/convert.py"
SEED="${1:-42}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

mkdir -p "$BENCH"

fetch() { # url outfile
    local url="$1" out="$2"
    if ! curl -fsSL --max-time 15 "$url" -o "$out"; then
        echo "  !! failed: $url" >&2
        return 1
    fi
}

convert() { # tmpfile target_name [extra-args...]
    local src="$1" name="$2"
    shift 2
    python3 "$CONVERT" "$src" --name "$name" --priority-seed "$SEED" \
            --out "$BENCH/$name.vrp" "$@"
}

# --- Augerat 1995 (CVRP, homogeneous) → converted with synthesised heterogeneous fleet
echo "== Augerat set A (CVRP, fleet synthesised) =="
AUGERAT_PRIMARY=(A-n32-k5 A-n33-k5 A-n37-k5)          # mirror 1 hosts these
AUGERAT_SECONDARY=(A-n45-k6 A-n54-k7 A-n60-k9 A-n80-k10)  # mirror 2 hosts these

for inst in "${AUGERAT_PRIMARY[@]}"; do
    echo "-- $inst"
    if fetch "https://raw.githubusercontent.com/prasadp4009/Vehicle-Routing-Problem-GPU-CUDA/master/$inst.vrp" \
             "$TMP/$inst.vrp"; then
        convert "$TMP/$inst.vrp" "augerat_${inst//-/_}" \
                --fleet synthesise --max-vehicles 8
    fi
done

for inst in "${AUGERAT_SECONDARY[@]}"; do
    echo "-- $inst"
    if fetch "https://raw.githubusercontent.com/giulianoxt/vehicle-routing-aco/master/augerat-a/$inst.vrp" \
             "$TMP/$inst.vrp"; then
        convert "$TMP/$inst.vrp" "augerat_${inst//-/_}" \
                --fleet synthesise --max-vehicles 8
    fi
done

# --- PyVRP HFVRP (already heterogeneous) — small subset of the X set
echo "== PyVRP HFVRP (native) =="
PYVRP=(X101-FSMFD X106-FSMD X110-HD X115-HVRP X120-FSMF)
for inst in "${PYVRP[@]}"; do
    echo "-- $inst"
    if fetch "https://raw.githubusercontent.com/PyVRP/Instances/main/HFVRP/$inst.vrp" \
             "$TMP/$inst.vrp"; then
        convert "$TMP/$inst.vrp" "pyvrp_${inst//-/_}" \
                --fleet as-is --max-vehicles 10
    fi
done

echo
echo "Done.  Instances in $BENCH"
ls "$BENCH"/*.vrp 2>/dev/null | wc -l | xargs -I{} echo "{} files written."
