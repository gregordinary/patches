<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# media-accel

Rockchip hardware **video transcode** stack for mainline Linux (RK35xx / RK3588)
— the out-of-tree patches that enable HW decode, encode, and 2D scaling without
the vendor BSP kernel. Spans three source trees (kernel, ffmpeg, MPP/RGA
userspace); a build series applies the relevant subset.

Validated together on Turing RK1 (RK3588) + mainline kernel 7.1-rc6.

## kernel/ — apply in numeric order

| Patch | What it enables | Target / notes |
|-------|-----------------|----------------|
| `030-v4l2-mem2mem-parallel-jobs.patch` | `v4l2_m2m_set_max_parallel_jobs()`, so one m2m device can run N jobs | Pengutronix RGA series 05/17, unmerged; core change, every m2m driver |
| `041-rkvdec-rcb-correct-size.patch` | Re-sizes the RCB buffer per run, so a resolution change cannot overflow it | rkvdec multi-core v2 1/5 |
| `042-rkvdec-remove-unused-need-reset.patch` | Drops a dead `need_reset` check | rkvdec multi-core v2 2/5 |
| `043-v4l2-export-max-parallel-jobs.patch` | Exports the symbol `045` calls | rkvdec multi-core v2 3/5; placeholder, retires with `030` |
| `044-rkvdec-split-core-and-master.patch` | Splits rkvdec into core and master platform drivers on the component framework | rkvdec multi-core v2 4/5; the restructure `rk3576/104` collides with |
| `045-rkvdec-multicore.patch` | Both VDPU381 **decode** cores behind one `/dev/video0` | rkvdec multi-core v2 5/5; N streams over N cores, no single-stream gain |
| `050-av1-iommu-v14-curated.patch` | AV1 **decode** (AV1 helper lib) + Verisilicon IOMMU | curated v14; kernels `<7.2` — upstream carries the driver from 7.2, but not its defconfig line |
| `060-vepu580-rcawston-v3.patch` | VEPU580 H.264/HEVC hardware **encode** | rcawston OOT driver (MPP framework), RK3588; author "not for upstream" |
| `070-rga-multicore-vendor-oot.patch` | RGA 2D **scale / CSC / blend** via `/dev/rga` | vendor multicore OOT driver (3 cores); the ABI `librga.so` speaks |
| `071-rga-multicore-fixups.patch` | Builds `070` against a mainline kernel | hrtimer_setup, MODULE_IMPORT_NS string, version.h includes, strscpy_pad |
| `072-rk3588-rga-dts-7.1.patch` | RK3588 DT nodes for the three RGA cores | kernels `<7.2`; 7.1 describes only the RGA2 core |
| `072-rk3588-rga-dts-7.2.patch` | Retargets 7.2's own RGA nodes at `070` | kernels `>=7.2`; compatibles + 4 KiB windows |

> NPU (`rocket`) patches live in the sibling [`../rocket/`](../rocket/) scope, not here.

## ffmpeg/ — ffmpeg-rockchip (nyanmisaka lineage)

| File | Purpose |
|------|---------|
| `0001-rkrga-accept-v4l2request-10bit-nv15-nv20.patch` | Let `scale_rkrga` accept v4l2request 10-bit NV15/NV20 frames (the HW-decode → RGA-scale path) |
| `README-libavutil-hwcontext-conflict-resolution.diff` | Reference: nyanmisaka RKMPP `hwcontext` patch, for resolving the libavutil conflict during rebases |

## userspace/ — librockchip-mpp / librga

| File | Purpose |
|------|---------|
| `001-mpp-allocator-dma-heap-mainline-cma.patch` | Point MPP dma-heap allocations at the contiguous `default_cma_region` so VEPU580 encode works on mainline kernels (fixes the encode regression) |

See [`userspace/README.md`](userspace/) for the full rationale and apply steps.
