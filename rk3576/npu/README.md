<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# rk3576/npu -- RK3576 NPU (rocket) enablement series

The kernel series that binds the mainline in-tree `rocket` DRM-accel driver to the
RK3576 NPU. Consumed by the `rk3576-npu` patch profile (`profiles/rk3576-npu/`).

## Origin

Patches 1-7 of the linux-rockchip RFC v2 series **"accel/rocket: RK3576 NPU (RKNN)
enablement"** by Jiaxing Hu (`gahing@gahingwoo.com`), with their upstream commit
messages and `Signed-off-by` lines intact. They compose after `rk3576-fixes` on the
RK3576 kernel and apply with `git am --3way`.

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

Patch 8 of the RFC (`arm64: dts: rockchip: rk3576-rock-4d: enable NPU`) is **not**
carried: the board enable is board-specific. A board opts the NPU in through its own
`.dts` -- setting `&rknn_core_0`/`&rknn_mmu_0` to `status = "okay"`, wiring
`npu-supply`, and listing the NPU power domains.

## Status

Experimental bring-up / reverse-engineering scaffold. The RK3576 NPU register map is
shifted and re-packed relative to the RK3588, and the compute datapath does not yet
run inference correctly. Binding the driver exposes the accel uAPI for register
probing; it is not a working accelerator. Re-sync from a later RFC revision by
re-splitting the series into this directory and updating `profiles/rk3576-npu`.
