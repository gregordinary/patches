<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# media-accel

Rockchip hardware **video transcode** stack for mainline Linux (RK35xx / RK3588)
— the out-of-tree patches that enable HW decode, encode, and 2D scaling without
the vendor BSP kernel. Spans three source trees (kernel, ffmpeg, MPP/RGA
userspace); a build series applies the relevant subset.

What is validated, on which board and at which pin, is generated — `boot2deb
support-matrix` is the authority, not this file.

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
| `073-rkvdec-hevc-bound-tile-counts.patch` | Bounds the HEVC tile counts a stream can ask for | our own; all three rkvdec HEVC backends; shared with `rk3576-fixes` |
| `074-rkvdec-av1-bound-tile-counts.patch` | The AV1 half of `073`, in Hantro/VPU981 | our own; per-dimension caps are the AV1 maxima, total cap is this core's scratch |
| `075-hantro-fix-run-fail-cleanup.patch` | Makes a failed hantro `->run()` clean up once | upstream rk3588-jpegdec 1/7, unmerged; what `074`'s rejection path rides |
| `076-rkvdec-align-10bit-bytesperline-64.patch` | Pads the 10-bit stride so **RGA can blit `NV15`/`NV20`** | our own; no-op for 8-bit; without it no HW filter path below width 256-multiples |
| `077-rkvdec-vp9-rename-get-ref-buf.patch` | VP9 series 1/4 — rename | upstream VP9-on-VDPU381 v1, unmerged |
| `078-rkvdec-vp9-move-to-common-file.patch` | VP9 series 2/4 — shared helpers | upstream VP9-on-VDPU381 v1, unmerged |
| `079-rkvdec-vdpu381-vp9.patch` | **VP9 decode** on VDPU381: profile 0 (`NV12`) + profile 2 (`NV15`) | upstream VP9-on-VDPU381 v1, unmerged; 224/305 Fluster |
| `080-rkvdec-vdpu381-vp9-multicore-fixups.patch` | Builds `079` against the multi-core restructure | our own; `ctx->core` for regs/clock/watchdog, as `071` is for `070` |

> NPU (`rocket`) patches live in the sibling [`../rocket/`](../rocket/) scope, not here.

## ffmpeg/ — ffmpeg-rockchip (nyanmisaka lineage)

Kwiboo's `v4l2-request` branch with nyanmisaka's RKMPP/RGA work grafted on, then
our fixes. Full per-patch rationale in [`ffmpeg/README.md`](ffmpeg/).

| File | Purpose |
|------|---------|
| `0001`–`0002` | `AV_PIX_FMT_NV15`/`NV20` and the swscale readers for them |
| `0003`–`0004` | RKMPP hwcontext; `scale_rkrga` / `vpp_rkrga` / `overlay_rkrga` |
| `0005` | Tag `NV15`/`NV20` as themselves, so `hwdownload` and `hwmap` work on a 10-bit frame |
| `0006` | Teach `hwcontext_vulkan`'s DRM importer the composed one-layer descriptor v4l2-request emits |
| `0007`–`0013` | Defect fixes in the grafted code (a use-after-free, a SIGFPE, a dropped row tail) |
| `0014`–`0015` | `alphasrc` and `sub2video`, the subtitle burn-in branch |
| `README-libavutil-hwcontext-conflict-resolution.diff` | Reference: nyanmisaka RKMPP `hwcontext` patch, for resolving the libavutil conflict during rebases |

## userspace/ — librockchip-mpp / librga

| File | Purpose |
|------|---------|
| `001-mpp-allocator-dma-heap-mainline-cma.patch` | Point MPP dma-heap allocations at the contiguous `default_cma_region` so VEPU580 encode works on mainline kernels (fixes the encode regression) |

See [`userspace/README.md`](userspace/) for the full rationale and apply steps.
