#!/usr/bin/env bash
# Load test in one command (from the repo root: `make bench`).
#
# Builds the release service, starts PostgreSQL + Redis + the service with
# pinned CPUs (service: 2 vCPUs), seeds data, runs a Pandora step profile and
# writes loadtest/out/<RUN_NAME>.{phout,md,json,cpu}. With PERF=1 it also
# records a perf profile of the service for PERF_SECONDS in the middle of the
# run and renders loadtest/out/<RUN_NAME>.svg.
#
# Knobs: RUN_NAME RPS_FROM RPS_TO RPS_STEP STEP_DURATION INSTANCES CC_ENABLED PERF PERF_SECONDS
set -euo pipefail
cd "$(dirname "$0")"

export RUN_NAME=${RUN_NAME:-ladder}
export RPS_FROM=${RPS_FROM:-1000}
export RPS_TO=${RPS_TO:-10000}
export RPS_STEP=${RPS_STEP:-1000}
export STEP_DURATION=${STEP_DURATION:-30}
export INSTANCES=${INSTANCES:-2000}
export CC_ENABLED=${CC_ENABLED:-true}
PERF=${PERF:-0}
PERF_SECONDS=${PERF_SECONDS:-30}
AMMO_COUNT=${AMMO_COUNT:-1000000}
FLAMEGRAPH_COMMIT=41fee1f99f9276008b7cd112fca19dc3ea84ac32

mkdir -p out
if [ ! -s out/ammo.jsonl ]; then
    python3 gen_ammo.py "$AMMO_COUNT" > out/ammo.jsonl
fi

make -C .. image build PRESET=release DOCKER_TTY=

# Fresh database every run: PostgreSQL data lives in tmpfs.
docker compose down -v --remove-orphans >/dev/null 2>&1 || true
docker compose up -d --wait postgres redis
docker compose exec -T postgres psql -q -v ON_ERROR_STOP=1 -U courier -d courier_shifts_db < seed.sql
docker compose up -d --wait service
docker compose --profile tools build -q pandora

cpu_usec() { docker compose exec -T service awk '/^usage_usec/ {print $2}' /sys/fs/cgroup/cpu.stat; }

steps=$(( (RPS_TO - RPS_FROM) / RPS_STEP + 1 ))
total_seconds=$(( steps * STEP_DURATION ))

if [ "$PERF" = 1 ]; then
    perf_delay=$(( total_seconds / 2 - PERF_SECONDS / 2 ))
    service_container=$(docker compose ps -q service)
    (
        sleep "$perf_delay"
        docker run --rm --privileged --pid="container:$service_container" \
            -v courier-build:/src/build:ro -v "$PWD/out:/out" courier-dev \
            perf record -F 499 -g -p 1 -o "/out/$RUN_NAME.perf.data" -- sleep "$PERF_SECONDS" 2>/dev/null
    ) &
    perf_job=$!
fi

# Pandora does not expand ${env:...} inside the schedule, so render the config here.
sed -e "s/\${env:RUN_NAME}/$RUN_NAME/; s/\${env:RPS_FROM}/$RPS_FROM/; s/\${env:RPS_TO}/$RPS_TO/" \
    -e "s/\${env:RPS_STEP}/$RPS_STEP/; s/\${env:STEP_DURATION}/${STEP_DURATION}s/; s/\${env:INSTANCES}/$INSTANCES/" \
    pandora.yaml > "out/$RUN_NAME.pandora.yaml"

start_usec=$(cpu_usec)
start_ns=$(python3 -c 'import time; print(time.time_ns())')
docker compose --profile tools run --rm pandora pandora "/work/out/$RUN_NAME.pandora.yaml"
end_usec=$(cpu_usec)
end_ns=$(python3 -c 'import time; print(time.time_ns())')
python3 -c "print(f'service CPU: {($end_usec - $start_usec) / (($end_ns - $start_ns) / 1000):.3f} cores on average over {($end_ns - $start_ns) / 1e9:.0f}s')" \
    | tee "out/$RUN_NAME.cpu"

python3 report.py "out/$RUN_NAME.phout" --from "$RPS_FROM" --step "$RPS_STEP" --duration "$STEP_DURATION" \
    --json "out/$RUN_NAME.json" | tee "out/$RUN_NAME.md"

if [ "$PERF" = 1 ]; then
    wait "$perf_job"
    docker run --rm -v courier-build:/src/build:ro -v "$PWD/out:/out" -w /out courier-dev bash -c "
        set -e
        [ -d FlameGraph ] || { git clone -q https://github.com/brendangregg/FlameGraph && git -C FlameGraph checkout -q $FLAMEGRAPH_COMMIT; }
        perf script -i $RUN_NAME.perf.data 2>/dev/null > $RUN_NAME.perf.txt
        FlameGraph/stackcollapse-perf.pl $RUN_NAME.perf.txt > $RUN_NAME.folded
        FlameGraph/flamegraph.pl --title '$RUN_NAME' $RUN_NAME.folded > $RUN_NAME.svg"
    echo "flamegraph: loadtest/out/$RUN_NAME.svg"
fi

[ "${KEEP:-0}" = 1 ] || docker compose down -v >/dev/null
