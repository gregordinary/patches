<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# media-accel

Rockchip hardware **video transcode** stack for mainline Linux (RK35xx / RK3588)
— the out-of-tree patches that enable HW decode, encode, and 2D scaling without
the vendor BSP kernel. Spans three source trees (kernel, ffmpeg, MPP/RGA
userspace); a build profile applies the relevant subset.

Validated together on Turing RK1 (RK3588) + mainline kernel 7.1-rc6. The
`debian-rk1` build repo carries its own copies of these for standalone builds;
this is the canonical home.

## kernel/ — apply in numeric order

| Patch | What it enables | Target / notes |
|-------|-----------------|----------------|
| `040-vdpu381-multicore-v1-curated.patch` | VDPU381 H.264/HEVC stateless **decode**, multi-core | mainline V4L2 stateless; curated v1 |
| `050-av1-iommu-v14-curated.patch` | AV1 **decode** (AV1 helper lib) + Verisilicon IOMMU | curated v14; helper-lib refactor may force re-curation on rebase |
| `060-vepu580-rcawston-v3.patch` | VEPU580 H.264/HEVC hardware **encode** | rcawston OOT driver (MPP framework), RK3588; author "not for upstream" |
| `070-rga-multicore-vendor-oot.patch` | RGA 2D **scale / CSC / blend** via `/dev/rga` | vendor multicore OOT driver (3 cores); the ABI `librga.so` speaks |
| `071-rga-multicore-7.1-fixups.patch` | Forward-port of `070` to kernel 7.1 | hrtimer_setup, MODULE_IMPORT_NS string, version.h includes |

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
