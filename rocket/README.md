# rocket-patches

Five patches to the mainline RK3588 `rocket` DRM-accel driver (`drivers/accel/rocket/`),
developed against v7.1. Each one is a module rebuild — the kernel image and the device
tree are never touched, so boot is never at risk and a bad outcome is recovered with
`rmmod` or a reboot. Every hardware access happens from inside the driver's runtime-PM
and job hooks, so the NPU power domain is always powered first: a register access on an
unpowered domain is the one operation that wedges this SoC's power firmware.

They are grouped by what they are *for*, not by number. Take the two **required** ones and
add the **optional** ones for more performance.

| | Patch | What you get |
|---|---|---|
| **Required** | `081-rocket-drv-npu-clk.patch` | Raises the NPU compute clock from its 200 MHz boot default to 600 MHz (~1.43× throughput). This is the patch that makes the NPU worth using. |
| **Required** | `084-rocket-drv-fix-bo-mm-uaf.patch` | Fixes a **use-after-free** on file close. A latent upstream bug, not a tuning knob; take it whether or not you care about performance. |
| Optional | `082-rocket-drv-npu-volt.patch` | Couples the `vdd_npu_s0` rail voltage to the clock. A **no-op at ≤600 MHz** — insurance for going above it. |
| Optional | `083-rocket-drv-iommu-keepattach.patch` | Keeps the IOMMU domain attached across jobs. −20 µs/submit (~38%). |
| Optional | `085-rocket-drv-batched-submit.patch` | Runs a job's tasks in one HW kick. Carries the `DRM_ROCKET_JOB_BATCHED` uAPI flag that userspace's `ROCKET_BATCH_SUBMIT` needs. |

Every patch is **correct on its own**: none of them ships a bug that a later patch fixes,
so any subset below is a supported configuration rather than a half-applied series.

## Dependencies

Only two, and both are real (not just diff context):

- **`082` requires `081`** — it scales the voltage the clock patch's rate needs.
- **`085` requires `083`** — batched submit builds on the keep-attached domain
  (they conflict in `rocket_job.c` otherwise).

`081`, `083` and `084` each apply to a pristine tree on their own, in any combination.
**Applying in ascending numeric order always satisfies the dependencies**, so if you are
unsure, just apply the ones you want lowest-number first.

## Compatibility matrix

Every row below was **applied, built, loaded, and run** on a Turing RK1 (RK3588,
kernel 7.1.1, `rocket_npu_clk_hz=600000000`) — none of it is inferred.

- **ctest** — the [`rocket-userspace`](https://github.com/gregordinary/rocket-userspace) suite (64 tests, 2 skipped).
- **clock** — 6× multi-worker resident-int8 matmul at stock PM defaults; "stable" means
  every run landed in a tight band with no catastrophic outlier (see [The shared clock](#the-shared-clock-why-081-refcounts-its-park)).
- **park** — after the run, all three cores `suspended` and `scmi_clk_npu` back at 200 MHz.

Each row is a distinct module build, and the loaded module's `srcversion` is asserted against
the `.ko` under test before every measurement — a stale module left loaded by a failed
`rmmod` is otherwise indistinguishable from a real result.

| Config | Applies | Builds | ctest | clock | park |
|---|---|---|---|---|---|
| *(stock, no patches)* | — | — | 63/64 † | 200 MHz | — |
| `081` | yes | yes | 63/64 † | stable | yes |
| `081` `084` — **the required set** | yes | yes | 63/64 † | stable | yes |
| `081` `082` | yes | yes | 63/64 † | stable | yes |
| `081` `083` `084` | yes | yes | 63/64 † | stable | yes |
| `081` `083` `084` `085` | yes | yes | **64/64** | stable | yes |

† **The one failing test is not a regression — it fails on stock too.** The suite's
`matmul_int8_dequant_rocket` exercises the batched-submit uAPI, so it needs `085`'s
`DRM_ROCKET_JOB_BATCHED` flag and returns `-EINVAL` (`ROCKET_SUBMIT: Invalid argument`)
on any kernel without it, including an unpatched one. Every config that includes `085`
is 64/64; every config without it fails exactly that one test and nothing else. If your
userspace uses `ROCKET_BATCH_SUBMIT`, `085` is not optional *for you*.

## The rocket NPU stack

These patches are the tuning layer of a four-part open source stack for Rockchip NPUs:

- **`rocket-patches`** (this project) — kernel-module patches. The clock patch raises the
  NPU from its 200 MHz boot default to 600 MHz; the others fix a UAF and trim per-submit
  dispatch latency. The performance figures quoted by the userspace projects below assume
  the clock patch is applied.
- **[`rocket-userspace`](https://github.com/gregordinary/rocket-userspace)** (`librocketnpu`) — the userspace driver, matmul, and on-NPU op library.
- **[`ggml-rocket`](https://github.com/gregordinary/ggml-rocket)** — a ggml backend `.so` for `llama.cpp` / `whisper.cpp`.
- **[`tflite-rocket`](https://github.com/gregordinary/tflite-rocket)** — a TFLite external delegate for detection models.

The userspace builds run on an unpatched mainline `rocket` driver (except for the batched
path noted above) — these patches raise the ceiling.

---

# Required

## NPU compute clock (`081-rocket-drv-npu-clk.patch`)

The device tree pins the NPU compute clock to 200 MHz and the `rocket` driver has no
devfreq, so the NPU runs at one-fifth of its usable speed. This patch adds a module
parameter that programs a chosen rate from inside runtime-resume — the only safe moment,
because the power domain is off until the PM core resumes it and a cold clock-set hangs
the firmware.

It sets a fixed rate, not a scaling governor: while the NPU is active the clock holds the
rate you choose; when the last core goes idle and runtime-suspends, the patch parks it
back at 200 MHz so the domain never powers down at a raised rate.

- **`rocket_npu_clk_hz`** — module parameter, sysfs-writable, default `0` (stock 200 MHz).
  Target rate in Hz.
- **`rocket_autosuspend_ms`** — module parameter, sysfs-writable, default `1000`. The park's
  deadline (see [The park's deadline](#the-parks-deadline-why-081-raises-the-autosuspend-delay)).
  `0` leaves the PM core's own default.

```sh
sudo rmmod rocket
sudo insmod rocket.ko rocket_npu_clk_hz=600000000
# dmesg reports each core's new rate; it parks at 200 MHz when the last core idles.
```

**Operating point: 600 MHz.** Stable at the stock 0.80 V NPU rail (matmul tests bit-exact,
coherent model output), for roughly 1.43× prefill throughput over 200 MHz at modest
temperatures. Above 600 MHz the matmul becomes bound by host readback and per-job launch
overhead rather than clock rate, so higher clocks gave no further gain here.

> **Do not pin the power domain on (`power/control=on`) to force the rate.** It defeats
> the idle park, which is the exact condition that hard-locks the box at 900 MHz.

### The shared clock: why `081` refcounts its park

The compute clock is **one** SCMI clock (`scmi_clk_npu`) shared by all three NPU cores —
`clk_summary` shows a single rate with `fdab/fdac/fdad0000.npu` all as consumers — but
runtime-PM is **per core**. So a park driven from a per-core suspend callback drops the
shared rate to 200 MHz out from under cores that are still running.

And it *sticks*. A core that is still runtime-**active** never re-raises the clock, because
`pm_runtime_get` on an already-active device fires no resume callback. Once a sibling parks
the rate, that core keeps running at 200 MHz until it happens to suspend and resume. The
rate does not dip — it stays down.

The rail does not have this problem: the regulator framework aggregates consumer requests
by max, so a sibling at a raised rate keeps its voltage. `clk_set_rate()` has no such
aggregation — last writer wins — so `081` refcounts the park by hand (`rocket_clk_users`,
under a mutex, since both PM callbacks run in process context and may sleep). **Only the
last core down parks the clock.**

This matters in practice because work is fanned across per-fd worker queues by default in
this stack, so any multi-core NPU workload is exposed. Without the refcount, on an RK1 at
stock PM defaults, identical invocations of a 3-worker resident-int8 matmul alternated
**15 ms / 69 ms — a 4.8× swing**, run to run, indefinitely, with `scmi_clk_npu` flapping
200 ↔ 600 MHz. It also silently corrupts A/B measurement: a worker sweep and a group sweep
both came back non-monotonic and were entirely artefact.

With the refcount, the same six runs land at 15.2 / 15.4 / 14.6 / 14.7 / 15.6 / 13.4 ms and
the clock holds 600 MHz for the whole active phase, with exactly one 600 → 200 transition:
the park at the end.

### The park's deadline: why `081` raises the autosuspend delay

Refcounting *who* may park is only half of it. The park also needs a **deadline** that
outlives a host-side gap **within** one inference — not merely the gap between inferences.

Upstream's autosuspend delay is 50 ms, and the comment says why: "~3 frames at 60Hz". That
is sized for a media pipeline, which submits one inference per frame, so the only idle gaps
that matter fall *between* inferences. An LLM or ASR workload breaks the assumption. The
host packs operands into the NPU's native tiled layout — the NPU cannot tile row-major data
on-chip, so that scatter is irreducible host work — and a **single** inference therefore
contains host-side gaps in which every core goes idle. At 50 ms those gaps trip runtime
suspend *mid-inference*: the last core down parks the shared clock, and the next submit runs
at 200 MHz until it ramps back.

The clock lever then quietly undoes itself on exactly the workloads it exists for. Sampling
`scmi_clk_npu` through **one** Whisper `base.en` encode at the stock 50 ms:

```
200 200 200 200 200 600 600 600 200 600 600 600 600 600   (MHz)
```

and that encode is *slower than the CPU running the same work*. Encode time on an RK1 at
600 MHz, 4 reps, against a CPU reference of 1628–1639 ms:

| `autosuspend_delay_ms` | encode (ms) |
|---|---|
| 50 (stock) | 1881 / 1734 / 1884 / 1809 — variable, **slower than CPU** |
| 100 | 1275 / 1383 / 1279 / 1286 |
| 200 | 1275 / 1272 / 1262 / 1265 |
| 500 | 1259 / 1254 / 1263 / 1269 |
| **1000** (default) | 1267 / 1253 / 1254 / 1263 |
| 2000 | 1273 / 1270 / 1264 / 1256 |

It saturates by ~200 ms. The default is **1000 ms**: that knee plus headroom for a large
encoder, whose packing gaps are several times longer, since they scale with the weight bytes
and `base.en` is the small end of the range. Park-at-idle is preserved — only its deadline
moves — and `rocket_autosuspend_ms=0` restores the PM core's own default.

This lives in `081` rather than a patch of its own because **the park is introduced here**:
stock mainline never re-rates the clock, so without the lever there is nothing to park and
nothing to fix.

If you are benchmarking and see a bimodal distribution, check
`sudo cat /sys/kernel/debug/clk/clk_summary | grep scmi_clk_npu` — and raise
`autosuspend_delay_ms` rather than pinning the domain on.

## BO IOVA-allocator use-after-free fix (`084-rocket-drv-fix-bo-mm-uaf.patch`)

**This is a correctness fix, not a tuning knob, and it is an upstream candidate.**

Stock `rocket` keeps each context's IOVA allocator (`struct drm_mm mm` + `mm_lock`) in
`struct rocket_file_priv` and tears it down (`drm_mm_takedown`) and frees the struct in
`rocket_postclose`. But a BO's IOVA node is removed only in its GEM `.free` path, and a
job's BO references are dropped asynchronously by the drm_sched free worker
(`rocket_job_free` → `rocket_job_cleanup`), which can run after the owning file is closed.
A just-completed job therefore keeps a BO alive past `postclose`; the later `bo_free` then
calls `drm_mm_remove_node()` / `mutex_lock()` on the already-`kfree`d `rocket_file_priv` —
a use-after-free, first visible as a `drm_mm_takedown` "allocator still has nodes"
`WARNING` (`drivers/gpu/drm/drm_mm.c:965`).

It reproduces with the FFN block (three `NT=3` multicore matmuls open/submit/close many
short-lived worker fds): on RK3588 a single `ffn_rocket` run emitted up to 8 such warnings.
The race is timing-dependent (it needs the fd close to win against the free worker), so it
is intermittent — which is an argument for taking the fix, not against.

The fix moves `mm`/`mm_lock` into `struct rocket_iommu_domain`, which is already
reference-counted and held by the file, by every mapped BO, and (while attached) by the
core. The allocator is initialized in `rocket_iommu_domain_create` and torn down in
`rocket_iommu_domain_destroy` (last reference) — so a late `bo_free` removes its node via
`bo->domain->mm`, a domain it still holds a reference to, and the takedown runs only when
the allocator is provably empty.

Independent of every other patch — applies to a pristine tree on its own.

---

# Optional

## NPU rail voltage coupling (`082-rocket-drv-npu-volt.patch`)

Requires `081`.

**At ≤600 MHz this patch does nothing.** `npu_uv_for_hz(600 MHz)` returns the 0.80 V floor,
which is where the rail already sits. Take it only if you intend to explore above 600 MHz;
skip it and the clock lever is complete and correct on its own.

What it buys you above that: the NPU rail (`vdd_npu_s0`, the RK8602 PMIC at i2c `0x42`,
0.55–0.95 V) is never scaled by anything — BL31/firmware sets only the PLL, never the
voltage — so if Linux doesn't couple V to f, nothing does. At ≤600 MHz that is fine (the
rail sits at 0.80 V, above the ~0.70 V that rate needs). Past that it isn't: 900 MHz needs
the voltage and got none, which is the V/f-marginal state that hard-locked the box. This
patch makes raising the clock safe by construction, so >600 MHz later is a config change
rather than new code.

Driver-only — the regulator is already wired in the mainline RK1 device tree
(`npu-supply = <&vdd_npu_s0>` on all three core nodes). Key points:

- **Hold the regulator for the device lifetime** (`devm_regulator_get_optional` at core
  init). The framework aggregates live consumers by max; a `regulator_put` drops our vote.
  We never `regulator_enable()` — the rail is `pd_npu`'s domain-supply, enabled by the
  genpd; we only adjust voltage and read it back to confirm.
- **Ordering:** voltage up before clock up (resume); clock down before voltage down (suspend).
- **0.80 V floor** (`uv = max(vendor_uv(hz), 800000)`). Voting the floor pins the rail at
  today's voltage at ≤600 MHz and prevents our own vote from ever undervolting the shared
  rail. Vendor f→V map: 300–700 → 0.70 V, 800 → 0.75 V, 900 → 0.80 V, 1000 → 0.85 V.
- **The voltage path is per core and deliberately *not* refcounted** the way the shared
  clock's park is. The rail only falls once every core has lowered its own request — which
  is exactly what max-aggregation gives. Gating it on the last core down would strand the
  earlier cores' requests high and the rail would never come down at all.
- **`rocket_npu_uv`** — module parameter, sysfs-writable, default `0` (vendor map + floor).
  A microvolt override for future >600 MHz bring-up only.

**Do not raise `rocket_npu_clk_hz` above 600 MHz** just because this patch is applied. At
900 MHz the speedup was zero, and staggered multi-core resume already powers a domain on at
a rate a sibling raised, so that exposure is pre-existing. This patch makes the lever safe
to pull; it does not pull it. A >600 MHz sweep must also watch temps (no auto-throttle) and
ideally capture this chip's OTP PVTPLL `max` first.

## IOMMU keep-attached (`083-rocket-drv-iommu-keepattach.patch`)

Stock `rocket` attaches the job's per-context IOMMU domain in `rocket_job_run()` and
detaches it again on every completion and reset. Each toggle drives the rk_iommu
stall/force-reset/paging handshake. On RK3568 that handshake times out on the idle NPU MMU
(the bug the upstream RFC fixes); on RK3588 it completes but still costs latency on every
submitted `drm_sched` job.

This is a backport of **"[RFC PATCH v4 5/9] accel: rocket: Keep the IOMMU domain attached
across jobs"** (Midgy BALON, linux-rockchip 2026-06-13). It tracks the attached domain in
`struct rocket_core` (`attached_domain`), re-attaches only when a job from a **different
context** runs, holds a `kref` so the domain outlives the job that first attached it, and
detaches only at core teardown / after a reset (a reset wipes the IOMMU page-table base, so
the domain is dropped to force re-attach). It touches `rocket_core.{c,h}` and
`rocket_job.c`.

**Measured on RK3588 @600 MHz:** −20 µs/submit (~38%), median 54→34 µs
(`rocket-userspace/tests/submit_overhead_rocket.c`), cross-checked at ~17–18 µs/job by
`multicore_probe`; flat on single-job prefill (the cost is per submit, not per task). A
per-job dispatch-floor win for small/many-submit workloads (decode GEMV, detection
convs/1×1s, multi-fd, cross-op batch).

> NOTE vs upstream: our in-tree `rocket_reset()` detaches inside the `job_lock` scoped_guard
> (before `rocket_core_reset()`) rather than after it; the keep-attached drop is applied in
> place there. Functionally equivalent for the bookkeeping.

Independent — applies to a pristine tree on its own.

## Batched submit (`085-rocket-drv-batched-submit.patch`)

Requires `083`.

Stock `rocket` submits one NPU task per HW kick: `rocket_job_hw_submit()` programs one
task's regcmd, sets `PC_TASK_CON.TASK_NUMBER(1)`, and the IRQ handler re-arms the next task
on every completion. A matmul tiled into many output tiles pays one submit + one completion
IRQ + one waiter wakeup per tile — the dispatch-floor cost on the prefill matmul.

The RK3588 PC can stream a contiguous run of self-chaining regcmds from a single kick. The
patch selects that per job via a uAPI flag (`DRM_ROCKET_JOB_BATCHED` in
`drm_rocket_job.flags`): a flagged job with more than one task has `TASK_NUMBER` set to the
task count and `next_task_idx` advanced to the end, so the stock IRQ-handler retire path
fires the job's fence on the single `TASK_NUMBER`-gated completion IRQ instead of re-arming
per task. An unflagged job keeps the stock per-task path. The module param
`rocket_batch_submit` (0644, default 1) is a master kill-switch over the flag. The `flags`
field is appended past the original `drm_rocket_job`, copied `min(job_struct_size, sizeof)`
+ zero-fill, so a stock userspace is unaffected. Install the matching uAPI header
([`uapi/rocket_accel.h`](uapi/rocket_accel.h)) where both the kernel build and userspace see it.

**This is the kernel half only.** It requires the userspace regcmd-chaining pass that lays a
flagged job's tasks contiguously and links each task's trailer — `rocket-userspace` with
`ROCKET_BATCH_SUBMIT=1`, which sets the flag exactly on the jobs it self-chained. **Enable
both halves together**: a flagged job with a stock gapped layout runs task 0, streams into
the gap, and times out (recoverable — drm_sched resets the core). Because the flag is
per-job, chained fp16 and gapped int8/int4/prepacked jobs coexist in one process.

Conversely, a userspace that *uses* the flag against a kernel **without** this patch gets
`-EINVAL` on submit — which is why the `rocket-userspace` suite's
`matmul_int8_dequant_rocket` fails on any kernel lacking `085`, stock included.

Measured (RK1, 600 MHz, `performance`, `matmul_tiled_rocket 512 3840 4096`, 320 tiles → 5
batches of 64): the profiled `wait` term drops ~62 → ~48 ms and GFLOP/s rises ~94 → ~96
(best 99) warm, bit-exact (cosine 1.000000) vs the per-task path. Full mechanism and the
disproven kernel-only descriptor-table model: [`BATCHED_SUBMIT_FINDINGS.md`](BATCHED_SUBMIT_FINDINGS.md).

---

## NPU IRQ affinity (runtime knob, no patch)

A companion runtime lever — no driver change, but in the same dispatch-floor family as the
keep-attached patch. RK3588 is little.BIG: cpu0–3 are Cortex-A55 (little), cpu4–7 are
Cortex-A76 (big). The NPU completion IRQs default to the all-CPUs mask `0-7`, and GICv3
delivers a level interrupt to the lowest CPU in the mask = cpu0, an A55 little core — so the
completion ISR and the wake of the blocked waiter both run at 1.8 GHz, and the per-submit
dispatch floor is ~51 µs.

Binding the three NPU IRQs to a big core cuts that to ~33 µs (−40%); also pinning the
submitting/waiting thread to the same big core gets ~27 µs (−47% vs default). Pinning the app
alone, while the IRQ still lands on the A55, is a no-op — the IRQ binding is the prerequisite.
Like the keep-attached patch it pays on many-small-submit paths and is flat on a single big
prefill job; the two stack.

The IRQ GIC numbers are SoC/kernel-specific — read `/proc/interrupts` rather than assuming (on
kernel 7.1 they were 69/70/71 for the three `.npu` nodes). A helper is provided:

```sh
# latency / single-stream (one-camera detection, decode): all 3 NPU IRQs -> cpu7
sudo rocket-userspace/tools/npu_set_irq_affinity.sh latency 7
taskset -c 7 <app>                                        # run the app on the same core

# throughput / multi-fd pool: spread the 3 IRQs across 3 big cores, pin each worker
sudo rocket-userspace/tools/npu_set_irq_affinity.sh throughput  # 69->5 70->6 71->7

sudo rocket-userspace/tools/npu_set_irq_affinity.sh reset       # back to 0-7
sudo rocket-userspace/tools/npu_set_irq_affinity.sh show
```

This is not persistent across reboot — wire `latency`/`throughput` into a systemd unit or an
`rc.local`/`udev` hook (the IRQs exist as soon as `rocket` probes). If `irqbalance` is running
it may re-migrate the IRQ; pin after it or mask these IRQs in its config.

## Build

The patches are mbox-format and apply with `git am --3way` (or `patch -p1`) against the
`rocket` driver in your kernel source tree. Apply the ones you want in ascending numeric
order:

```sh
cd <kernel-tree>
git am --3way /path/to/rocket-patches/081-rocket-drv-npu-clk.patch          # required
git am --3way /path/to/rocket-patches/082-rocket-drv-npu-volt.patch         # optional; needs 081
git am --3way /path/to/rocket-patches/083-rocket-drv-iommu-keepattach.patch # optional
git am --3way /path/to/rocket-patches/084-rocket-drv-fix-bo-mm-uaf.patch    # required (UAF fix)
git am --3way /path/to/rocket-patches/085-rocket-drv-batched-submit.patch   # optional; needs 083
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- drivers/accel/rocket/rocket.ko
```

Copy `rocket.ko` to the device and reload it (`rmmod rocket && insmod rocket.ko …`); its
vermagic must match the running kernel. To return to stock, reload the distribution module or
reboot.

Because the driver's includes are all public, it also builds **out of tree** against the
running kernel's headers package — a ~8-second rebuild loop on the board itself, which is how
the matrix above was produced:

```make
obj-m := rocket.o
rocket-y := rocket_core.o rocket_device.o rocket_drv.o rocket_gem.o rocket_job.o
KDIR ?= /lib/modules/$(shell uname -r)/build
all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules
```

You need the driver *source* as well as the headers (it is an in-tree driver), and the headers
package supplies `Module.symvers` — hence correct vermagic and symbol CRCs, hence the module
actually loads. The `"compiler differs from the one used to build the kernel"` and `pahole`
warnings are benign when the kernel was cross-built and you are building natively.

## Portability to other RK3588 boards and kernels

These patch the SoC-level `rocket` driver (`drivers/accel/rocket/`), not the board device tree
or the kernel image, so on any RK3588 board running the same mainline `rocket` (developed
against v7.1) they apply to identical source and rebuild the same way. The one hard requirement
is that the board runs the mainline `rocket` driver and that the module is rebuilt against the
exact running kernel.

The two board-dependent pieces both degrade gracefully:

- **Voltage coupling** needs `npu-supply` wired to the NPU rail in that board's device tree (the
  Turing RK1 has `npu-supply = <&vdd_npu_s0>` on all three core nodes). A board that does not
  wire it falls back to clock-only — and at ≤600 MHz `082` is a no-op regardless.
- **IRQ affinity** uses GIC interrupt numbers that are kernel/SoC-specific — read
  `/proc/interrupts` for the three `.npu` nodes rather than assuming.

A different mainline version may have moved the `rocket` driver and require rebasing; each patch
is small and self-contained, so a rebase is mechanical — but re-verify on that kernel, since the
bit-exact and dispatch-floor figures here were measured on v7.1 at 600 MHz.

## License

**GPL-2.0-only**, per the [`LICENSE`](LICENSE) file. These patches modify the mainline `rocket`
DRM-accel driver (`drivers/accel/rocket/`), which is `GPL-2.0-only` (© Tomeu Vizoso /
Collabora), and the Linux kernel is GPL-2.0-only — so the patches and the
Collabora), and the Linux kernel is GPL-2.0-only — so the patches are derivative kernel work and
carry the same `GPL-2.0-only` identifier. `083-rocket-drv-iommu-keepattach.patch` is a backport of
an upstream RFC (Midgy BALON, linux-rockchip) and retains that authorship.

This is intentionally GPL-2.0-only, not the `GPL-3.0-or-later` used by the userspace projects in
this stack: those never link kernel code, whereas GPL-3.0 is incompatible with the GPL-2.0-only
kernel, so kernel patches must stay GPL-2.0.
