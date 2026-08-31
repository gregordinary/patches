<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# ffmpeg graft (media-accel)

`ffmpeg-rk` is a hybrid FFmpeg: mainline V4L2-request stateless **decode** from the
Kwiboo base, with Rockchip **rkmpp encode** + **rkrga scale** grafted on from
nyanmisaka. This scope carries that graft plus the fixes the hybrid needs, applied
with `git am` onto the base.

## Series (apply order)

1. `0001-lavu-add-NV15-and-NV20-bitstream-formats-support.patch`
2. `0002-lsws-add-NV15-and-NV20-formats-support.patch`
3. `0003-lavu-add-RKMPP-hwcontext.patch`
4. `0004-lavf-rkrga-add-RKRGA-scale-vpp-and-overlay-filters.patch`
5. `0005-hwcontext-v4l2request-tag-nv15-nv20-as-themselves.patch`
6. `0006-hwcontext-vulkan-import-multiplanar-drm-frames.patch`
7. `0007-rkrga-do-not-free-frame-owned-by-ff-filter-frame.patch`
8. `0008-rkrga-clear-output-frame-crop-rectangle.patch`
9. `0009-rkmppenc-validate-drm-descriptor-before-import.patch`
10. `0010-hwcontext-rkmpp-free-mapping-on-non-linear-reject.patch`
11. `0011-hwcontext-rkmpp-honour-device-level-cacheable-flags.patch`
12. `0012-hwcontext-rkmpp-pool-buffer-size-in-size-t.patch`
13. `0013-lsws-nv15-nv20-do-not-drop-row-tail-samples.patch`
14. `0014-lavfi-add-alphasrc-source-video-filter.patch`
15. `0015-lavfi-subtitles-add-sub2video-option.patch`
16. `0016-v4l2request-hevc-set-sps-rps-controls.patch`
17. `0017-v4l2m2m-set-coded-format-before-raw-when-encoding.patch`
18. `0018-v4l2m2m-negotiate-formats-the-driver-offers.patch`
19. `0019-v4l2m2m-add-mjpeg-encoder.patch`

Patches 0001–0004 are the graft; 0005 is a fix to the base, described below.
0006 is independent of the graft — it makes the base's own V4L2-request frames
importable into Vulkan. 0007–0013 are defect fixes in the grafted code. 0014 and
0015 are independent of the graft too: they build the subtitle branch every
hardware burn-in chain needs. 0016 is a fix to the base's HEVC hwaccel,
described below. 0017–0019 are independent of the graft as well: they reach the
mainline VEPU121 JPEG encoder through V4L2, with no vendor userspace involved,
and are described below.

## 0005 — what a 10-bit decoded frame is called

The base tags a packed `V4L2_PIX_FMT_NV15` capture buffer — four 10-bit samples in
five bytes, no padding — with the planar logical `sw_format` `AV_PIX_FMT_YUV420P10`,
and NV20 with `YUV422P10`. Neither describes the buffer: wrong plane count, wrong
sample layout. Upstream ffmpeg has no packed 10-bit pixel format, so the base has
nothing better to say.

Because the tag is wrong, the base has to switch off everything generic that would
act on it. `v4l2request_transfer_get_formats()` blanks its entire format list when
`sw_format` is `YUV420P`, `YUV420P10` or `YUV422P10`, and `v4l2request_map_from()`
returns `ENOSYS` for the same three. So `hwdownload` of a 10-bit frame fails for
*every* target format, not one, and `hwmap` fails outright. On RK3588 that is every
10-bit HEVC, VP9 and AV1 frame the stateless decoder produces — which is why HDR
content has to fall back to software decode to reach a tone mapper, at 26x the CPU
for a twelfth of the throughput.

`0001` already adds `AV_PIX_FMT_NV15`/`NV20` and `0002` already teaches swscale to
read them, so the fix is to name the two table entries correctly and let the
machinery work as it stands: `hwdownload` yields a real NV15 frame, `hwmap` maps the
dma-buf with no copy, and `format=yuv420p10le` unpacks it. `v4l2request_map_frame()`
needs nothing — it fills `data[]` and `linesize[]` from the DRM layer descriptors and
never consults `sw_format`.

The two guards are deliberately left alone. They still name the planar tags, and that
is still right for the entries that genuinely carry one: the Allwinner tiled NV12, the
Broadcom SAND128 pair and the AFBC formats all keep a logical tag that lies about
their layout, and none of them may be downloaded or mapped.

This replaces a patch that normalised the same two tags back to NV15/NV20 inside the
RGA filters — the same fix applied one consumer at a time. `map_av_to_rga_format()`
maps `AV_PIX_FMT_NV15` natively, so with the tag right the RGA filters need nothing.

## 0007–0013 — defect fixes

Each follows the patch that introduces the file it touches: 0007/0008 need 0004,
0010–0012 need 0003, 0013 needs 0002. `rkmppenc.c` comes from the base, so 0009 has
no graft dependency beyond `--enable-rkmpp`.

| patch | defect |
|---|---|
| `0007` | **Use-after-free then double free.** Both RGA async delivery paths called `av_frame_free()` on a frame `ff_filter_frame()` had already taken. Its contract in `libavfilter/filters.h` says the receiving filter owns the frame on error, and it frees on every negative return — freeing its own parameter, which leaves the caller's pointer dangling. The queue's `queued` count also went unbalanced on that path. |
| `0008` | Only `crop_top` was cleared on the output frame, so a crop the RGA had already applied propagated downstream and asked for a second one. `crop_left` is the field that leaks in practice; all four leak when a caller sets `apply_cropping = 0`. |
| `0009` | **The encoder's DRM import trusted the descriptor.** No `nb_objects`/`nb_layers`/`nb_planes` checks before indexing them; `planes[1].offset / stride` with no check that the stride is non-zero, which is SIGFPE rather than an error; and a frame whose planes live in several dma-bufs imported object 0 and encoded whatever was at that offset. |
| `0010` | The non-linear `ENOSYS` return leaked the mapping descriptor allocated just above it. |
| `0011` | Cacheable buffers requested through the *device* context got no `DMA_BUF_IOCTL_SYNC` — only the frames-level flag was read. The effective answer is now decided once at map time so unmap cannot disagree. |
| `0012` | The pool buffer size multiplied three `int`s before widening to `size_t`. Hardening: reaching `INT_MAX` needs roughly 13000×13000. |
| `0013` | `nv15_20ToPlanarWrapper` stepped whole 5-byte groups (`src_w / 4`, `chrSrcW / 2`), dropping up to three luma samples and one chroma pair at the end of a row. 1920 and 3840 divide cleanly; 1366 loses two pixels per row. The chroma *slice height* truncation is deliberately left alone — see the patch. |

Origins are `yisding/rock-5b-ysp`'s fix-candidate list, re-derived against this tree
rather than ported: two of the defects it describes are not present here, and 0009's
divide-by-zero and 0010's leak are not in it.

**None of the seven has run on hardware.** They compile warning-clean and the series
reproduces byte-for-byte; that is not the same claim.

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

## 0014, 0015 — the subtitle branch

No hardware compositor rasterizes text, so every hardware burn-in chain has the
same two-branch shape: the video on one input, and on the other a transparent
frame with libass drawn into it. These two patches build that second branch; the
compositor at the end of it may be `overlay_rkrga`, `overlay_vulkan` or
`libplacebo`, and none of them care which.

```
alphasrc , format=bgra , subtitles=FILE:alpha=1:sub2video=1 , hwupload ...
```

**`0014` supplies the transparent base.** The `color` filter cannot: `ff_draw_init`
leaves the alpha component alone unless the caller passes `FF_DRAW_PROCESS_ALPHA`
and `vsrc_color` passes `0`, so `color=c=black@0` emits *opaque* black and the
overlay covers the video instead of laying text over it. `alphasrc` constrains
format negotiation to alpha-capable formats and zero-fills, so transparency is a
property of the filter rather than of a colour argument that may not survive
negotiation. It is also required by name: Jellyfin probes `alphasrc` to decide
whether a hardware filter chain is available at all, and silently falls back to
software filtering without it.

**`0015` makes the rasterized overlay's alpha correct.** Drawing onto a
transparent base through the unpatched path gets it wrong twice: `ff_blend_mask`
weights every component by `color->rgba[3]` including the alpha component, whose
source value is the event's own alpha, so the alpha plane receives `a * a`; and
the colour blend against a zero base leaves the premultiplied `a * C` where a
straight-alpha overlay expects `C`. On a 50%-alpha white glyph the branch emits
`R=127 A=63` against a correct `R=255 A=127`. The errors partly cancel under a
straight overlay, which is why the result looks plausible rather than broken —
measured at 34.7 dB against CPU burn-in, where opaque subtitles reach 82.4 dB.
Neither is recoverable downstream, because the per-event alpha is gone from the
frame.

`FF_DRAW_MASK_SRC_ALPHA_OPAQUE` and `FF_DRAW_MASK_UNPREMUL_RGB32` fix them at the
source, set only when `sub2video` is on and the link is not already
premultiplied, so no other `drawutils` caller changes behaviour.
`AVALPHA_MODE_PREMULTIPLIED` is not an alternative: `ff_draw_color` premultiplies
only the colour components and leaves alpha straight, so the alpha error survives
it and the colour is premultiplied twice.

`0015` is the one patch here that touches shared code — `drawutils` is also used
by `drawtext`, `drawbox` and the pad/fill filters. Its additions are gated on new
flags no other caller sets, so the exposure is rebase surface, not behaviour.

Measurements, harnesses and the ASS assets behind the figures above are in
`board-research/media-accel/subtitle-burnin/` of the workspace.

## 0016 — SPS-level RPS

The rkvdec VDPU381/VDPU383 hardware parses slice headers itself, so a slice
that selects its short or long term RPS from the SPS by index needs the SPS
RPS tables in the driver — the Linux 7.2 controls
`V4L2_CID_STATELESS_HEVC_EXT_SPS_ST_RPS` and `_LT_RPS`, which the base never
sends. Without them the driver programs no RPS table (it warns `Long and
short term RPS not set` in dmesg) and every inter frame of such a stream
reconstructs against the wrong references while decoding reports success.

Encoders split on where they put the RPS, which is what makes the gap
treacherous: x265 writes the inline slice-header form the hardware parses
itself, so typical encoder output is untouched, while the HM reference
encoder writes SPS-level sets, so conformance streams concentrate the
failures — JCT-VC-HEVC_V1 on RK3588 runs 15/147 without the controls.

Each short term set is sent in the explicit (non inter-set-predicted) form:
the driver only derives the per-set delta POCs and used flags from the
control, which the explicit syntax encodes exactly, so re-encoding an
inter-predicted set loses nothing. Both controls are probed like the other
optional controls and sent only when the driver exposes them, and their
definitions are carried in the file for builds against pre-7.2 kernel
headers.

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

## 0017–0019 — reaching a hardware JPEG encoder

The RK3588's VEPU121 JPEG encoder is a mainline V4L2 stateful M2M device, bound and
working on `/dev/video2`, and FFmpeg could not reach it in either direction: there is
no V4L2 JPEG M2M codec wrapper upstream, and never has been. That single gap is what
kept every consumer that drives FFmpeg — Jellyfin trickplay, Frigate snapshots — on a
software JPEG encode beside idle silicon. These three patches close it for the encode
direction. They touch no vendor library and speak only the kernel uAPI, so unlike the
graft they are upstreamable as they stand.

Two of the three are generic V4L2 M2M fixes that JPEG happens to be the first codec to
need, and both were found by measuring the driver rather than by reading:

**0017 — the coded format goes first when encoding.** The V4L2 stateful encoder
interface has the client set the coded format on CAPTURE first and the raw format on
OUTPUT second, because a driver derives its raw format from the coded one and may
reset OUTPUT whenever CAPTURE is set. FFmpeg did it the other way round. hantro
implements the interface faithfully — `hantro_set_fmt_cap()` on an encoder calls
`hantro_reset_raw_fmt()` — so asking for `NM12` and then setting the capture format
put the hardware back in `YM12` while FFmpeg's own cached `v4l2_format` still said
`NM12`. Nothing failed; the encoder read the frame's chroma with the wrong plane
layout. Decoding is the same rule with the queues the other way round, which is the
order the code already used, so decode is unchanged.

**0018 — negotiate the fourccs a driver actually offers.** Two assumptions, both
wrong for this device:

- `AV_CODEC_ID_MJPEG` maps to `V4L2_PIX_FMT_MJPEG` *and* `V4L2_PIX_FMT_JPEG`, and
  `ff_v4l2_format_avcodec_to_v4l2()` returned only the first of them.
  `v4l2_get_coded_format()` then required `VIDIOC_ENUM_FMT` to produce exactly that
  fourcc. `rockchip_vpu_hw.c` enumerates only `V4L2_PIX_FMT_JPEG`, so the encoder
  could not probe at all. The driver's enumeration is now the outer loop.
- `v4l2_try_raw_format()` treated a successful `VIDIOC_TRY_FMT` as agreement. A
  driver that cannot do the requested fourcc is *required* to substitute one it can
  and still succeed, so success proves nothing on its own — asking this device for
  `V4L2_PIX_FMT_NV12` on a multiplanar queue returns `YM12`, not `NM12`. The
  returned fourcc is now compared, and every fourcc a pixel format has (the
  contiguous one and the non-contiguous multiplanar one) is offered in turn.

Neither changes anything for a driver whose fourccs are the ones FFmpeg lists first,
which is every V4L2 M2M codec that worked before.

**0019 — the encoder.** JPEG shares none of the inter-coded setup: no GOP, no
bitrate, no rate control, no B-frame probe. Its one control,
`V4L2_CID_JPEG_COMPRESSION_QUALITY`, lives in the JPEG control class, and passing it
under the `V4L2_CTRL_CLASS_MPEG` the code hardcoded is refused with `EINVAL` by the
control framework — so the class is now derived from the control id, which is correct
for every existing call site too. Three further things a JPEG instance needs:

- **The visible size.** A raw OUTPUT format is macroblock aligned, so 1080 lines
  become 1088 and the JPEG's `SOF` would say 1088 with eight rows of alignment
  padding in the picture. V4L2 names the visible part of that buffer with the OUTPUT
  crop rectangle, which hantro reads as the JPEG's own dimensions. Set for every
  encoder, not just this one — an inter-coded stream has the same padding otherwise.
- **A capture buffer the driver sizes.** FFmpeg's estimate is half a raw frame, which
  suits an inter-coded stream and underruns JPEG, whose frames can approach the raw
  picture. Requesting zero is what V4L2 defines for a coded format as "use your own
  maximum".
- **A keyframe flag on every packet.** Every JPEG is a complete intra picture and
  this driver does not flag it, so without this no packet in an `.mjpeg` stream is
  seekable.

### Using it

```sh
ffmpeg -i input.mkv -frames:v 1 -c:v mjpeg_v4l2m2m -quality 80 -update 1 out.jpg
```

`-quality` is the driver's 5–100 percentage, larger is better; `-q:v` maps to the
same scale, and leaving both off keeps the driver's default of 50. Input is
`yuv420p`, `nv12`, `yuyv422` or `uyvy422` — system memory, not `drm_prime`, so a
hardware-decoded frame arrives through FFmpeg's own transfer or an explicit
`hwdownload,format=nv12`.

### What it measures at, on an RK1 at 1080p

Quality is monotonic across the control's range — q=10 gives 49.7 KB at 33.6 dB,
q=50 gives 116 KB at 40.6 dB, q=90 gives 310 KB at 46.8 dB — and the quantisation
tables are byte-identical to GStreamer's `v4l2jpegenc` at the same setting, which is
the cross-check that the wrapper drives the hardware the same way an independent
userspace does. Two honest caveats. Rate-distortion trails FFmpeg's software `mjpeg`
encoder by roughly 7–30% in size at matched PSNR, because the hardware uses fixed
quantisation and Huffman tables where the software encoder optimises them. And the
CPU saving is smaller than the silicon suggests — 4.9 CPU-seconds per 300 frames
against 8.1 for a single-threaded software encode — because what remains is dominated
by the copy of each frame into the V4L2 buffer. The win is steady wall-clock time and
free cores, not a large CPU reduction.

It encodes to 8K. Note a driver-side limit while doing so: `VIDIOC_ENUM_FRAMESIZES`
advertises `8192x8192`, but a dimension of exactly 8192 produces a JPEG whose `SOF`
reads 0x0, silently and under GStreamer too — 8176 is the last size that works in
each axis. Widths are rounded up to a multiple of 4 by the driver's own selection
handling, and the wrapper warns when the coded size it gets back differs from the one
asked for.
