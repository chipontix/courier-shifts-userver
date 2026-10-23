#!/usr/bin/env bash
# The standard measurement set (`make bench LABEL=...`), results go to results/<LABEL>/:
#   ladder-nocc  step load 4k..32k RPS, congestion control off: capacity, p50/p99, errors
#   ladder-cc    the same with congestion control on: how overload is shed
#   const-N      3 x 60s at 12k RPS: service CPU (cores) at a fixed load
#   perf         60s at 12k RPS with a 30s perf profile -> flamegraph
set -euo pipefail
cd "$(dirname "$0")"
LABEL=${LABEL:-$(git rev-parse --short HEAD)}
OUT=../results/$LABEL
mkdir -p "$OUT"

run() {
    name=$1; shift
    env "$@" RUN_NAME="$LABEL-$name" ./run.sh
    for ext in md json cpu; do cp "out/$LABEL-$name.$ext" "$OUT/$name.$ext"; done
}

ladder=(RPS_FROM=4000 RPS_TO=32000 RPS_STEP=4000 STEP_DURATION=30)
fixed=(RPS_FROM=12000 RPS_TO=12000 RPS_STEP=1000 STEP_DURATION=60)

run ladder-nocc CC_ENABLED=false "${ladder[@]}"
run ladder-cc CC_ENABLED=true "${ladder[@]}"
for i in 1 2 3; do run "const-$i" CC_ENABLED=false "${fixed[@]}"; done
run perf CC_ENABLED=false PERF=1 PERF_SECONDS=30 "${fixed[@]}"
cp "out/$LABEL-perf.svg" "$OUT/flamegraph.svg"

{
    echo "docker host: $(docker info --format '{{.OperatingSystem}}, {{.Architecture}}, {{.NCPU}} CPUs, kernel {{.KernelVersion}}')"
    for i in 1 2 3; do cat "$OUT/const-$i.cpu"; done
} > "$OUT/summary.txt"
cat "$OUT/summary.txt"
