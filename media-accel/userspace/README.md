<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# media-accel/userspace

Standalone, reusable patches for the userspace HW-accel stack
(librockchip-mpp / librga). Parallels [`../kernel/`](../kernel/).

These are applied to the source clones as part of a build profile; a builder
consumes the profile's `userspace` list in order.

## 001-mpp-allocator-dma-heap-mainline-cma.patch

**Target:** Rockchip MPP (`librockchip_mpp`) — tsukumijima
`mpp-rockchip` `develop` branch (HermanChen lineage), file
`osal/allocator/allocator_dma_heap.c`.

**Problem it fixes:** On a **mainline** Linux kernel (not Rockchip BSP)
with the rcawston VEPU580 encoder driver, `hevc_rkmpp` / `h264_rkmpp`
encode fails — MPP allocates the bitstream output buffer from the
non-contiguous `/dev/dma_heap/system` heap; the rcawston driver does
not coalesce the multi-segment dma-buf into one IOVA range, so its
guardrail rejects the buffer (MPP_IOC_CFG_V1 ENOMEM /
`rkvenc … guardrail … outside iova`), and the HW would IOMMU-fault
even past that. The VEPU580 needs a physically contiguous buffer.

**What it does:** points every `heap_infos[]` entry at the mainline
contiguous CMA heap `/dev/dma_heap/default_cma_region`, so all MPP
dma-heap allocations are physically contiguous.

**Apply:**
```
cd <mpp source tree>           # tsukumijima develop or equivalent
git am   .../001-mpp-allocator-dma-heap-mainline-cma.patch   # or:
patch -p1 < .../001-mpp-allocator-dma-heap-mainline-cma.patch
```

**Scope / caveats:**
- Mainline kernels only. **Revert for Rockchip BSP kernels** (those
  expose the `cma` / `system-dma32` / `system-uncached` heap names the
  upstream table expects).
- MPP here is encode-only on this board (decode uses v4l2request or the
  rkmpp decoder which manages its own buffers); extra CMA pressure is
  modest. 4K under heavy concurrency may want a larger `cma=` bootarg
  (default pool 393 MB).
- Verified: applies cleanly to tsukumijima `develop` @ 750e76ec;
  produces a working `librockchip-mpp1` that encodes 720p HEVC at
  312 fps with a clean dmesg on mainline 7.1 + rcawston VEPU580.

**Community relevance:** anyone running nyanmisaka ffmpeg-rockchip on a
*mainline* RK3588 kernel with the rcawston VEPU580 patch needs this (or
an equivalent CMA-heap fix, or the kernel-side `buffer->size` fix).
