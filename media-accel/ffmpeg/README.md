<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# ffmpeg graft (media-accel)

`ffmpeg-rk` is a hybrid FFmpeg: mainline V4L2-request stateless **decode** from the
Kwiboo base, with Rockchip **rkmpp encode** + **rkrga scale** grafted on from
nyanmisaka. This scope is that graft plus the Vulkan import fix the hybrid needs,
applied with `git am` onto the base.

## Series (apply order)

1. `0001-lavu-add-NV15-and-NV20-bitstream-formats-support.patch`
2. `0002-lsws-add-NV15-and-NV20-formats-support.patch`
3. `0003-lavu-add-RKMPP-hwcontext.patch`
4. `0004-lavf-rkrga-add-RKRGA-scale-vpp-and-overlay-filters.patch`
5. `0005-rkrga-accept-v4l2request-10bit-nv15-nv20.patch`
6. `0006-hwcontext-vulkan-import-multiplanar-drm-frames.patch`

Patches 0001–0004 are the graft; 0005 normalizes the v4l2-request 10-bit planar
tags to the semi-planar NV15/NV20 that `scale_rkrga` expects. 0006 is independent
of the graft — it makes the base's own V4L2-request frames importable into Vulkan.

## 0006 — multi-planar DRM import

`hwcontext_vulkan`'s DRM importer knows only single-component and packed-RGB
fourccs, and uses each layer index as a plane index — the shape VAAPI's
`SEPARATE_LAYERS` export produces. `hwcontext_v4l2request` emits the opposite: one
layer, a composed fourcc, N planes. A Rockchip `rkvdec` CAPTURE buffer therefore
fails `hwmap=derive_device=vulkan` with `Unsupported DMABUF layer format`, for
plain 8-bit NV12, against any Vulkan driver.

0006 adds the multi-planar fourccs to the format table so a driver that can import
them does, and re-describes the descriptor as one single-component layer per plane
when it cannot. `AVDRMPlaneDescriptor` carries an `object_index`, so both layers
reference the same dma-buf at different offsets: no copy, no memory touched.

**It builds only with `--enable-vulkan`**, which `ffmpeg-rk` does not currently
pass; the patch is inert in today's binaries and lands ahead of the build-flag
change. NV15/NV20 stay out of reach — Vulkan has no packed 10-bit 4:2:0 or 4:2:2
format, and neither layout decomposes into `R8`/`GR88` or `R16`/`GR1616`.

Origin is jellyfin-ffmpeg's
`debian/patches/0079-add-fixes-for-vaapi-drm-prime-vulkan-interop.patch`
(nyanmisaka); the patch header records what was and was not carried across.

## Provenance

- **Base:** Kwiboo `FFmpeg` branch `v4l2-request-n8.1`, commit `b57fbbe5`.
- **Graft source:** nyanmisaka `ffmpeg-rockchip` branch `8.1`, commit `f66f2f80`.

Each graft patch's `(cherry picked from commit …)` trailer records the exact
nyanmisaka commit it materializes. The series is `git am`-ready and reproduces the
grafted tree byte-for-byte.

## Why these are patches, not cherry-picks

`0003` (RKMPP hwcontext) does not cherry-pick cleanly onto the Kwiboo base: the base
and nyanmisaka's tree register different sets of `AVHWDeviceType`s, so adding the
rkmpp entry is a 3-way textual conflict in `libavutil/hwcontext.c` / `.h` and
`libavutil/Makefile`. The resolution is mechanical (keep both sets, add rkmpp), but
a pinned cherry-pick cannot reproduce a manual resolution — so the resolved commit
ships as a patch. The other three apply cleanly and ship as patches for uniformity.

## Excluded from the graft

Two nyanmisaka commits match the encode/scale subject keywords but are deliberately
**not** grafted — they displace the working upstream paths mainline needs:

- `d3b5fbba1a157355d965b108f9935949c2654f80` — *lavc/rkmppenc: refactor RKMPP
  encoders*. Bloats the MPP cfg payload; trips the mainline VEPU580 driver
  (`MPP_IOC_CFG_V1` ENOMEM). The simpler upstream rkmppenc already in the base is
  what the mainline driver expects.
- `383893abc6a1a0ae5ad0f2e3927f9649593c3019` — *lavc/rkmppdec: refactor RKMPP
  decoders*. Forces the vendor MPP decode HAL that mainline lacks; breaks the
  working V4L2 stateless decode. Decode stays on the mainline V4L2 path.
