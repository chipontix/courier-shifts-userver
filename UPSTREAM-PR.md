# Upstream PR to userver: `hostinfo::CpuLimit()` reports a limit when there is none

Prepared in the fork, not opened. Rabiya opens the PR herself.

- Fork branch: <https://github.com/chipontix/userver/tree/fix-hostinfo-cpu-limit>
- Base: `userver-framework/userver` `develop` at `0d550b04d`
- Commits: [`800c60d46`](https://github.com/chipontix/userver/commit/800c60d46) (regression test, fails),
  [`c4bc0dc6a`](https://github.com/chipontix/userver/commit/c4bc0dc6a) (fix, test passes)
- Status on 2026-11-01: no issue or PR about it upstream, open or closed (`gh search issues --include-prs` for
  `CpuLimit`, `IsInRtc`, `cpu_limit`, `CPU limit`, `cgroup`, `cpu.max`, `only-rtc`, `fake mode`, and the titles
  of all open PRs). Check again right before opening.

## The problem

`universal/src/hostinfo/cpu_limit.cpp` gained a cgroup v2 fallback in `b0efc2b49` ("feat core: fallback to
cpu limit from cgroup v2 if present"), which is in v2.16, v3.0, v3.1 and v3.2. Two things went wrong there:

1. `GetCpuLimit()` returns `double` and ends with `return 0;` when no source has a limit. `CpuLimit()`
   wraps that in `std::optional<double>`, so the optional is always engaged, and `IsInRtc()` (`!!CpuLimit()`)
   is always `true`. Before `b0efc2b49`, both returned `nullopt`/`false` without `CPU_LIMIT`.
2. `CpuLimitCgroupV2()` returns `std::optional<int>`, but computes `cpu_max / cpu_period` as `double`.
   The fraction is truncated: `cpu.max = "150000 100000"` (`docker run --cpus=1.5`) gives 1, and
   `--cpus=0.5` gives 0.

What a user sees:

- `congestion_control` with the default `only-rtc: true` is meant to refuse to start without a known CPU
  limit ("Started outside of RTC, don't know how to get number of CPU cores..."). That check never fires now.
  Instead `cpu_limit` is 0, which is less than `min-cpu`, and the component silently switches to fake mode
  with only a warning. A service with a typo such as `CPU_LIMIT=2` (instead of `2c`), or with no limit at
  all, runs with congestion control off.
- With `--cpus=0.5` the cgroup limit becomes 0 and congestion control goes to fake mode as well;
  with `--cpus=1.5` it believes it has 1 core.
- `components::Manager` logs a misleading "CPU limit from env CPU_LIMIT (0) looks very different from the
  estimated number of hardware threads" for every task processor that guesses its thread count.

This project hit the first case: `CPU_LIMIT=2` put congestion control in fake mode without failing at start
(README, "Bugs the tests and the stand found", item 4).

## How to reproduce

The regression test needs only `userver-universal`:

```bash
git clone https://github.com/chipontix/userver && cd userver
git checkout 800c60d46            # test only, no fix
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DUSERVER_FEATURE_CORE=OFF \
  -DUSERVER_FEATURE_CHAOTIC=OFF -DUSERVER_FEATURE_CHAOTIC_OPENAPI=OFF -DUSERVER_BUILD_TESTS=ON \
  -DUSERVER_FEATURE_JEMALLOC=OFF
cmake --build build --target userver-universal-unittest -j
./build/universal/userver-universal-unittest --gtest_filter='HostinfoCpuLimit.*'   # 2 failures
git checkout c4bc0dc6a
cmake --build build --target userver-universal-unittest -j
./build/universal/userver-universal-unittest --gtest_filter='HostinfoCpuLimit.*'   # passes
```

Measured on 2026-11-01, macOS 26 on Apple M4 Max, Apple clang 21, native build of `userver-universal` only
(Release, 777 MB build tree):

Before the fix (`800c60d46`):

```
[ RUN      ] HostinfoCpuLimit.NoLimit
cpu_limit_test.cpp:60: Failure
Expected equality of these values:
  GetCpuLimit()
    Which is: (0)
  std::nullopt
[ RUN      ] HostinfoCpuLimit.FractionalCgroupLimit
cpu_limit_test.cpp:65: Failure
  GetCpuLimit()
    Which is: (1)
  1.5
[  FAILED  ] 2 tests
```

After the fix (`c4bc0dc6a`): both pass, also with `--gtest_repeat=200` and `--gtest_repeat=200 --gtest_shuffle`.
The whole `userver-universal-unittest` passed, 2162 of 2162.

Not covered: `core` was not built, so the `congestion_control` component and `components::Manager` were
checked by reading the code, not by a test. Neither has a test for CPU-limit handling upstream either.

Build notes for the Mac, none of which touch the patch:

- Homebrew's `openssl` is now OpenSSL 4.1, and `universal/src/crypto/certificate.cpp:98` does not compile
  against it (`X509_get_subject_name` returns `const X509_NAME*`). The build used a copy of OpenSSL 3.6
  headers. That is a separate upstream issue, not part of this PR.
- re2 2023-03-01, which userver downloads, does not compile with Homebrew ICU 78, so re2 was built
  locally without ICU. yaml-cpp 0.8.0 was built locally as well.
- Four `formats::json` iterator tests (`FormatsJsonSpecificMemberAccess.ArrayIterator*`,
  `FormatsJson/MemberAccess/0.MemberPathsByIterator`) failed in one full run on unpatched `develop`
  (`0d550b04d`) and in one full run on the branch, and passed in the later full run and with
  `--gtest_filter='FormatsJson*'`. They are flaky and unrelated to this change. Also worth a separate look.

## Does it change this project?

No, not with the current config. The service sets `CPU_LIMIT: "2c"` explicitly in
[`loadtest/docker-compose.yml`](loadtest/docker-compose.yml) and `only-rtc: false` in
[`configs/static_config.yaml`](configs/static_config.yaml), and the stand pins CPUs with `cpuset`, not a CFS
quota, so cgroup `cpu.max` is `max`. The fix only changes what happens when the limit is missing or
fractional. That is why there is no separate branch of the project on a patched userver (brief, item 5):
there is nothing to measure. With the fix, the project could drop `only-rtc: false` and get a start-up error
instead of silent fake mode if `CPU_LIMIT` is ever mistyped again.

## CONTRIBUTING and repository rules

From `CONTRIBUTING.md`, `AGENTS.md` and `scripts/docs/en/userver/development/code_review.md` in userver:

- **CLA.** Contributions require the Yandex CLA (<https://yandex.ru/legal/cla/?lang=en>). Opening a PR or an
  issue counts as accepting it (note in `.github/pull_request_template.md`).
- **PR title** `type component: message`, imperative, at most 72 characters preferred. Types: `bug`, `fix`,
  `feat`, `refactor`, `typo`, `cc`. Description in English: the problem, the resulting behaviour, and a
  paragraph on how it was validated.
- **Tests.** A bug fix needs one test that fails without the fix; add more only for distinct behaviour.
- **Code.** No comments in production code; internal headers go to `src/`, not `include/userver/`;
  public API stays compatible (the change keeps `CpuLimit()` and `IsInRtc()` signatures).
- **Build systems.** CMake always; `ya.make` only where it already exists (not for `universal`).
  The new `*_test.cpp` is picked up by the existing glob, so no CMake change is needed.
- **AI.** `AGENTS.md` says: no `Generated with ...` lines in PR descriptions and no `Co-Authored-By: <agent>`
  trailers. It does not forbid AI-assisted contributions. The orchestrator's rule for this portfolio is to
  always disclose AI help in the PR text, so the description below says it in one plain sentence in the
  validation paragraph rather than as a "Generated with" footer. Rabiya decides on the final wording.

## PR text

**Title:** `fix hostinfo: return no CPU limit when none is set`

```markdown
Since the cgroup v2 fallback (b0efc2b49), `hostinfo::CpuLimit()` never returns `std::nullopt`:
`GetCpuLimit()` returns `double` and ends with `return 0;` when neither `CPU_LIMIT`, `DEPLOY_VCPU_LIMIT`
nor cgroup v2 `cpu.max` sets a limit. As a result `hostinfo::IsInRtc()` is always `true`.

The visible effect is in `congestion_control`. With the default `only-rtc: true` it is supposed to refuse
to start when the CPU limit is unknown. Now that check never fires: the component gets a limit of 0, which is
below `min-cpu`, and silently switches to fake mode with only a warning. A typo such as `CPU_LIMIT=2`
(instead of `2c`) or a container without any limit therefore runs with congestion control disabled.
`components::Manager` also logs "CPU limit from env CPU_LIMIT (0) looks very different from the estimated
number of hardware threads" for each task processor in that case.

A second problem in the same code: `CpuLimitCgroupV2()` returns `std::optional<int>` and truncates the
quota. `cpu.max` of `150000 100000` (`docker run --cpus=1.5`) gives 1 CPU, and `50000 100000` gives 0,
which again puts congestion control into fake mode.

This change returns `std::nullopt` when no source sets a limit and keeps the cgroup limit fractional, which
restores the behaviour from before the cgroup fallback for services without a limit. To test it, the paths
to `/proc/self/cgroup` and `/sys/fs/cgroup` became parameters of an internal `hostinfo::impl::GetCpuLimit()`;
the public API is unchanged.

Validation: two new tests in `universal/src/hostinfo/cpu_limit_test.cpp` run the detection against fake
cgroup files. Both fail without the fix (0 instead of `std::nullopt`, 1 instead of 1.5) and pass with it,
including 200 shuffled repeats. The full `userver-universal-unittest` passes on macOS (Apple clang 21,
Release). `core` was not built locally, so the effect on `congestion_control` was checked by reading the
code. The fix and the tests were prepared with the help of an AI coding assistant (Claude Code) and
reviewed by me.
```
