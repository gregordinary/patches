<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# kernel graft (media-accel)

The kernel half of the media-accel stack: hardware **decode**, **encode**, and 2D
**scale/CSC/blend** on mainline Linux, without the vendor BSP kernel. The parent
[`../README.md`](../README.md) indexes what each patch enables; this covers how the
series fits together.

## Series (apply order)

1. `030-v4l2-mem2mem-parallel-jobs.patch`
2. `041-rkvdec-rcb-correct-size.patch`
3. `042-rkvdec-remove-unused-need-reset.patch`
4. `043-v4l2-export-max-parallel-jobs.patch`
5. `044-rkvdec-split-core-and-master.patch`
6. `045-rkvdec-multicore.patch`
7. `050-av1-iommu-v14-curated.patch` (kernels `<7.2`)
8. `060-vepu580-rcawston-v3.patch`
9. `070-rga-multicore-vendor-oot.patch`
10. `071-rga-multicore-fixups.patch`
11. `072-rk3588-rga-dts-7.1.patch` (kernels `<7.2`) / `072-rk3588-rga-dts-7.2.patch` (kernels `>=7.2`)
12. `073-rkvdec-hevc-bound-tile-counts.patch`
13. `074-rkvdec-av1-bound-tile-counts.patch`
14. `075-hantro-fix-run-fail-cleanup.patch`
15. `076-rkvdec-align-10bit-bytesperline-64.patch`
16. `077-rkvdec-vp9-rename-get-ref-buf.patch`
17. `078-rkvdec-vp9-move-to-common-file.patch`
18. `079-rkvdec-vdpu381-vp9.patch`
19. `080-rkvdec-vdpu381-vp9-multicore-fixups.patch`
20. `081-hantro-align-nv15-bytesperline-64.patch`

The order is the series' list, not the filename prefixes; the prefixes only make the
directory read the same way. The gaps between them are deliberate room to slot a patch
in without renumbering a committed series.

## Provenance, and what that implies

The patches here come from different places, and they age differently:

- **`030` / `041`–`045` — two unmerged upstream series, vendored whole.** `041`–`045` are
  Detlev Casanova's rkvdec multi-core v2 series, one file per posted patch, and `030` is
  the one patch of Pengutronix's RGA multi-core series they depend on:
  `v4l2_m2m_set_max_parallel_jobs()`, which no released kernel has. Each file names its
  patchwork series and msgid in the region after `---`. The pair retires as a pair — when
  a kernel arrives carrying the merged rkvdec series, all six are deleted rather than
  rebased, because `030` and `043` exist only for as long as the symbol is unexported.
  `030` is the one with reach beyond this driver: it rewrites five functions in
  `v4l2-mem2mem.c` for every m2m driver in the image, defaulting them to the
  one-job-at-a-time behaviour they have today.
- **`050` — absorbed by mainline at 7.2.** The AV1 helper library and the Verisilicon
  IOMMU, taken from an upstream posting series and curated to apply as one. 7.2 carries
  the binding, the driver, the RK3588 DT node and the MAINTAINERS entry, so the patch is
  ranged `<7.2` and simply drops out above that — a patch that stops applying because
  the code is already there is an upper bound on its range, not a break. What did *not*
  land is its trailing `CONFIG_VSI_IOMMU=m` defconfig line, so a 7.2 consumer sets that
  symbol from a kconfig fragment or gets no AV1 IOMMU at all.
- **`060` — out-of-tree by its author's intent.** The VEPU580 encoder is an OOT driver on
  the MPP framework, explicitly not offered for upstream, so it does not retire on its
  own — it stays until something replaces it.
- **`070` / `071` — vendor code, built against mainline.** The multicore RGA driver is
  vendor OOT, and `071` is the delta that keeps it compiling as kernel APIs move. This
  pair is where a kernel bump most often lands: `070` is the driver, `071` tracks the
  kernel, so a rework normally touches `071` alone. 7.2 removing `strncpy()` outright is
  the shape that takes — one call site, one line in `071`.
- **`072` — our own devicetree wiring, one file per kernel generation.** The RGA driver
  in `070` is SoC-neutral, so each SoC points its own nodes at it; this is the RK3588's,
  and it is the one patch here that reads differently on 7.1 and 7.2. 7.1 describes only
  the RGA2 core, so `-7.1` replaces that node and adds the two RGA3 cores and their
  IOMMUs. 7.2 describes all three for its own in-tree V4L2 RGA3 driver, so `-7.2` leaves
  the nodes where they are and changes two properties each: the compatible, which selects
  this driver and distinguishes the two RGA3 cores, and the register window, which has to
  span the 4 KiB the driver needs to reach the core's IOMMU block at `+0xf00`.
- **`073` — our own defect fix, upstream-shaped.** A missing bound on the HEVC tile
  counts, which index fixed-size arrays in all three rkvdec HEVC backends and are not
  range-checked by the V4L2 core. It is not Rockchip-configuration work and carries no
  vendor code, so unlike everything above it is offerable to mainline as-is; it retires
  when an equivalent bound lands there. See the patch header for the reachability
  argument.
- **`074` — the AV1 half of `073`.** The same missing bound in the Hantro/VPU981 stateless
  AV1 decoder: unchecked u8 tile counts index fixed-size arrays, divide
  `context_update_tile_id`, and overflow a 128-record scratch buffer, none of it caught by
  the V4L2 core. Same offer-to-mainline status as `073`, and it maps onto patches 6-7 of
  upstream's "bound stateless HEVC/AV1 tile counts" series. One bound differs in kind from
  `073`: the per-tile-count caps are the AV1 maxima and refuse nothing conforming, but the
  total-tile cap is this hardware's scratch size (128), below the uAPI's 512, so a
  conforming >128-tile frame this core cannot decode is refused. See the patch header.
- **`075` — upstream core fix, carried for `074`.** Sascha Hauer's fix for the hantro
  `->run()` error paths (patch 1/7 of the unmerged rk3588-jpegdec series). The base
  kernel double-finishes a failed decode and mis-arms the watchdog, which `074`'s rejection
  path rides; `075` gives `hantro_end_prepare_run()` an error argument so the rejection is
  clean. Carried unmodified and retires when it merges. The JPEG decoder the rest of that
  series adds is not carried — nothing in our userspace can drive a hardware JPEG decoder.

- **`076` — our own patch, for a consumer upstream does not have.** The packed 10-bit
  capture formats carry five bytes per four samples, so NV15 and NV20 get a stride of
  `width * 5 / 4`, which a 64-aligned width does not make a multiple of 64 — 1920 gives
  2400. The decoder does not care, since every stride register takes `bytesperline / 16`.
  The RGA 2D engine does: it rejects a blit whose source stride is not 64-byte aligned,
  and it is the only route from a packed 10-bit decoded frame to any other pixel format.
  Without this every width that is not a multiple of 256 loses hardware scale, convert and
  blend entirely — 1080p 10-bit among them, while 4K survives only because `3840 * 5 / 4`
  lands on 4800. The same hunk was posted as 4/4 of the VP9 series below and dropped in
  review on its own terms: the decoder was measured byte-identical with and without it.
  That finding stands; it is the RGA requirement that makes the padding worth having.
- **`077`–`079` — an unmerged upstream series, vendored whole.** Venkata Atchuta
  Bheemeswara Sarma Darbha's "media: rkvdec: Add VP9 support for VDPU381 variant" v1,
  one file per posted patch, each naming its msgid after the `---`. It is the last decode
  this SoC was missing: VP9 profile 0 and profile 2, so 8-bit `NV12` and 10-bit `NV15`,
  up to 7680x4320. The posting scores 224/305 on Fluster VP9-TEST-VECTORS across three
  RK3588 boards and three kernel bases; the failures are frames below 64x64, mid-stream
  resolution changes on a non-keyframe, 4:2:2 and 4:4:4 (which the hardware cannot do and
  the profile control refuses), and seven not yet root-caused. Patch 4/4 of the posting is
  not carried — `076` makes that change for a different reason. Retires on merge rather
  than being rebased.
- **`080` — the delta that makes `079` build here, as `071` is for `070`.** The multi-core
  series moved the register window, the AXI clock and the watchdog out of `struct
  rkvdec_dev` into `struct rkvdec_core`, so the vendored VP9 backend does not compile
  against this tree. It takes all four from `ctx->core`, exactly as the VDPU381 H.264 and
  HEVC backends already do, and uses the `rkvdec_schedule_watchdog()` helper `045` factored
  out instead of the backend's open-coded copy. Kept separate so `079` stays byte-identical
  to the posting and retires with it.
- **`081` — the AV1 half of `076`.** AV1 decodes on Hantro/VPU981, a separate driver
  `076` does not reach, and its NV15 capture stride has the same shape: `width * 5 / 4`,
  a multiple of 64 only when the width is a multiple of 256, so 1920 gets 2400 and the
  RGA refuses the blit while 4K survives on 4800. The pad sits in `hantro_try_fmt()`
  after `v4l2_fill_pixfmt_mp()`, scoped to plain `V4L2_PIX_FMT_NV15` — a fourcc that
  appears in exactly one format list in the whole verisilicon driver, the VPU981
  postprocessor's, so the i.MX8M, Sunxi and STM32 variants (`NV15_4L4`, `P010_4L4`) are
  untouched. The hardware honours the padded stride: the postprocessor programs its
  output strides and chroma offset straight from `bytesperline`. The internal reference
  buffers never leave the decoder, so the reference-frame path
  (`hantro_set_reference_frames_format()`) keeps its own geometry.

Anything NPU-shaped lives in the sibling [`../../rocket/`](../../rocket/) scope, which
has its own dependency notes.

## Which kernels these apply to

Not recorded here — it moves on a different clock than this list does, and a copy would
drift. The `series` file that selects these patches carries the kernel window, per patch
where they differ. What has actually been *validated*, on which board, is generated from
the shipped locks rather than written down.

License: GPL-2.0-only (kernel code); see `LICENSES/` at the repo root.
