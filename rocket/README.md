# rocket-patches

Six small out-of-tree patches to the mainline RK3588 `rocket` DRM-accel driver
(`drivers/accel/rocket/`), developed against v7.1-rc2:

| Patch | Purpose |
|---|---|
| `081-rocket-drv-npu-clk.patch` | Raise the NPU compute clock above its 200 MHz default. |
| `082-rocket-drv-npu-volt.patch` | Couple the `vdd_npu_s0` rail voltage to that clock (f/V coupling) so a raised clock always gets a matching voltage. Apply after the clk patch. |
| `086-rocket-drv-perf-counters.patch` | Read-only DDMA register probe via debugfs (v2; the v1 0x2000 byte-counter read is removed — it hard-locked rk3588). Debug-only (`CONFIG_DRM_ACCEL_ROCKET_PERF_PROBE`). |
| `083-rocket-drv-iommu-keepattach.patch` | Keep the NPU IOMMU domain attached across same-context jobs instead of attach+detach per job. Backport of upstream RFC v4 5/9. Touches `rocket_core.{c,h}` + `rocket_job.c`. Measured −20 µs/submit (~38%) on RK3588 — a per-job dispatch-floor win for small/many-submit workloads. |
| `084-rocket-drv-fix-bo-mm-uaf.patch` | Fix a use-after-free on file close: the per-context IOVA allocator is moved from `rocket_file_priv` into the refcounted IOMMU domain, so a BO freed asynchronously by the drm_sched worker after `postclose` no longer touches freed memory. Touches `rocket_drv.{c,h}` + `rocket_gem.{c,h}`; independent of the others. Upstream candidate. |
| `085-rocket-drv-batched-submit.patch` | Run a multi-task job's tasks in one HW kick (`TASK_NUMBER=N`, single completion IRQ) instead of one submit per task. Selected per job by the `DRM_ROCKET_JOB_BATCHED` uAPI flag (master kill-switch param `rocket_batch_submit`, default 1). Touches `rocket_job.{c,h}` + the uAPI header (`uapi/rocket_accel.h`). Pairs with userspace regcmd chaining (`rocket-userspace` `ROCKET_BATCH_SUBMIT=1`), which flags only self-chained jobs — so chained fp16 and gapped int8/int4 coexist in one process. A dispatch-floor win on jobs that decompose into independent tiles. |

All are module rebuilds — the kernel image and device tree are never touched,
so boot is never at risk and a bad outcome is recovered with `rmmod` or a reboot.
They perform every hardware access from inside the driver's runtime-PM/job hooks, so the
NPU power domain is always powered first: a register access on an unpowered domain is
the one operation that wedges this SoC's power firmware.

A related runtime lever (no driver change) is documented at the end: binding the
NPU completion IRQs to a big core. It stacks with the keep-attached patch.

## The rocket NPU stack

These patches are the optional tuning layer of a four-part open source stack for
Rockchip NPUs:

- **`rocket-patches`** (this project) — optional kernel-module patches (clock / voltage /
  IOMMU). The clock patch raises the NPU from its 200 MHz boot default to 600 MHz
  (~1.43× throughput); the others trim per-submit dispatch latency. The performance
  figures quoted by the userspace projects below assume the clock patch is applied.
- **[`rocket-userspace`](https://github.com/gregordinary/rocket-userspace)** (`librocketnpu`) — the userspace driver, matmul, and on-NPU op
  library. Runs on the stock driver; these patches only raise its ceiling.
- **[`ggml-rocket`](https://github.com/gregordinary/ggml-rocket)** — a ggml backend `.so` for `llama.cpp` / `whisper.cpp`.
- **[`tflite-rocket`](https://github.com/gregordinary/tflite-rocket)** — a TFLite external delegate for detection models.

None of these patches are required to run the stack — they raise the performance
ceiling, and the userspace builds work on an unpatched mainline `rocket` driver.

## NPU compute clock (`081-rocket-drv-npu-clk.patch`)

The device tree pins the NPU compute clock to 200 MHz and the `rocket` driver has no
devfreq, so the NPU runs at one-fifth of its usable speed. This patch adds a module
parameter that programs a chosen rate from inside runtime-resume — the only safe
moment, because the power domain is off until the PM core resumes it and a cold
clock-set hangs the firmware.

It sets a fixed rate, not a scaling governor: while the NPU is active the clock
holds the rate you choose; when the core goes idle and runtime-suspends, the patch
parks it back at 200 MHz so the domain never powers down at a raised rate.

- **`rocket_npu_clk_hz`** — module parameter, sysfs-writable, default `0` (stock
  200 MHz). Target rate in Hz.

```sh
sudo rmmod rocket
sudo insmod rocket.ko rocket_npu_clk_hz=600000000
# dmesg reports each core's new rate; it parks at 200 MHz when idle.
```

**Operating point: 600 MHz.** Stable at the stock 0.80 V NPU rail (matmul tests
bit-exact, coherent model output), for roughly 1.43× prefill throughput over 200 MHz
at modest temperatures. Above 600 MHz the matmul becomes bound by host readback and
per-job launch overhead rather than clock rate, so higher clocks gave no further gain
here. Do not pin the power domain on to force a rate — that defeats the idle park and
re-creates the cold power-on hang.

## NPU rail voltage coupling (`082-rocket-drv-npu-volt.patch`)

The clock patch above sets only the clock. The NPU rail (`vdd_npu_s0`, the
RK8602 PMIC at i2c `0x42`, 0.55–0.95 V) is never scaled — and BL31/firmware sets
only the PLL, never the voltage, so if Linux doesn't couple V to f, nothing
does. At ≤600 MHz that is fine (the rail sits at 0.80 V, above the ~0.70 V that
rate needs). Past that it isn't: 900 MHz needs the voltage and got none → the
V/f-marginal state that hard-locked the box. This patch makes raising the clock
safe by construction, so >600 MHz later is a config change, not new code.

It is a driver-only change — the regulator is already wired in the mainline RK1
device tree (`npu-supply = <&vdd_npu_s0>` on all three core nodes), so no DT
change is needed. Key points:

- **Hold the regulator for the device lifetime** (`devm_regulator_get_optional`
  at core init, stashed in `struct rocket_core`). The regulator framework
  aggregates live consumers by max; a `regulator_put` drops our vote, so a
  get/put-per-resume (the clk pattern) would not hold the voltage. We never
  `regulator_enable()` — the rail is `pd_npu`'s domain-supply, enabled by the
  genpd; we only adjust voltage and read it back to confirm.
- **Ordering:** voltage up before clock up (resume); clock down before
  voltage down (suspend).
- **0.80 V floor** (`uv = max(vendor_uv(hz), 800000)`). The rail is already at
  0.80 V, and aggregation is by max, so voting the floor pins it at today's
  voltage at ≤600 MHz (no change) and prevents our own vote from ever
  undervolting the shared rail. Vendor f→V map: 300–700 → 0.70 V, 800 → 0.75 V,
  900 → 0.80 V, 1000 → 0.85 V; with the floor that is 0.80 V up to 900 MHz and
  0.85 V at 1 GHz. Degrades to clock-only if no `npu-supply` is wired.

- **`rocket_npu_uv`** — module parameter, sysfs-writable, default `0` (use the
  vendor map + floor). A microvolt override for future >600 MHz bring-up only.

```sh
sudo rmmod rocket
sudo insmod rocket.ko rocket_npu_clk_hz=600000000     # rocket_npu_uv=0 (default)
# dmesg now shows, per core, a "NPU vdd -> 800000 uV (reads back 800000 uV)"
# line *and* the existing "NPU clk -> 600000000 Hz" line.
# vdd_npu_s0 stays 0.80 V — unchanged from the clk-only build (non-disruption).
```

Validated at 600 MHz: with this patch loaded, `vdd_npu_s0` reads 0.80 V before load,
after load, and while running matmul jobs (the floor pins it at today's voltage — zero
movement); matmul tests stay bit-exact, and the clock parks back at 200 MHz with the
rail at 0.80 V when the core idles, with a clean unload.

**Do not raise `rocket_npu_clk_hz` above 600 MHz** until the dispatch/readback
floor (not the clock) is the bottleneck — at 900 MHz the speedup was zero. This
patch only makes the lever safe to pull; it does not pull it. The deferred
>600 MHz sweep must also watch temps (no auto-throttle) and ideally capture this
chip's OTP PVTPLL `max` first.

## DMA byte counters (`086-rocket-drv-perf-counters.patch`)

This probe targets the NPU's hardware DMA "amount" counters (weight-read / data-read /
data-write bytes) via debugfs, to measure DMA traffic in bytes moved rather than
inferring it from wall time. On RK3588 the real counters are not usable, so the patch
ships only a safe characterisation probe.

It is a debug/RE tool, compiled out by default: the probe (and its module params)
exist only when the kernel is built with `CONFIG_DRM_ACCEL_ROCKET_PERF_PROBE=y` (a new
debug Kconfig, default `n`, that the patch adds under `DRM_ACCEL_ROCKET`). A shipping
build carries no `rocket_perf` debugfs surface at all. `rocket_ddma_probe` is read-only
via sysfs (`0444`) — armed once at modprobe time, never re-armable on a live system —
and the `power_hold` get/put refcount is mutex-serialized against racing debugfs writes.

### The BSP `0x2000` amount offsets are not usable on RK3588

The vendor BSP exposes these counters only on sibling SoCs and sets `amount_top = NULL`
on RK3588; the offsets it would use live on a per-core perf page at
`pc_resource.start + 0x2000` (`top` `0x2234/38/3c`, `core` `0x2434/38/3c`). On RK3588
that page is undecoded (absent from the Mesa register map, which has no registers in
`0x2xxx`). The v1 probe `ioremap`'d it and read those offsets — and the read
hard-locked the SoC (a write to the page survived; the read raised a bus abort).
Conclusion: a high-confidence practical negative — these offsets are unusable and
unsafe to read via `rocket` on RK3588.

The `0x2000` read/`clear` paths are removed, not stubbed — a stub is a re-armable
footgun. The patch ships only the safe `0x8000` probe below.

### What ships: a safe, read-only DDMA probe (`0x8000`)

The probe (canonical source: [`perf-probe-v2-safe.c`](perf-probe-v2-safe.c)).
Unlike `0x2000`, the DDMA block at `0x8000` is a defined/decoded domain in the Mesa
map (`0x8030 = CFG_STATUS`), so reading it is materially lower-risk. The probe reads
that block to (a) characterise the RK3588 DDMA registers and (b) check whether the
legacy rk356x amount offsets (`0x8034/38/3c`) carry anything counter-like here. Safety
is layered:

- **READ-ONLY** (no writes → cannot corrupt DMA config; note `0x8010 = RD_WEIGHT_1`).
- **CORE-0 only**, and disarmed by default — the MMIO reads happen only when the
  module is loaded with `rocket_ddma_probe=1`; a plain `cat` while disarmed touches
  no hardware.
- **Known-register-first**: `0x8030 CFG_STATUS` is read before anything else, each read
  in its own `seq_printf`, so a fault is attributable to one exact register.
- **`power_hold`** (write `1`/`0`) is retained to pin the domains across a job (e.g. to
  read the DDMA regs before/after traffic).

> Caution: this still reads an address `rocket` does not normally map. Run it with a
> hardware watchdog + serial console armed, so that if even `0x8030` fails to decode
> the box auto-recovers and the hang is logged.

```sh
# load disarmed first; confirm 'cat ddma' prints the "disarmed" notice (no HW touched):
sudo insmod rocket.ko rocket_npu_clk_hz=600000000
cat /sys/kernel/debug/rocket_perf/ddma          # -> "disarmed ..."

# then, with a watchdog running, arm and read (optionally bracket an NPU job):
sudo rmmod rocket; sudo insmod rocket.ko rocket_npu_clk_hz=600000000 rocket_ddma_probe=1
sudo sh -c 'echo 1 > /sys/kernel/debug/rocket_perf/power_hold'
sudo cat /sys/kernel/debug/rocket_perf/ddma     # baseline DDMA regs
sudo -E ./build/matmul_tiled_rocket 512 3840 4096
sudo cat /sys/kernel/debug/rocket_perf/ddma     # after a job — did any legacy offset move?
sudo sh -c 'echo 0 > /sys/kernel/debug/rocket_perf/power_hold'
```

If even the `0x8000` page won't decode, that closes the on-NPU-counter avenue; the next
fallback (a DDR/DFI or NOC/MSCH PMU with master-ID filtering) measures the same traffic
from outside the NPU register space.

## IOMMU keep-attached (`083-rocket-drv-iommu-keepattach.patch`)

Stock `rocket` attaches the job's per-context IOMMU domain in `rocket_job_run()` and
detaches it again on every completion and reset. Each toggle drives the rk_iommu
stall/force-reset/paging handshake. On RK3568 that handshake times out on the idle NPU
MMU (the bug the upstream RFC fixes); on RK3588 it completes but still costs latency
on every submitted `drm_sched` job.

This is a backport of **"[RFC PATCH v4 5/9] accel: rocket: Keep the IOMMU domain attached
across jobs"** (Midgy BALON, linux-rockchip 2026-06-13). It tracks the attached domain in
`struct rocket_core` (`attached_domain`), re-attaches only when a job from a **different
context** runs, holds a `kref` so the domain outlives the job that first attached it, and
detaches only at core teardown / after a reset (a reset wipes the IOMMU page-table base,
so the domain is dropped to force re-attach). It touches three files —
`rocket_core.{c,h}` and `rocket_job.c` — unlike the others which are `rocket_drv.c`-only.

**Measured on RK3588 @600 MHz (clean A/B, 2026-06-22):** −20 µs/submit (~38%), median
54→34 µs (`rocket-userspace/tests/submit_overhead_rocket.c`), cross-checked at ~17–18 µs/job by
`multicore_probe`; flat on single-job prefill (the cost is per submit, not per task).
CTest 8/8, dmesg clean. A per-job dispatch-floor win for small/many-submit workloads
(decode GEMV, detection convs/1×1s, multi-fd, cross-op batch).

> NOTE vs upstream: our in-tree `rocket_reset()` detaches inside the `job_lock`
> scoped_guard (before `rocket_core_reset()`) rather than after it; the keep-attached
> drop is applied in place there. Functionally equivalent for the bookkeeping.

Independent of the clk/volt patches — applies and reloads on its own.

## BO IOVA-allocator use-after-free fix (`084-rocket-drv-fix-bo-mm-uaf.patch`)

Stock `rocket` keeps each context's IOVA allocator (`struct drm_mm mm` + `mm_lock`)
in `struct rocket_file_priv` and tears it down (`drm_mm_takedown`) and frees the
struct in `rocket_postclose`. But a BO's IOVA node is removed only in its GEM
`.free` path, and a job's BO references are dropped asynchronously by the
drm_sched free worker (`rocket_job_free` → `rocket_job_cleanup`), which can run
after the owning file is closed. A just-completed job therefore keeps a BO alive
past `postclose`; the later `bo_free` then calls `drm_mm_remove_node()` /
`mutex_lock()` on the already-`kfree`d `rocket_file_priv` — a use-after-free,
first visible as a `drm_mm_takedown` "allocator still has nodes" `WARNING`
(`drivers/gpu/drm/drm_mm.c:965`).

It reproduces with the FFN block (three `NT=3` multicore matmuls open/submit/close
many short-lived worker fds): on RK3588 a single `ffn_rocket` run emitted up to 8
such warnings; the race is timing-dependent (it needs the fd close to win against
the free worker), so it is intermittent.

The fix moves `mm`/`mm_lock` into `struct rocket_iommu_domain`, which is already
reference-counted and held by the file, by every mapped BO, and (while attached) by
the core. The allocator is initialized in `rocket_iommu_domain_create` and torn
down in `rocket_iommu_domain_destroy` (last reference) — so a late `bo_free`
removes its node via `bo->domain->mm`, a domain it still holds a reference to, and
the takedown runs only when the allocator is provably empty. This is a latent
upstream bug independent of the other four patches (apply in any order); it is an
upstream candidate.

## Batched submit (`085-rocket-drv-batched-submit.patch`)

Stock `rocket` submits one NPU task per HW kick: `rocket_job_hw_submit()` programs one
task's regcmd, sets `PC_TASK_CON.TASK_NUMBER(1)`, and the IRQ handler re-arms the next
task on every completion. A matmul tiled into many output tiles pays one submit + one
completion IRQ + one waiter wakeup per tile — the dispatch-floor cost on the prefill
matmul.

The RK3588 PC can stream a contiguous run of self-chaining regcmds from a single kick.
The patch selects that per job via a uAPI flag (`DRM_ROCKET_JOB_BATCHED` in
`drm_rocket_job.flags`): a flagged job with more than one task has `TASK_NUMBER` set to the
task count and `next_task_idx` advanced to the end, so the stock IRQ-handler retire path
fires the job's fence on the single `TASK_NUMBER`-gated completion IRQ instead of
re-arming per task. An unflagged job keeps the stock per-task path. The IRQ handler is
otherwise unchanged. The module param `rocket_batch_submit` (0644, default 1) is a master
kill-switch over the flag (0 forces every job per-task). The `flags` field is appended past
the original `drm_rocket_job`, copied `min(job_struct_size, sizeof)` + zero-fill, so a stock
userspace is unaffected. Install the matching uAPI header (`rocket-patches/uapi/rocket_accel.h`)
where both the kernel build and userspace see it.

This is the kernel half only. It requires the userspace regcmd-chaining pass that lays a
flagged job's tasks contiguously and links each task's trailer (an embedded `PC_BASE_ADDRESS`
op to the next task's address, plus its `PC_REGISTER_AMOUNTS` length) — `rocket-userspace`
with `ROCKET_BATCH_SUBMIT=1`, which sets the flag exactly on the jobs it self-chained.
**Enable both halves together**: a flagged job with a stock gapped layout runs task 0,
streams into the gap, and times out (recoverable — drm_sched resets the core). The
`PC_BASE_ADDRESS` redirect is load-bearing; contiguous layout with only the length op still
stops after task 0. Because the flag is per-job, chained fp16 and gapped int8/int4/prepacked
jobs coexist in one process.

Measured (RK1, 600 MHz, `performance`, `matmul_tiled_rocket 512 3840 4096`, 320 tiles → 5
batches of 64): the profiled `wait` term drops ~62 → ~48 ms and GFLOP/s rises ~94 → ~96
(best 99) warm, bit-exact (cosine 1.000000) vs the per-task path. The win is the dispatch
slice of a matmul that is otherwise NPU-compute/readback-bound at this operating point —
larger on submit-overhead-bound paths. Full mechanism and the disproven kernel-only
descriptor-table model: [`BATCHED_SUBMIT_FINDINGS.md`](BATCHED_SUBMIT_FINDINGS.md).

## NPU IRQ affinity (runtime knob, no patch)

A companion runtime lever — no driver change, but in the same dispatch-floor family
as the keep-attached patch. RK3588 is little.BIG: cpu0–3 are Cortex-A55 (little), cpu4–7
are Cortex-A76 (big). The NPU completion IRQs default to the all-CPUs mask `0-7`, and
GICv3 delivers a level interrupt to the lowest CPU in the mask = cpu0, an A55 little
core — so the completion ISR and the wake of the blocked waiter both run at 1.8 GHz, and
the per-submit dispatch floor is ~51 µs.

Binding the three NPU IRQs to a big core cuts that to ~33 µs (−40%); also pinning the
submitting/waiting thread to the same big core gets ~27 µs (−47% vs default). Pinning the
app alone, while the IRQ still lands on the A55, is a no-op — the IRQ binding is the
prerequisite. Like the keep-attached patch it pays on many-small-submit paths and is flat
on a single big prefill job; the two stack (~27 µs floor co-located + keep-attached, vs
~54 µs stock on default affinity).

The IRQ GIC numbers are SoC/kernel-specific — read `/proc/interrupts` rather than assuming
(on kernel 7.1 they were 69/70/71 for the three `.npu` nodes). A helper is provided:

```sh
# latency / single-stream (one-camera detection, decode): all 3 NPU IRQs -> cpu7
sudo rocket-userspace/tools/npu_set_irq_affinity.sh latency 7
taskset -c 7 <app>                                        # run the app on the same core

# throughput / multi-fd pool: spread the 3 IRQs across 3 big cores, pin each worker
sudo rocket-userspace/tools/npu_set_irq_affinity.sh throughput  # 69->5 70->6 71->7

sudo rocket-userspace/tools/npu_set_irq_affinity.sh reset       # back to 0-7
sudo rocket-userspace/tools/npu_set_irq_affinity.sh show
```

This is not persistent across reboot — wire `latency`/`throughput` into a systemd unit or
an `rc.local`/`udev` hook (the IRQs exist as soon as `rocket` probes). If `irqbalance` is
running it may re-migrate the IRQ; pin after it or mask these IRQs in its config.

## Portability to other RK3588 boards and kernels

These patch the SoC-level `rocket` driver (`drivers/accel/rocket/`), not the board device
tree or the kernel image, so on any RK3588 board running the same mainline `rocket`
(developed against v7.1-rc2) they apply to identical source and rebuild the same way. The
one hard requirement is that the board runs the mainline `rocket` driver and that the module is rebuilt against the exact running
kernel (the vermagic must match).

The two board-dependent pieces both degrade gracefully:

- **Voltage coupling** needs `npu-supply` wired to the NPU rail in that board's device
  tree (the Turing RK1 has `npu-supply = <&vdd_npu_s0>` on all three core nodes). A board
  that does not wire it falls back to clock-only — and at ≤600 MHz the voltage patch is a
  no-op regardless (it only pins the rail at its existing 0.80 V), so this matters only for
  a future >600 MHz bring-up.
- **IRQ affinity** uses GIC interrupt numbers that are kernel/SoC-specific — read
  `/proc/interrupts` for the three `.npu` nodes rather than assuming (they were 69/70/71 on
  kernel 7.1).

A different mainline version may have moved the `rocket` driver and require rebasing the
patches; each is small and self-contained (a handful of files each), so a rebase is
mechanical — but re-verify on that kernel, since the bit-exact and dispatch-floor figures
here were measured on v7.1 at 600 MHz.

## Build

Apply against the `rocket` driver in your kernel source tree and build the module:

```sh
cd <kernel-tree>
patch -p1 < /path/to/rocket-patches/081-rocket-drv-npu-clk.patch
patch -p1 < /path/to/rocket-patches/082-rocket-drv-npu-volt.patch        # f/V coupling; needs the clk patch
patch -p1 < /path/to/rocket-patches/083-rocket-drv-iommu-keepattach.patch # optional; independent
patch -p1 < /path/to/rocket-patches/084-rocket-drv-fix-bo-mm-uaf.patch    # UAF fix; independent
patch -p1 < /path/to/rocket-patches/085-rocket-drv-batched-submit.patch  # optional; one-kick multi-task (per-job flag)
patch -p1 < /path/to/rocket-patches/086-rocket-drv-perf-counters.patch   # optional; debug-only, apply last
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- drivers/accel/rocket/rocket.ko
```

The perf-counter probe additionally needs `CONFIG_DRM_ACCEL_ROCKET_PERF_PROBE=y`
(default `n`) in the kernel config; without it the other three patches build and the
probe is simply absent.

Copy `rocket.ko` to the device and reload it (`rmmod rocket && insmod rocket.ko …`);
its vermagic must match the running kernel. To return to stock, reload the distribution
module or reboot.

## License

**GPL-2.0-only**, per the [`LICENSE`](LICENSE) file. These patches modify the mainline
`rocket` DRM-accel driver (`drivers/accel/rocket/`), which is `GPL-2.0-only`
(© Tomeu Vizoso / Collabora), and the Linux kernel is GPL-2.0-only — so the patches and
the `perf-probe-v2-safe.c` snippet (the canonical source for the perf-counter patch) are
derivative kernel work and carry the same `GPL-2.0-only` identifier. The
`083-rocket-drv-iommu-keepattach.patch` is a backport of an upstream RFC (Midgy BALON,
linux-rockchip) and retains that authorship.

This is intentionally GPL-2.0-only, not the `GPL-3.0-or-later` used by the userspace
projects in this stack (the driver library and the ggml / TFLite frontends): those never
link kernel code, whereas GPL-3.0 is incompatible with the GPL-2.0-only kernel, so kernel
patches must stay GPL-2.0.
