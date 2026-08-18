# Jellyfin patches

Patches against the Jellyfin **server** (`github.com/jellyfin/jellyfin`) for
Rockchip boards running a mainline kernel. Standalone: applying them needs
nothing from this repository but the patch files, and building needs nothing but
Jellyfin's own toolchain.

Base: `master` at `e6e3099`. They are small and touch one file, so they rebase
across releases with little friction.

## Series

| patch | what it does |
| --- | --- |
| `0001` | Under the `rkmpp` acceleration type, decode with `-hwaccel v4l2request` when the FFmpeg build carries that hwaccel. |

## 0001 — decode with v4l2request under the rkmpp type

**The problem.** Jellyfin's `rkmpp` acceleration type reaches the Rockchip
decoder through MPP, which needs the vendor kernel's MPP service. On a mainline
kernel the same silicon is driven by `rkvdec`, a V4L2 **stateless** driver
reached through the request API, and no MPP service exists. FFmpeg still
advertises the `rkmpp` hwaccel there, so Jellyfin offers it, it fails to open,
and the decode falls back to software without saying so.

Measured on a Turing RK1 (RK3588, kernel 7.1.7), 1080p HEVC → 720p, 900 frames,
CPU-seconds `utime+stime`:

| what Jellyfin emits | CPU-s | wall |
| --- | ---: | ---: |
| `-hwaccel rkmpp` (before) | 50.4 | 8.18 s |
| no hwaccel at all | 50.2 | 8.10 s |
| **`-hwaccel v4l2request` (after)** | **17.0** | **3.49 s** |

So the type was costing exactly what software decode costs. With the patch the
same transcode uses **2.96x less CPU** and runs **2.3x faster**.

**The discriminator.** The patch prefers `v4l2request` when
`_mediaEncoder.SupportsHwaccel("v4l2request")` is true. FFmpeg upstream has no
V4L2 request-API decoders — they live in Kwiboo's `v4l2-request` tree — so a
build that carries the hwaccel is by construction built for a mainline kernel.
Where it is absent, including every `jellyfin-ffmpeg` build, nothing changes.

**Why this is not a new acceleration type.** Jellyfin's acceleration type picks
the decoder *and* the encoder. Our two halves come from different stacks and
there is no way to say so:

- **Decode** is mainline V4L2 request-API (`rkvdec`).
- **Encode** is MPP. RK3588 has no mainline H.264/HEVC encoder driver;
  `h264_v4l2m2m` and `hevc_v4l2m2m` are compiled in and both report
  `Could not find a valid device`, and the only mainline encoder node present is
  `/dev/video2`, `rockchip,rk3588-vepu121-enc`, the Hantro JPEG encoder.

A generic `v4l2request` type would therefore be decode-only and would fall back
to `libx264`, which costs more than the decode saves (68.8 CPU-seconds against
32.7 at 1080p). Keeping the change inside the `rkmpp` type is what lets one
acceleration type name a mainline decoder and an MPP encoder — which is what the
hardware actually is on this kernel.

The real fix is to let Jellyfin choose its decoder and its encoder separately.
That is a much larger change and is planned separately; see
`PROJECT-PLAN-jellyfin-decode-encode-split.md` in the workspace root.

**10-bit is unchanged.** `GetRkmppVidDecoder` already returns `null` for 10-bit
without a hardware surface, and `v4l2request` cannot transfer NV15 to system
memory (`v4l2request_transfer_get_formats()` blanks its list for `YUV420P10`),
so 10-bit content keeps decoding in software. That is correct rather than a
limitation of the patch.

## Applying and building

```sh
git clone https://github.com/jellyfin/jellyfin.git
cd jellyfin
git am --3way /path/to/patches/jellyfin/0001-*.patch
dotnet publish Jellyfin.Server --configuration Release --output ./out
```

Jellyfin's own README documents `dotnet build` plus running the output from
`Jellyfin.Server/bin/Debug/net10.0`; either is fine, and `dotnet publish` is
shown here only because a self-contained output directory is easier to drop over
a packaged install.

Nothing else in the repository is touched, so `jellyfin-web` is unmodified and
the stock package can be used with it. To replace the server in a Debian
install, keep the packaged `jellyfin-web` and the packaged unit file and swap
only the server assemblies under `/usr/lib/jellyfin/bin/`.

## Server configuration

The patch changes *what* Jellyfin emits, not *whether* it emits it. Hardware
decoding is still gated on `HardwareDecodingCodecs`, which is empty by default:

```xml
<HardwareAccelerationType>rkmpp</HardwareAccelerationType>
<HardwareDecodingCodecs>
  <string>h264</string>
  <string>hevc</string>
  <string>vp9</string>
</HardwareDecodingCodecs>
```

List only what `rkvdec` decodes on the board in question. On RK3588 that is
H.264, HEVC and VP9; AV1 is a separate `hantro-vpu` node whose correctness has
not been established here, so it is left out.
