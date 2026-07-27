<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# rk3576/npu -- RK3576 NPU (rocket) enablement series

The kernel series that binds the mainline in-tree `rocket` DRM-accel driver to the
RK3576 NPU. Consumed by the `rk3576-npu` patch profile (`profiles/rk3576-npu/`).

## Origin

Patches 8, 9 and 10 are ours; patches 1-7 are patches 1-7 of the linux-rockchip RFC v2 series **"accel/rocket:
RK3576 NPU (RKNN) enablement"** by Jiaxing Hu (`gahing@gahingwoo.com`), with their
upstream commit messages and `Signed-off-by` lines intact. They compose after
`rk3576-fixes` on the RK3576 kernel and apply with `git am --3way`.

Patch 0002 is the one non-verbatim file: `rk3576-fixes` and this series both extend
`drivers/pmdomain/rockchip/pm-domains.c` (the `DOMAIN_RK3576` macro signature and
the RK3576 power-domain table) -- one adds a `.need_regulator` argument, the other a
`.delay_us` argument. Because the resolver always orders `rk3576-fixes` first (it is
a SoC-wide kernel profile) and `rk3576-npu` second (a board-opt-in device profile),
0002 is rebased over `rk3576-fixes`: the merged macro/table carry both arguments
(GPU `regulator = true`, the NPU domains `delay = 15`). Re-derive it the same way on
a future RFC re-sync.

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

## What 0009 is for, and why its default changes nothing

`PC_DONE` is read-only in `INTERRUPT_MASK` on this SoC, so 0006 polls it on a 1 ms
hrtimer where the RK3588 takes an interrupt. A submit cannot complete faster than one
poll, so that period is the per-submit floor and every RK3576 cost table is a
submit-count table because of it. Measured on an H96 MAX M9, the floor tracks the
period one for one — 1.2 ms at the stock 1000 us down to 0.32 ms at 150 us, a 3.75x
cut on any submit-bound workload.

It ships as a parameter at the stock default rather than as a shorter default because
the two failures below it are not decoded: at 100 us the IOMMU wedges, and at 200 us a
matmul driving the DPU's 32-bit output writer starts failing while a convolution
workload stays bit-exact. Set `rocket.poll_interval_us=` to spend it.

Patch 8 of the RFC (`arm64: dts: rockchip: rk3576-rock-4d: enable NPU`) is **not**
carried: the board enable is board-specific. See below for what a board owes.

## What a board `.dts` must do, and why

0007 adds the SoC nodes `disabled`; a board enables and wires them. For the H96 MAX M9
that is `devices/h96-max-m9-npu/dts/rk3576-h96-max-m9-npu.dts` in the boot2deb tree.
Five properties, and **two of the three nodes fail in a way that does not point at
itself** -- so take them together, not as a menu:

| node | change |
|---|---|
| `&rknn_core_0` (`/soc/npu@27700000`) | `power-domains` = **both** `RK3576_PD_NPU0` **and** `RK3576_PD_NPU1` |
| `&rknn_core_0` | `npu-supply = <&vdd_npu_s0>` |
| `&rknn_core_0` | `status = "okay"` |
| `&rknn_mmu_0` (`/soc/iommu@27702000`) | `status = "okay"` |
| `&vdd_npu_s0` (PMIC `dcdc-reg2` on this board) | `regulator-always-on` |

**Both power domains, even though `rocket` computes on core 0.** `rocket_core_init()`
attaches with `devm_pm_domain_attach_list()`. A node listing a **single**
`power-domains` entry is auto-attached by the driver core first, so the driver's
explicit attach then fails and there is no `/dev/accel` at all:

```
rocket 27700000.npu: error -EEXIST: failed to attach NPU power domains
rocket 27700000.npu: probe with driver rocket failed with error -17
```

Listing two entries suppresses the auto-attach. It also matches the vendor, which
powers both NPU domains from one node even on a single core -- the CBUF->CMAC read
path is only fully powered with NPU1 up.

**The rail needs `regulator-always-on` *in addition to* `npu-supply`.** `vdd_npu_s0`
boots enabled but has no consumer, so the regulator core disables it as unused late in
boot (`vdd_npu_s0: disabling`, t~33 s) -- well after a successful probe at t~8 s, which
is why this presents as a runtime failure rather than a boot failure. `npu-supply`
alone does not hold it: mainline `rocket` has no regulator support at all (`rocket.ko`
references no `regulator_*` symbol), so the property is inert and nothing claims the
rail. Set both -- `npu-supply` so a regulator-aware driver can vary the voltage,
`regulator-always-on` so the rail survives under this one.

Core 1 (`npu@27710000`) and its IOMMU stay disabled: single-core bring-up, with core 0
holding both domains.

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
`profiles/rk3576-npu`.
