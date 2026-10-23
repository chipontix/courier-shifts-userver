# Build/test environment: userver build deps (subset of userver's
# scripts/docs/en/deps/ubuntu-24.04.md), clang 18, PostgreSQL 16 and Redis
# servers for testsuite, perf for profiling. Native on x86_64 and aarch64.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC LANG=C.UTF-8
COPY docker/apt-deps.txt /tmp/apt-deps.txt
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl \
      $(cat /tmp/apt-deps.txt) \
      clang-18 lld-18 llvm-18 libclang-rt-18-dev \
      postgresql-16 redis-server python3-pip \
      linux-tools-generic linux-tools-common \
 && apt-get clean && rm -rf /var/lib/apt/lists/*

# perf from linux-tools is tied to a kernel version; any recent build works
# with the host kernel for sampling, so expose the installed one on PATH.
RUN ln -sf "$(ls -d /usr/lib/linux-tools/*/ | head -1)perf" /usr/local/bin/perf

# testsuite's initdb wants en_US.UTF-8.
RUN apt-get update && apt-get install -y --no-install-recommends locales \
 && locale-gen en_US.UTF-8 && apt-get clean && rm -rf /var/lib/apt/lists/*

ENV CC=clang-18 CXX=clang++-18 CCACHE_DIR=/ccache
