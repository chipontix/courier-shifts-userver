Early exploratory run (2k..20k RPS, 20 s steps, congestion control on,
baseline handlers as in commit 7721076) while the laptop was busy with other
workloads (load average 4-8). CC shed 16-48% of requests from 2k RPS, while
at 2k-6k RPS p99 stayed under 1 ms. The likely cause: CC's overload sensor
counts tasks that waited over 3 ms in the task queue, and vCPU descheduling
in the VM produces such waits. A similar ladder on a quieter host
(results/baseline/ladder-cc.md, 4k..32k) shed nothing up to 24k RPS.
