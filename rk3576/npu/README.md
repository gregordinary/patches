<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# rk3576/npu -- RK3576 NPU (rocket) enablement series

The kernel series that binds the mainline in-tree `rocket` DRM-accel driver to the
RK3576 NPU. Consumed by the `rk3576-npu` patch series (`series/rk3576-npu.toml`).

## Origin

Patches 0008 to 0029 are ours; 0001-0007 come from the linux-rockchip RFC
series **"accel/rocket: RK3576 NPU (RKNN) enablement"** by Jiaxing Hu
(`gahing@gahingwoo.com`), with their upstream commit messages and `Signed-off-by` lines
intact. They compose after `rk3576-fixes` on the RK3576 kernel and apply with
`git am --3way`.

| file | upstream subject |
|------|------------------|
| 0001 | dt-bindings: npu: rockchip: add `rockchip,rk3576-rknn-core` |
| 0002 | pmdomain/rockchip: add optional per-domain power-on settle delay |
| 0003 | pmdomain/rockchip: cycle optional power-domain resets on power-on |
| 0004 | iommu/rockchip: take all DT clocks |
| 0005 | iommu/rockchip: clear stale page faults before enabling stall |
| 0006 | accel/rocket: add RK3576 NPU (RKNN) support |
| 0007 | arm64: dts: rockchip: rk3576: add NPU (RKNN) nodes |
| 0008 | accel/rocket: program PC_TASK_CON with the per-SoC TASK_NUMBER width (ours) |
| 0009 | accel/rocket: make the RK3576 completion poll period tunable (ours) |
| 0010 | accel/rocket: complete jobs that never reach the hardware (ours) |
| 0011 | accel/rocket: keep the IOMMU domain attached across jobs (ours) |
| 0012 | accel/rocket: retire an RK3576 job on the DPU's completion, not on `PC_DONE` (ours) |
| 0013 | accel/rocket: take the job's IOMMU domain before it can be rejected (ours) |
| 0014 | accel/rocket: anchor the IOVA allocator to the IOMMU domain (ours) |
| 0015 | accel/rocket: make the submit ioctl's uAPI structs extensible (ours) |
| 0016 | accel/rocket: run a job's tasks in one HW kick (ours) |
| 0017 | accel/rocket: let userspace name a job's completion class (ours) |
| 0018 | accel/rocket: put the IOMMU domain when `create_bo` fails (ours) |
| 0019 | accel/rocket: free the scheduler list the file allocated (ours) |
| 0020 | accel/rocket: do not free the task array twice (ours) |
| 0021 | accel/rocket: retire an RK3576 pooling job on the PPU's completion (ours) |
| 0022 | accel/rocket: retire an RK3576 job on the hardware's own account of the kick (ours) |
| 0023 | accel/rocket: do cache maintenance over named ranges of a BO (ours) |
| 0024 | accel/rocket: do not let a stale poll retire the next job (ours) |
| 0025 | accel/rocket: wait for the writing block, not for a deadline (ours) |
| 0026 | accel/rocket: reset the cores this SoC acquired (ours) |
| 0027 | accel/rocket: stop the block under the job lock (ours) |
| 0028 | accel/rocket: do not touch a gated core from an idle poll (ours) |
| 0029 | accel/rocket: sequence the completion poll against scheduler teardown (ours) |

Four of the carried patches have local changes; the rest are verbatim.

**0001 describes the RK3576 node shape.** The upstream schema is written for the
RK3588 and constrains every countable property to the RK3588's count, so the RK3576
nodes 0007 adds -- five register banks, six clocks, two power domains, one reset --
validate against nothing. The shared properties carry the union of the two parts and
an `allOf` pins the exact shape per compatible, including `power-domains` at exactly
two for `rockchip,rk3576-rknn-core`. That is where an RK3576 node listing one domain
is caught: `dtbs_check` names the property, at build time.

**0006 attaches whatever `power-domains` the node lists**, rather than deciding from
the SoC it matched. See "The power domains are not a board's job" below.

**0002 is rebased over `rk3576-fixes`.** That series and this one both extend
`drivers/pmdomain/rockchip/pm-domains.c` (the `DOMAIN_RK3576` macro signature and
the RK3576 power-domain table) -- one adds a `.need_regulator` argument, the other a
`.delay_us` argument. Because the resolver always orders `rk3576-fixes` first (it is
a SoC-wide kernel series) and `rk3576-npu` second (a board-opt-in device series), the
merged macro/table carry both arguments (GPU `regulator = true`, the NPU domains
`delay = 15`). Re-derive it the same way on a future RFC re-sync.

**0007 carries three local changes**, all on `rknn_core_1` and all invisible in any
booting configuration, because the RFC ships both cores `disabled` and neither its board
patch nor the H96 MAX M9's enables core 1 (see
[What a board `.dts` must do](#what-a-board-dts-must-do-and-why)). They stay because a
dtsi describes the part whether or not a board opts in, and an inaccurate node is a trap
for whoever enables it next.

**Core 1 sits at `0x27708000`.** The two cores are the two halves of one 64 KB `RKNN TOP`
window: the RK3576 TRM v1.2 address map has `RKNN TOP` at `0x27700000` (64 KB) and the
unrelated `RKNN_NSP` at `0x27710000` (64 KB), and the vendor DT covers the NPU as
`reg = <0x27700000 0x8000>, <0x27708000 0x8000>` with `npu0_irq`/`npu1_irq`. The stride
is `0x8000`, not the RK3588's `0x10000`. Each core's five banks sit at the same offsets
within its half -- pc `+0x0000`, cna `+0x1000`, core `+0x3000`, dpu `+0x4000`, dpu_rdma
`+0x5000` -- with its IOMMU at `+0x2000`, which is why `rknn_mmu_1` at `0x2770a000` was
already right while the core banks were not. Measured with core 1's clocks forced on
[HW sweep], `0x27708000` reads the same version word as core 0 (`0x46495245` /
`0x00010002`, the `VERSION + (VERSION_NUM & 0xffff)` the driver prints as 1179210311),
and `0x27709000`/`0x2770b000` match `0x27701000`/`0x27703000`. `0x27710000` holds
boot-dependent noise, so a core pointed there binds, reads a plausible version, and
times out on every job while writing PC registers into the NPU MCU subsystem.

**Core 1 needs the CBUF clocks.** `rocket` asks every core for all six RK3576 clocks --
`rocket_soc_data.num_clks` is 6 and `rocket_core_init()` takes them with
`devm_clk_bulk_get()`, which is a hard get, decided per SoC and not per node. The RFC
lists only four on core 1, so that core cannot probe:

```
rocket 27708000.npu: error -ENOENT: Failed to get clk 'aclk_cbuf'
rocket 27708000.npu: probe with driver rocket failed with error -2
```

`ACLK_RKNN_CBUF` and `HCLK_RKNN_CBUF` are single clocks in the CRU, not per-core --
there is no `..._CBUF1` -- and core 1's own `rknn_mmu_1` and `power-domain@RK3576_PD_NPU1`
already list both, as does `rknn_core_0`. Adding them to the core node makes it
consistent with the three, and the clock framework reference-counts the sharing.

**Both core nodes list both power domains.** See
[What a board `.dts` must do](#what-a-board-dts-must-do-and-why) for why a single entry
fails, and why this belongs to the SoC and not to each board.

## What 0026 to 0029 fix: four defects in the driver's own bookkeeping

None of these is a property of the RK3576. They are places where the driver's
accounting of its own state is wrong, three of them in code this series added and one
of them in code the RK3588 shares (`rocket/090` is 0027 for that SoC).

**0026 -- resetting a line that was never acquired.** `rocket_core_init()` takes the
reset bulk with `soc->num_resets`, which is 1 here and 2 on the RK3588, while
`rocket_core_reset()` still walked `ARRAY_SIZE(core->resets)`. `struct rocket_core` is
`kzalloc`'d and the reset core accepts a `NULL` `rstc`, so the extra entry has been a
pair of no-ops. It stops being harmless as soon as the missing reset is real: `srst_h`
is not absent on this part, it is driven from the NPU power domain by 0003, and a
reset that silently covers one of two lines reads identically in the source to one
that covers one of one.

**0027 -- the block stopped from outside the lock.** `rocket_job_handle_irq()` wrote
`OPERATION_ENABLE = 0` and `INTERRUPT_CLEAR` before taking `job_lock`, while
`rocket_job_hw_submit()` writes `OPERATION_ENABLE = 1` from inside it. A completion's
zero landing after a submit's one stops a task the hardware has only just been given,
and the job then waits out a completion for a program that is no longer running. On
this SoC the two writers are genuinely concurrent contexts -- the completion arrives
through the poll work queued from the hrtimer, and the shared interrupt thread still
handles the DMA-error bits -- so either can be inside `hw_submit()` while the other is
between its two writes. Both writes are now inside the existing `scoped_guard`.

**0028 -- an idle poll writing to a gated core.** The two writes above are an
*acknowledgement* for an interrupt and nothing at all for the poll, which sampled a
status register and has no hardware condition to end. When the submit the work was
queued for has already been retired -- by the DMA-error thread, or by a reset -- and
nothing was submitted behind it, there is no block left to stop; and that retire ran
`pm_runtime_put_autosuspend()`, so the core may be runtime-suspended by the time the
work is scheduled and the writes reach an NPU whose clocks are gated. 0024's
`poll_seq` does not cover this case: it only advances in `rocket_job_hw_submit()`, so
it catches a retire *followed by another submit* and not a retire with nothing behind
it, where the work still looks current. `in_flight_job` is the state that separates
them, so the poll now returns early when the core is idle. The interrupt path is
unchanged -- its condition is level-triggered and `INTERRUPT_CLEAR` is what ends it,
so it clears whatever is in flight, including nothing.

**0029 -- a cancel that does not hold.** `rocket_job_fini()` cancelled the poll timer
and its work and *then* called `drm_sched_fini()`. In that order the cancel does not
stick: a job still in flight retires through the poll, `rocket_job_handle_irq()`
re-arms the next task, and `rocket_job_hw_submit()` starts the timer again behind the
cancel that already ran -- into a scheduler `drm_sched_fini()` is in the middle of
tearing down. The two operations are not interchangeable and both are needed, so they
are sequenced: a `poll_dying` flag stops the poll **acting** before the scheduler
goes, and the timer and work are **cancelled** after, at a point where nothing can
re-arm them. The flag is deliberately not a one-way latch. `struct rocket_core` is
allocated for the device rather than for a binding, so it outlives an unbind whenever
another core keeps the device alive, and a core that came back would otherwise carry
the dying flag from its previous teardown and never retire a job again;
`rocket_job_init()` clears it. On a single-core RK3576 -- the supported configuration
-- every unbind is the last one and the array is reallocated, which is why no test on
this hardware reaches it.

## What 0010 fixes, and why it is not RK3576-specific

`rocket_job_run()` takes a runtime-PM reference and then attaches the IOMMU group.
Both of its early returns leave the job half-started, and each of the two leaks that
follow is enough on its own to pin the NPU runtime-active for good:

- **The usage counter ratchets.** `pm_runtime_get_sync()` keeps its reference when the
  resume fails, and the IOMMU path has already resumed successfully; neither is put.
  `core->in_flight_job` is still `NULL`, so the completion path in
  `rocket_job_handle_irq()` and the timeout path in `rocket_reset()` — both of which
  only put when a job is in flight — never balance it.
- **The scheduler never gets its credit back.** The fence handed to the scheduler is
  one nothing will ever signal, so the job stays pending, and
  `rocket_device_runtime_suspend()` returns `-EBUSY` for as long as a credit is
  outstanding.

The usage-counter half **outlives the driver**: the reference is on the platform
device, which the OF core owns, so `rmmod`/`insmod` does not clear it — the freshly
probed device comes back `active` and stays there until a reboot. That is the state
in which the NPU's int32 output path silently writes nothing (its hazard is cleared
by the power domain cycling, which a device that cannot suspend never does) while
every other workload still passes, so it reads as a compute bug rather than a
machine state.

Measured on an H96 MAX M9 by failing the attach on one job [HW sweep]. Before, a
single failure became self-sustaining — the job timed out at 500 ms, `rocket_reset()`
called `rocket_core_reset()`, which takes the IOMMU down (`MMU_DTE_ADDR is not
functioning`), so the next attach failed for real: one injected failure produced
seven and leaked seven references, and the device then stayed `active` across a
module reload. After, the injected failure is the only one — the job is retired with
its error, the caller's next submit attaches and computes normally, no timeout fires,
and the device suspends.

The error paths are identical on the RK3588, which reaches them through the same
`rocket_core_reset()`; the patch is in this series only because this is the part that
wedges often enough to notice.

## What 0009, 0011 and 0012 are for: the per-submit floor, and what actually bounds it

`PC_DONE` is read-only in `INTERRUPT_MASK` on this SoC, so 0006 polls it on an hrtimer
where the RK3588 takes an interrupt. A submit cannot complete faster than one poll, so
that period is the per-submit floor and every RK3576 cost table is a submit-count table
because of it. 0009 makes the period a module parameter; the shipped default is
**50 us** (`poll_interval_us`).

**What bounds it is not the poll rate.** `PC_DONE` means the program counter finished,
not that the DPU's write DMA has drained, and the driver both tears down the IOMMU
mapping and signals the fence as soon as it sees `PC_DONE`. A long period hides that
behind its own detection lag. Two independent failures come out from under it, and
they need separate fixes:

- **The per-job IOMMU detach races the write drain.** Detaching on completion faults
  writes still in flight, which leaves a *write* page fault active
  (`RK_MMU_STATUS` `0x2b` = `PAGING_ENABLED|PAGE_FAULT_ACTIVE|IDLE|PAGE_FAULT_IS_WRITE`);
  that blocks the next `enable_stall`, which the IOMMU reports as a timeout
  (`0x1d` shows `STALL_ACTIVE` — the stall arrived, just after the poll gave up), and
  the NPU raises `DMA_READ_ERROR|DMA_WRITE_ERROR`. **0011 removes this outright** by
  keeping the domain attached across jobs.
- **The fence is signalled before the writes are visible.** 0011 does not touch this
  one; **0012 removes it** by retiring the job on the DPU's own completion — the
  `DPU_0`/`DPU_1` bits in `INTERRUPT_RAW_STATUS`, which is what the RK3588 takes its
  interrupt on — instead of on `PC_DONE`.

Measured on an H96 MAX M9 with the int8 matmul gate, one run per point [HW sweep]:

| poll period | 1000 us | 500 us | 250 us | 150 us |
|---|---|---|---|---|
| detach per job | 43/43 | 43/43 | 40/43 | 37/43 |
| &nbsp;&nbsp;stall timeouts / DMA errors | 0 / 0 | 0 / 0 | 0 / 4 | 4 / 13 |
| domain kept attached (0011) | 43/43 | 43/43 | 42/43 | 40/43 |
| &nbsp;&nbsp;stall timeouts / DMA errors | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |

### What 0012 retires on, and why the wait is bounded

`INTERRUPT_RAW_STATUS` carries the signal that has drained: the `DPU_0`/`DPU_1` bits set
tens to hundreds of microseconds *after* `PC_DONE`. Logged at the moment the poll retires
[HW sweep]:

```
raw@retire=0x20000001 -> 0x30000155 after 82us
```

— `PC_DONE_1` with no DPU bit, then `DPU_0` (`0x100`).

**Not every task raises one**, so the wait is bounded by `dpu_grace_us`, at the 500 us
this patch defaults it to, past which the job retires anyway. (What that wait *starts*
from is 0022's subject: `PC_DONE` alone is a per-task signal, and on a batched job it is
not the kick being over. Whether that wait is bounded at all is 0025's: the shipped
default is 0, which waits for the block.) A task whose DPU
output element is wider than one byte — the int32 writer, and fp16 output — completes CNA
and CORE and never sets a DPU bit at all (`raw=0x30000055`, held for milliseconds). The
500 us default is twice what those tasks need: they still fail at 100 us (3 of 43 shapes)
and pass at 250, 500, 1000 and 2000, and a longer grace only costs them wall time (the
fp16 stem in the cost table is 13.5 ms at 500 us against 15.8 at 1000).

With the DPU bit as the retire condition the poll period goes to 50 us. Per-submit cost of
an 8x64x32 int8 matmul, n=300, us/submit [HW sweep]:

| poll period | 500 us | 250 us | 125 us | 50 us | 25 us |
|---|---|---|---|---|---|
| retire on `PC_DONE` | 1065 | 731\* | 535\* | 433\* | — |
| retire on the DPU bit | 1073 | 739 | 558 | 439 | 398 |

\* *incorrect* — retiring on `PC_DONE`, the largest output surface in the int8 matmul gate
fails 3 runs of 3 at 250 us with a **partial** surface (32x2048x3072, ~95-98k of 98304
elements exact, the missing bytes a contiguous tail), and 7 of 43 shapes fail at 50 us.

At the shipped defaults that is **2.42x** on every submit (1065 -> 439 us/submit), and the
correctness is the point: retiring on the DPU bit, the gate is 43/43 at both 250 and 50 us,
five runs in a row, and the full RK3576 gate set is clean at 50 us — convolution 102/102,
the convolution library 154 + 8 refused, the int8 matmul gate 43/43 through both int32
writers, the refusal gate 15/15, both fp16 sweeps, and nothing in `dmesg`.

Whole-entry cost, NPU milliseconds [HW sweep]:

| entry | 500 us / `PC_DONE` | 50 us / DPU bit |
|---|---|---|
| int8 stem 3x224x224 k3s2 oc32 | 4.00 | 3.33 |
| conv 32->64 k1 56x56 | 3.12 | 1.90 |
| conv 256->512 k1 14x14 | 3.78 | 2.46 |
| dw 256 k3 14x14 | 1.42 | 1.00 |
| matmul 32x1024x1024 | 5.83 | 4.74 |
| matmul 32x1024x2560 | 13.37 | 11.17 |

The RK3588 takes a completion interrupt and does not poll, so none of this reaches it.

Patch 8 of the RFC (`arm64: dts: rockchip: rk3576-rock-4d: enable NPU`) is **not**
carried: the board enable is board-specific. See below for what a board owes.

## What 0013 and 0014 fix: two crashes reachable from group `render`

`/dev/accel/accel0` is group `render`, so both of these are available to any client
that may use the NPU at all. Neither is RK3576-specific -- the code is mainline's and
the RK3576 enablement does not touch it; the RK3588 series carries the same two as 084
and 085. They are here because this is the series the RK3576 image applies, and that
image does not take the RK3588 series.

**0013: the job reaches its destructor without its domain.** `rocket_job_cleanup()`
puts `rjob->domain` unconditionally, but `rocket_ioctl_submit_job()` assigns it only
after the task copy and both BO lookups have succeeded. Five rejection sites unwind
through the cleanup ahead of that assignment -- a `task_struct_size` below
`sizeof(struct drm_rocket_task)`, an unreadable `tasks` pointer, `regcmd_count == 0`,
and either BO handle not existing -- so a malformed submit reaches
`rocket_iommu_domain_put(NULL)`, which dereferences the NULL to get at the kref:

```
Internal error: Oops: 0000000096000004 [#1]  SMP
pc : rocket_iommu_domain_put+0x50/0xac [rocket]
lr : rocket_job_cleanup+0x20/0x23c [rocket]
```

Taking the reference at construction removes the window rather than teaching the put
to tolerate NULL, so the next field added to that struct does not reopen it. The get
cannot fail: `rocket_open()` creates the file's domain before the file is usable.
Verified with `tests/uapi_submit_errpath_rocket` in rocket-userspace, one malformed
submit per site from a forked child: 0 of 5 sites returned to userspace before, 5 of 5
after.

**0014: the IOVA allocator outlives its owner.** The allocator (`drm_mm` + `mm_lock`)
lived in `rocket_file_priv` and was torn down in `rocket_postclose()`, but a BO's IOVA
node is removed in its GEM free path -- and a job's BO references are dropped by the
`drm_sched` free worker, which can run *after* the owning file has closed. A client
that submits and closes without waiting therefore removes a node through a freed
`rocket_file_priv`. Anchoring the allocator to `rocket_iommu_domain` fixes it by
lifetime: that object is refcounted and held by the file, by every mapped BO and by
the attached core, so the node removal always runs against a live allocator and the
takedown runs only once it is provably empty.

Measured on an H96 MAX M9, 16 iterations of open / allocate / submit /
close-without-waiting: 16 of 16 produced

```
WARNING: drivers/gpu/drm/drm_mm.c:965 at drm_mm_takedown+0x28/0x38
 rocket_postclose [rocket]
```

before and 0 of 16 after, on the same kernel with only this patch changed. Left running
longer, the same client reaches the dereference through the free worker:

```
Internal error: Oops: 0000000096000004 [#1] SMP
Workqueue: 27700000.npu drm_sched_free_job_work
pc : add_hole+0x34/0x15c
lr : drm_mm_remove_node+0x1e8/0x380
 rocket_gem_bo_free [rocket]
 rocket_job_cleanup [rocket]
```

## What 0015, 0016 and 0017 are: the submit uAPI, and two levers over the floor

0009-0012 got the per-submit floor to 439 us and made it correct. 0016 and 0017 attack
what is left, and both need userspace to opt in per job -- so 0015 comes first to make
that possible at all.

**0015 makes the submit descriptors actually extensible.** `drm_rocket_submit` carries
`@job_struct_size` and `drm_rocket_job` carries `@task_struct_size` precisely so the
descriptors can grow, but both checks compared against the kernel's own `sizeof()`, so
the day either struct gained a field every already-built userspace would start failing
SUBMIT with `-EINVAL`. The copy had the mirror-image bug: `copy_from_user()` always read
the kernel's `sizeof()`, so a *newer* userspace had fields it did set silently ignored --
a flag nobody read, running a job with semantics nobody asked for. 0015 guards on the v1
`offsetofend()` minimum and copies with `copy_struct_from_user()`, which zero-fills what
the caller did not supply and returns `-E2BIG` for a trailing field this kernel does not
know. It also reports a rejected job (`rocket_ioctl_submit()` discarded the per-job
return, so SUBMIT answered 0 for a job it dropped and every per-job check was
unobservable) and declares interface version **1.0**. No ABI change: nothing grows here,
and for today's layouts the v1 minimum equals the current `sizeof()`.

**0016 runs a job's tasks in one HW kick.** Stock rocket programs one task per kick and
re-arms the next from the completion path -- and on this part that completion is a poll,
so a convolution a row window splits into n tasks pays the 439 us floor n times. The PC
can instead stream a contiguous run of self-chaining regcmds from a single kick:
program task 0, set `TASK_NUMBER = n`, and one completion gates the lot. That is per job
via `DRM_ROCKET_JOB_BATCHED`, not a module param, because the flag asserts something
about *that job's regcmd layout* -- userspace laid it out contiguously and rewrote each
trailer to link to the next. A flagged job whose regcmds are not contiguous runs task 0
and times out, recoverably. `rocket_batch_submit` (default 1) is only a master kill
switch over the flag. The task-count bound comes from `rocket_soc_data.task_number_bits`
(0008), not the RK3588 field mask.

Measured on an H96 MAX M9: the convolution library gate is 156 passed / 7 refused as
required / 0 failed, unchanged from the one-submit-per-task path -- which is the
correctness bar. A 32x32 k3 plane forced into 8 row windows goes **2.8 ms -> 1.0 ms**,
and a 224x224 k3 s1 convolution **21.3 ms -> 19.1 ms**.

**0017 splits `dpu_grace_us` in two.** The same number serves two waits that want
different values. For an ordinary job the DPU completion arrives tens to hundreds of
microseconds after `PC_DONE`, so the grace is a **deadline** on a wait that usually wins
early -- it has to cover the slowest drain (the int8 stem at 224 k7 s2 reaches 745 us;
n-tiled-deep fails 10 of 10 at 200 us and none at 250), and lowering it hands back a
surface whose tail has not landed. For a job whose DPU output element is wider than one
byte that completion never arrives, so the same number is a pure **blind settle** paid in
full on every submit. The driver cannot tell the classes apart at `PC_DONE` time;
userspace can, because the class follows from the register program it just emitted.
`DRM_ROCKET_JOB_NO_DPU_DONE` names it and selects `dpu_blind_us` (**250 us**, the measured
pass point -- such tasks fail at 100 us and pass from 250, so raise rather than lower it).
Advisory by construction: a completion that does arrive still retires the job, so a wrong
hint costs time and never correctness.

Measured on an fp16 convolution at ic=128 oc=32 28x28 k3, eight wide-output slices:

| `dpu_blind_us` | 250 (the hint) | 500 (neutralised) | 3000 |
|---|---|---|---|
| whole entry | 13.67 ms | 15.20 ms | 36.96 ms |

**10%** off that path with every narrow-output job keeping its full 500 us deadline --
which is the point, since lowering the shared number to collect the same win is exactly
what the drain measurements rule out.

**Version the interface, do not sniff it.** `drm_driver` left `.major`/`.minor` unset, so
stock rocket reports 0.0.0 and userspace had no way to ask what the kernel guarantees.
0015 claims **1.0** (extensible descriptors), 0016 **1.1** (`BATCHED`), 0017 **1.2**
(`NO_DPU_DONE`), 0021 **1.3** (`PPU_DONE`), 0022 **1.4** (a batched stream's length
bounded by nothing but the scheduler's job timeout), 0023 **1.5**
(`PREP_BO_RANGES`/`FINI_BO_RANGES`) and 0025 **1.6** (one program's length bounded the
same way). Userspace must check `DRM_IOCTL_VERSION` before
self-chaining: an older kernel does not know the flag and would run a chained layout
down the per-task path. 1.4 and 1.6 are the bumps that add no uAPI -- what each names is
a bound, which a caller that sizes its programs has no other way to ask about.

## What 0018, 0019 and 0020 fix: two leaks and a double free

Three more defects on the same footing as 0013 and 0014 -- mainline code the RK3576
enablement does not touch, reachable by any client that may open `/dev/accel/accel0`
at all. The RK3588 reaches all three; it carries 0020 as `rocket/087`.

**0018: a failed `CREATE_BO` leaks the file's IOMMU domain.**
`rocket_ioctl_create_bo()` takes a domain reference as soon as it has an object, and
each of its four error paths then frees that object with
`drm_gem_shmem_object_free()`. That helper calls `drm_gem_shmem_free()` directly, so
it never reaches the object's own `.free` handler -- `rocket_gem_bo_free()`, the only
place the driver drops that reference. Every failing create therefore leaks one
`struct rocket_iommu_domain` reference, and the first of the four paths is a plain
large allocation. With 0014 the cost is larger than one struct: the leaked reference
keeps the IOVA allocator, its `drm_mm` and every mapping it still holds alive for the
life of the module. The put goes where the reference is taken rather than in the error
paths, because `rocket_gem_bo_free()` would also `iommu_unmap()` and
`drm_mm_remove_node()` a node those paths have either not inserted or already removed.

**0019: the scheduler array is freed through a pointer the entity does not keep.**
`rocket_job_open()` allocates a `num_cores`-entry array and hands it to
`drm_sched_entity_init()`; `rocket_job_close()` then frees `entity->sched_list`. The
entity stores that pointer only when `num_sched_list > 1`, and
`drm_sched_entity_select_rq()` clears it again once the entity settles on a runqueue.
On a single-core device -- the supported RK3576 configuration -- the close path is
`kfree(NULL)` and the array leaks on every open/close cycle, bounded by nothing. The
driver keeps its own pointer and frees that after `drm_sched_entity_destroy()`, since
the array belongs to the file rather than to the entity. Two smaller defects on the
same path go with it: the `kmalloc` return was unchecked, and the
`drm_sched_entity_init()` failure path leaked the array.

**0020: the submit ioctl's task array is freed twice.** `rocket_copy_tasks()` frees
`rjob->tasks` on its `fail:` path without clearing the pointer, and its only caller
unwinds through `rocket_job_cleanup()`, which frees `job->tasks` unconditionally. Two
failures reach it and both are ordinary userspace input: the task copy fails, or
`task.regcmd_count` is 0 -- which is what a userspace whose register-program generator
refused a shape submits if it submits anyway. It is an unprivileged double free of a
kmalloc-16 object, one of the hottest caches in the kernel, so the damage surfaces far
from here in whatever allocates next. It was chased through two faults that are not
defects in the paths they appeared in: a scatterlist walked in
`drm_gem_shmem_release()` from `rocket_gem_bo_free()`, and a wild `phys_to_virt()`
inside `swiotlb_bounce()` reached from `drm_gem_shmem_get_pages_sgt()`. With
`slub_debug=FZPU slab_nomerge` it is deterministic -- two

```
BUG kmalloc-16: Object already free
```

reports per run of a client that submits rejected jobs, naming
`rocket_ioctl_submit()` as the allocation site and `rocket_job_cleanup()` as the
second free -- before, and zero after, over ten rounds of the full gate sequence on a
clean boot. The fix gives the array a single owner: `rocket_job_cleanup()` already
frees it on every path out of the ioctl, so the `fail:` path only has to stop.

## What 0021 is for: the class of job that raises no DPU completion at all

0012 retires a job on the DPU's own completion bits, and 0017 carves out the jobs whose
DPU completion never arrives because the output element is wider than one byte. A
**pooling** program is a third case, and a sharper one: it has no DPU stage to complete.

`PC_OPERATION_ENABLE` is a per-block bitmap, and the two programs enable disjoint bit
sets -- a convolution `0x1d` (CNA/CORE/DPU/DPU_RDMA), a pool `0x60` (PPU/PPU_RDMA). So
`DPU_0`/`DPU_1` in `INTERRUPT_RAW_STATUS` can never set for a pool, and every pooling
job runs out the `dpu_grace_us` deadline instead -- paid in full, on every submit. It
shows up as the wait tracking the parameter count for count, which is what says the job
is not retiring on a completion at all. Measured through the userspace pooling entry on
an H96 MAX M9, waiting on the output BO of a 1024x7x7 k7 average pool [HW sweep]:

| `dpu_grace_us` | 500 | 250 | 100 |
|---|---|---|---|
| wait | 641 us | 389 us | 213 us |

On MobileNetV1-224 that is 640 us of the pooling layer's 1.48 ms, and 640 us of the
whole 6.2 ms inference.

The PPU raises its own completion in the same register, two bits over, so the fix is to
wait on that instead when the job's last program is a PPU program. Which class a job is
in is invisible to the driver at `PC_DONE` time and fully visible to userspace, which
wrote `PC_OPERATION_ENABLE` -- so it is a per-job flag, `DRM_ROCKET_JOB_PPU_DONE`, the
same shape 0017 uses. It selects the PPU bits in place of the DPU ones, latched per
submit into `core->poll_done_mask` so the poll callback still needs no job pointer.

Two things bound the flag. Setting it on a job whose last program is a convolution costs
that job the grace period and nothing else, since the PPU bit simply never sets. But a
job that **mixes** DPU and PPU programs in one chained stream must not set it: an
interior program's PPU bit would retire the job while a later DPU write was still
draining. The flag names the *last* program's class, and the uapi says so.

The interface version goes to **1.3**, so userspace can tell a kernel that honors the
flag from one that ignores it -- and ignoring it is the old behavior, correct and slower.
The RK3588 takes a completion interrupt rather than polling, so it is untouched.

## What 0022 is for: where a kick actually is

0012 and 0021 pick *which* block's completion retires a job. 0022 is about when the wait
for it begins -- because neither signal that wait was anchored on says what it was taken
to say.

`PC_OPERATION_ENABLE` is a self-clearing GO bit. The vendor `rknpu` driver writes 1 and
then 0 to it back to back (`rknpu_job.c`) [source-confirmed], and on this part it reads
back zero at every poll of a kick that is still running [HW sweep]. So
`OPERATION_ENABLE == 0` -- the first half of the completion condition -- has been
**vacuously true** since the polled path was written, and every wait under it therefore
started at the *first poll tick* rather than at an event.

`PC_DONE`, the other half, is raised **per task and not per kick**. 0016 made a job of n
tasks go out as one hardware kick, and the two `PC_DONE` bits alternate as the program
counter retires each program of it. Traced at a 10 us poll over the 35-program cross-layer
kick of a MobileNetV1-224 [HW sweep]:

```
t=   12 us   PC_DONE_0 set, task 1 of 35 in flight
t=   31 us   PC_DONE_1 set
 ...         the two alternating, once per task, for 1.9 ms
t= 1911 us   task 35 of 35 in flight
t= 1921 us   the DPU's own completion, and the kick is over
```

So `dpu_grace_us`, measured as one task's drain, was being asked to cover a whole stream.
Past it the driver signals the fence while the core is still writing: the caller reads a
surface that never landed, and the next job is programmed into a core that has not
finished the last one. From userspace that looks like a hardware bound on how long a
chained stream may be, with a magic number in a shipping path. It is not one.

**Scaling the deadline by the task count is not enough.** It is a fitted number, and a
stream whose programs each take longer than `dpu_grace_us` outruns it. A 224x224 k3
convolution at 256 channels row-splits into 75 chained tasks; scaled, the deadline expires
at 37.5 ms -- 75 x 500 us to the microsecond -- with the task counter reading 67 tasks
started of 75 [HW sweep].

The part says exactly where a kick is. `PC_TASK_STATUS` -- the register the vendor driver
reads as its "task counter", and whose offset it carries per SoC, `0x3c` on RK3588 and
`0x48` here [source-confirmed] -- is a live pair of counters, both modulo the programmed
`TASK_NUMBER` [HW sweep, kicks of 2, 3, 5, 8, 9, 35 and 90 tasks]:

| bits | counter |
|---|---|
| 15:0 | tasks started |
| 31:16 | tasks completed |

Mid-stream at least one of the two is non-zero: the started count wraps to zero only as
the last task starts, and the completed count only once every task has retired. So **with
a `PC_DONE` in hand -- something has retired -- a zero register means the whole kick is
over**, and nothing else does.

The wait is held off until then. `dpu_grace_us` is once again the drain it was measured
as, applied to the program that wrote last, and how long a batched stream may be stops
being a property of the driver's timing. A job of one task programs `TASK_NUMBER = 1`,
where both counters read zero always, so `PC_DONE` alone is its signal and its wait does
not change.

This also closes a race the old condition had: the per-task block completion bits do not
persist across tasks [HW sweep], so a poll landing while a *mid-stream* task's DPU bit was
still set would retire the job early -- rare enough never to have been caught, and
indistinguishable from a correct retire when it happened.

What bounds a job that raises no `PC_DONE` at all is a **backstop** rather than a
deadline: it retires after a quarter of the scheduler's job timeout, with the reason
logged. It is not a tuning knob, and no job in the gates reaches it -- 29 gates, zero
backstop hits, which is also what says every class raises `PC_DONE`, the pooling and
LUT-load programs included.

Measured on an H96 MAX M9 [HW sweep]:

- the 224x224 k3 convolution above is **bit-exact**, 150 programs in one kick, where a
  deadline scaled by the task count retires it at t=37554 us and the layer computes wrong
  ("a row task wrote nothing over 8 attempts");
- 29 gates pass, the kernel taint word unmoved at every one;
- MobileNetV1-224 is 6.5 ms per inference in 5 submits and 40 tasks, and the per-submit
  floor is a median of 348 us -- **both unchanged**, because the DPU's own completion is
  when the wait ends either way and this patch only changes when it begins.

The interface version goes to **1.4** with no uAPI change: the bump exists because only
from here is a batched stream's length bounded by nothing but the scheduler's job timeout,
and a caller that sizes its streams has no other way to tell. The RK3588 takes a
completion interrupt rather than polling, so the polled path is not code it runs.

## What 0023 adds: cache maintenance over named ranges of a BO

`PREP_BO` and `FINI_BO` sync the **whole object** -- `dma_sync_sgtable_for_cpu()` and
`dma_sync_sgtable_for_device()` over the BO's entire scatterlist -- so a bracket costs what
the buffer *is* rather than what the caller touches. For a client that reads or writes a
few bytes of a large surface that is the whole cost.

Measured on core 0 over a ladder of BO sizes from 4 KiB to 4 MiB, 400 iterations each,
taking the median [HW sweep]:

| call | floor | per KiB |
|---|---|---|
| a rejected ioctl (syscall + drm dispatch + GEM lookup) | 1.17 us | -- |
| `PREP_BO` | 1.33 us | 0.0969 us |
| `FINI_BO` | 1.11 us | 0.0970 us |
| one `PREP_BO`/`FINI_BO` pair | 2.24 us | 0.1939 us |

That is about **10.6 GB/s of cache walk per direction** against a per-ioctl floor of
roughly a microsecond, so past a few KiB the ioctl is entirely the walk. The floor is not
what needs removing; the bytes are.

The user of this in the rocket-userspace library is a **write guard**. A job whose DPU
output element is wider than one byte leaves the *next* submit -- of any kind, across
processes -- completing normally and writing nothing, so before each submit userspace
stamps a sentinel into every output surface and afterwards asks whether each one changed.
The check reads a handful of bytes per task, but it has to bracket the whole buffer to
read them. On four quantized classifiers run as one hardware kick each [HW sweep]:

| graph | wall | guard | brackets | bytes bracketed |
|---|---|---|---|---|
| MobileNetV1 | 4.9 ms | 1.99 ms | 30 | 4.8 MiB |
| MobileNetV2 | 6.7 ms | 3.23 ms | 85 | 8.4 MiB |
| ResNet-18 | 9.3 ms | 2.67 ms | 51 | 7.9 MiB |
| Inception V1 | 10.8 ms | 4.58 ms | 122 | 14.1 MiB |

-- 27% to 48% of wall. Brackets times the measured floor is 0.07 to 0.27 ms of that, 4% to
7%: the rest is bytes that nobody reads.

So name the bytes. `DRM_ROCKET_PREP_BO_RANGES` and `DRM_ROCKET_FINI_BO_RANGES` take an
array of `{offset, size}` and sync only those. **A `range_count` of zero means the whole
object**, so the new ioctls are supersets of the old ones and the old ones are untouched.

Ranges must be **ascending, non-overlapping and inside the BO**, and there is a cap on how
many. That is a contract rather than a convenience: each range walks the scatterlist, so
an unsorted array would cost O(ranges x segments) in the kernel on a caller's say-so. All
three are checked and refused with `-EINVAL`.

**One implementation note.** A sync must cover a *physically* contiguous span, because the
arch back-end resolves the DMA address to a physical one and then walks the cache over
`[phys, phys + size)`. A scatterlist segment is contiguous in the DMA address space, which
for an IOMMU-mapped object says nothing about the pages under it -- so a range is split at
page boundaries, which is safe because mapping is page granular and the offset within a
page is therefore the same on both sides. (`dma_sync_sgtable_*()` is safe for the same
reason from the other end: it works from the CPU-side segments, whose lengths are
physical.)

With the library's guard narrowed to one cache line per (task, channel group) and its
brackets taken as ranges [HW sweep]:

| graph | wall | guard |
|---|---|---|
| MobileNetV1 | 4.9 -> **3.6 ms** | 1.99 -> **0.59 ms** |
| MobileNetV2 | 6.7 -> **4.7 ms** | 3.23 -> **1.09 ms** |
| ResNet-18 | 9.3 -> **7.2 ms** | 2.67 -> **0.69 ms** |
| Inception V1 | 10.8 -> **7.7 ms** | 4.58 -> **1.47 ms** |

-- on the same submits and tasks, every layer still bit-exact against a CPU model of the
part's arithmetic and every graph still returning TFLite's own top-1. The residual guard
is well above brackets-times-floor because a range costs per-range kernel work of its own;
the ranges are 64 bytes each and there are tens per bracket.

The new entry points take a user pointer, a count and offsets from an unprivileged caller
(`/dev/accel/accel0` is group `render`), so their refusals are gated rather than assumed:
17 malformations x {`PREP`, `FINI`}, all 34 returning to userspace with the documented
errno.

The interface version goes to **1.5**. Nothing here is RK3576-specific -- `rocket_gem.c`
is the shared GEM path -- but it is carried in this series, and `rk3588-accel` does not
take it.

## What 0024 fixes: a poll that retires a job it was not queued for

The completion poll 0012 introduced queues a work item from the hrtimer and retires the
job from there. That work is not the only path into `rocket_job_handle_irq()`: the shared
interrupt still reports the DMA-error bits, and its thread retires whatever job is in
flight.

So the two cross. The timer queues the work for job N; the DMA-error thread runs first and
retires job N; drm_sched pushes job N+1, which `rocket_job_hw_submit()` starts; and the
work, still queued from before, retires job N+1 as well -- signalling its fence while the
hardware is still running it and handing the caller a surface that was never written. A
reset does the same, since it retires the in-flight job before `cancel_work_sync()` reaches
the queued work.

The fix is a sequence number per submit, snapshotted onto the work when the timer queues
it and compared before anything is retired. A work item that no longer matches belongs to
a job somebody else has already finished, and returns without touching the current one.

The window is narrow -- it needs a DMA error or a reset inside one poll period -- but
neither is rare on this part while a driver or an encoder is under development, and the
failure it produces is a silently short surface rather than an error. No uAPI change, so
the interface version stays at **1.5**.

RK3588 does not reach this: it has a completion interrupt and `soc->poll_completion` is
false, so no poll work is ever queued. `rk3588-accel` therefore does not carry it.

This is equivalent to the `poll_seq`/`poll_work_seq` fix in RFC v4 4/6 of the upstream
RK3576 series, rewritten against this series, where the poll condition and the per-submit
state it latches have both moved.

## What 0025 fixes: a deadline standing in for one program's execution

0022 bounded how long a batched *stream* may be by the hardware's own account of the kick.
What it left is the other half: how long **one program** may be. Past the kick the driver
waited `dpu_grace_us` for the writing block's completion, and that bound falls on the last
program of the kick. `PC_DONE` does not mean that program has finished computing -- it
means the program counter retired it -- and on a job of one task, where `TASK_NUMBER = 1`
and both `PC_TASK_STATUS` counters read zero always, `PC_DONE` alone is the kick-over
signal, raised while the CNA/CORE/DPU pipeline still has the whole convolution to run. So
the grace was covering an entire program's execution, and every value is too small for
some program.

**From userspace this wore a hardware capacity bound convincingly.** A single 112x112 k7
ic32 convolution is bit-exact up to a plane height that *falls* as the output-channel count
rises -- 78 rows at two output-channel groups, 44 at four, 32 at six -- handing back a
full, correctly sized surface whose last rows of the last output-channel group are wrong.
No "feature + weights <= pool" form states that: one group computes at a total of 6888 CBUF
granules where two groups fail at 5992. What the walls share is **work** -- 766 to 830
MMACs, at a footprint that falls 2.4x across them -- and work is a time. Raising
`dpu_grace_us` to 750 makes all of them bit-exact at unchanged shape, program and CBUF
budget, and restoring 500 brings them back [HW sweep, H96 MAX M9].

Two more measurements say the governing quantity is the program's execution and not the
bytes it writes. At the same plane and a 3x3 kernel -- 5.4x fewer MACs per output element,
and a *larger* surface -- the convolution is bit-exact at every reachable height to 109,
where the output is 1.47 MB against the 516 KB the k7 wall sits at. And at 64 input
channels, where the MACs per output element double and the surface halves, the wall
returns.

So the block raises its own completion; wait for that. `dpu_grace_us` stays as an optional
**cap** and defaults to **0**, which waits for the block. Setting it non-zero restores the
bounded wait, which is what makes the two comparable on one kernel. 0022's backstop moves
out of the kick-unfinished branch to cover this wait too, so a job whose writing block
never completes retires there -- loudly, with the core and the task count logged -- instead
of silently handing back a truncated surface.

**What it costs is paid by a job that raises no completion and does not say so.**
`DRM_ROCKET_JOB_NO_DPU_DONE` and `DRM_ROCKET_JOB_PPU_DONE` are advisory, and a job in the
wrong class used to lose 500 us; it now waits out the backstop. Raising `dpu_grace_us` to
5000 on the old behaviour shows which submits ride it to completion, since those are the
ones whose wall scales with it. Over a 58-gate list [HW sweep, H96 MAX M9] the whole run
moves +1.0% and every graph is inside noise -- MobileNetV1 623 ms either way, Inception V3
65917 -> 65351 ms -- so nothing on the library's own paths rides it: those name their class.
Two probe rows do, 153 -> 535 ms and 214 -> 404 ms, which is ~85 and ~42 grace-bound
submits, and both are raw test submits that pass no class flag at all. With this patch
applied those two rows are 153 -> 10303 ms and 214 -> 4767 ms -- the 125 ms backstop times
the submit counts the grace sweep predicted, to within 10% -- and they stay `rc=0`: the
surfaces are right and the wait is long. Every other row of the 58 is unmoved. The
population that pays is jobs with no flag naming their writer, and the fix for that is to
name it, not to keep a deadline for everyone.

Measured with this patch applied, as an out-of-tree build of the same source against the
board's own headers [HW sweep, H96 MAX M9]:

- the 112x112 k7 ic32 convolution at four output-channel groups is bit-exact at every
  height from 40 to 56, 51 runs, 0 wrong;
- setting `dpu_grace_us = 500` on that same kernel brings the failure back -- 13 of those
  17 heights wrong, 30 runs of 51, the wrong elements a suffix of the rows of the last
  output-channel group as before -- so the two behaviours are one A/B on one binary;
- the unmodified series, rebuilt the same way, is 58 of 58 gates `rc=0` at taint 4096 in
  205 s against the shipped module's 202 s, which is what says the rebuild is faithful
  before anything is read off it.

The interface version goes to **1.6** with no new uAPI: the bump exists because on an
older kernel a single program whose drain outruns `dpu_grace_us` retires with its last
output rows unwritten, silently and looking exactly like a capacity bound, and a caller
that sizes its programs has no other way to tell.

## What a board `.dts` must do, and why

0007 adds the SoC nodes `disabled`; a board enables and wires the ones it wants. For the
H96 MAX M9 that is `devices/h96-max-m9/dts/rk3576-h96-max-m9.dts` in the boot2deb tree,
which enables core 0. The rail below fails in a way that does not point at itself, so
take it together with the rest, not as a menu:

| node | change |
|---|---|
| `&rknn_core_0` (`/soc/npu@27700000`) | `npu-supply = <&vdd_npu_s0>`, `status = "okay"` |
| `&rknn_mmu_0` (`/soc/iommu@27702000`) | `status = "okay"` |
| `&rknn_core_1` (`/soc/npu@27708000`) | none -- left `disabled`, see below |
| `&rknn_mmu_1` (`/soc/iommu@2770a000`) | none -- left `disabled` |
| `&vdd_npu_s0` (PMIC `dcdc-reg2` on this board) | `regulator-always-on` |

**Bind one core.** Two jobs in flight at once compute wrong answers on this part:
96-100% of calls wrong, over as much as 94% of the surface, deltas spanning the whole
int8 range, and it crosses process boundaries. Simultaneous execution is the entire
condition, isolated by driving two fds across both cores through a mutex so only one job
is ever in flight -- every call exact, 96 calls per cell over three shapes -- against the
same two fds run concurrently, which is where the corruption appears. Neither the second
core's existence, nor what a job inherits from its predecessor, nor the IOMMU mapping
discriminates. So a board that binds one core leaves the kernel nowhere to schedule a
concurrent job, and the class is unreachable by construction rather than merely rare.

The shared CBUF is the mechanism this points at, reached by elimination and held as a
candidate rather than a settled cause: there is one 1 MB buffer at `0x3fe80000`, outside
either core's register bank, gated by a single `NPU_GRF_MEMGATE_CON0` field and the one
clock pair both core nodes list, and nothing an encoder emits depends on which core runs
it, so both cores stage into the same region. The vendor driver is the only Rockchip NPU
configuration carrying a `cache_sgt_init`, and what it does is hand each core a disjoint
slice -- core 0 banks {0-6, 14}, core 1 {7-13, 15}. Mainline `rocket` has no such
partitioning, which is what makes this a userspace encoder's job to solve rather than the
kernel's; no RK3576 encoder exists yet, so the second core has no consumer that could use
it correctly today.

The second core buys nothing meanwhile. Measured against a one-core control rather than
against a single fd, two fds on core 0 alone run 1.51-1.69x (8x64x32), 1.62-1.66x
(32x1024x512) and 1.49-1.51x (128x1024x1024); with both cores bound the same shapes give
1.55-1.74x, 1.49-1.68x and 1.38-1.42x -- inside the one-core range at the small shapes and
below it at the large one. The 1.5-1.7x is submit pipelining, not parallelism: one core is
not saturated by one fd at a ~340 us submit floor, so filling its dispatch gaps is the
whole gain, and a single core delivers that on its own.

Prefer the DT over a runtime unbind: leaving the node `disabled` survives kernel package
upgrades, and unbinding does not.

**The power domains are not a board's job.** Both core nodes list both `RK3576_PD_NPU0`
and `RK3576_PD_NPU1` in the SoC dtsi, because the requirement is a property of the part,
not of any board. It matches the vendor, which powers both NPU domains from one node even
on a single core -- the CBUF->CMAC read path is only fully powered with NPU1 up. genpd
reference-counts the two shared domains, so each core holding both costs nothing and keeps
the pair up while either core is active -- which is also why a board that enables core 0
alone still gets a fully fed buffer, core 1's node having no part in it. Each core lists
its own domain first.

`rocket_core_init()` calls `devm_pm_domain_attach_list()` unconditionally and takes the
count from the node, so the DT is the only place that says how many domains a core spans.
The driver core attaches the single-domain case itself and reports `-EEXIST`, which the
driver ignores. A core that lists one domain therefore probes and runs -- with NPU1
unpowered and its convolution buffer half fed -- so the constraint is enforced in the
binding, where `dtbs_check` catches it and names the property.

The IOMMUs need only their `status`: `rk_iommu` does no explicit attach, and one
`power-domains` entry is what the hardware has.

**The rail needs `regulator-always-on` *in addition to* `npu-supply`.** `vdd_npu_s0`
boots enabled but has no consumer, so the regulator core disables it as unused late in
boot (`vdd_npu_s0: disabling`, t~33 s) -- well after a successful probe at t~8 s, which
is why this presents as a runtime failure rather than a boot failure. `npu-supply`
alone does not hold it: mainline `rocket` has no regulator support at all (`rocket.ko`
references no `regulator_*` symbol), so the property is inert and nothing claims the
rail. Set both -- `npu-supply` so a regulator-aware driver can vary the voltage,
`regulator-always-on` so the rail survives under this one.

0008 is required for back-to-back submits, and 0006's commit message predates it:
that message describes only the first operation per power session producing valid
output, which is what 0008 fixes. `PC_TASK_CON`'s `TASK_NUMBER` field is 12 bits
wide on the RK3588 and 16 on the RK3576, so the driver's fixed RK3588 word programs
a task count of 28673 on this part and leaves the PC mid-program. Carrying the width
in `rocket_soc_data` costs the RK3588 nothing -- at 12 bits it still emits `0x7001`,
bit for bit.

## Status

Experimental bring-up / reverse-engineering scaffold. The RK3576 NPU register map is
shifted and re-packed relative to the RK3588, so a register program written for the
RK3588 geometry does not run here; a userspace needs its own RK3576 encoder. With
that in hand the kernel side runs: an int8 convolution encoded for this part is
bit-exact against a CPU model on real silicon (H96 MAX M9), and multi-task
row-windowed programs are bit-exact submitted back to back with no gap. Re-sync from
a later RFC revision by re-splitting the series into this directory and updating
`series/rk3576-npu.toml`.

**0026 to 0029 have not been on hardware.** They are reasoned from the source and
build clean, and none of them changes what a correct submit does: 0026 and 0027 are
the same operations with a different count and a different lock scope, and 0028 and
0029 are paths a passing run does not take. Every measurement above stands, but the
H96 has not been through a run with them applied -- the checks that would close that
are a submit sweep unchanged and an unbind and rebind with a job in flight.
