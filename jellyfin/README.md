# Jellyfin patches

Patches against the Jellyfin **server** (`github.com/jellyfin/jellyfin`), and
against Jellyfin's own Debian **packaging** (`github.com/jellyfin/jellyfin-packaging`,
under [`packaging/`](packaging/)) so the patched server builds as a package.
Standalone: applying them needs nothing from this repository but the patch
files, and building needs nothing but Jellyfin's own toolchain.

[`series/jellyfin.toml`](../series/jellyfin.toml) binds both: its `app` scope is
the server patches below, its `app_packaging` scope the packaging patches.

Base: the server at `v12.2` (`ca0f16eb7`), the packaging at `v12.2-202610051851`,
the tag that packaged that release.

## Series

| patch | what it does |
| --- | --- |
| `0001` | Route every hardware-acceleration read in `EncodingHelper` through a decode accessor or an encode accessor. No behaviour change. |
| `0002` | Add `HardwareDecodingType` / `HardwareEncodingType` to `EncodingOptions` so the two halves can name different stacks. |
| `0003` | Initialise the encoder's hardware device when a split pairing puts it on a stack the decoder's branch never reached. |
| `0004` | Add a `v4l2request` acceleration type. |

Apply them together. `0001` on its own is a refactor, and `0004` on its own is
decode-only, which costs more than it saves — see below.

## The problem

`HardwareAccelerationType` is one setting that decides both which decoder and
which encoder a transcode uses. A platform whose best decoder and best encoder
come from different stacks cannot be described. It has to pick one and lose the
other.

That is exactly a Rockchip board on a mainline kernel. Decode is `rkvdec`, a
V4L2 **stateless** driver reached through the request API. Encode is VEPU580
through MPP — mainline has no H.264/HEVC encoder driver for RK3588 at all
(`/dev/video2` is `rockchip,rk3588-vepu121-enc`, the Hantro JPEG encoder, and
`h264_v4l2m2m` reports `Could not find a valid device`). Jellyfin's `rkmpp`
type names the encoder correctly and the decoder wrongly: the `rkmpp` hwaccel is
compiled into FFmpeg, so it is offered, it fails to open because there is no MPP
service on a mainline kernel, and the decode falls back to software without
saying so.

Measured on a Turing RK1 (RK3588, kernel 7.1.7), 1080p HEVC → 720p, 900 frames,
CPU-seconds `utime+stime`:

| what Jellyfin emits | CPU-s | wall |
| --- | ---: | ---: |
| `-hwaccel rkmpp` | 50.4 | 8.18 s |
| no hwaccel at all | 50.2 | 8.10 s |
| **`-hwaccel v4l2request`** | **17.0** | **3.49 s** |

So the type was costing exactly what software decode costs, and the flag it
could not name uses **2.96x less CPU** and runs **2.3x faster**.

## Who else this is for

Not only Rockchip. The same shape appears wherever a mainline kernel exposes a
stateless V4L2 decoder and the encoder is somewhere else:

- **Rockchip RK3576** — same decode story; a mainline V4L2 *stateful* VEPU510
  encoder driver is on-list as an RFC. If it merges, that board wants
  `v4l2request` for decode and `v4l2m2m` for encode: two types Jellyfin already
  has, and still no way to combine them.
- **Allwinner, Amlogic, NXP, Raspberry Pi** — `cedrus` and friends are request
  API decoders with no matching encoder in the same family.
- **Intel/AMD mixes** — an Intel iGPU for QSV decode beside a discrete AMD card
  for AMF encode is expressible on the FFmpeg command line and not in Jellyfin.

## What the split does not change

Both new fields are nullable and default to null, which follows
`HardwareAccelerationType`. An untouched `encoding.xml` produces the command
line it produced before, on both halves, and there is nothing to migrate.

The filter chain follows the **decoder**, because the surface it has to accept
is the one the decoder produced. Where the encoder is on another stack the chain
has to end in a download; `GetSwVidFilterChain` already does that and is already
the fallback whenever the preferred hardware chain is unavailable.

## 0004 — the `v4l2request` type

**It takes 4:2:0 streams at 8 and 10 bits.** A 10-bit frame reaches the software
chain the way an 8-bit one does: FFmpeg's automatic download hands the chain the
layout the decoder wrote, `NV15` on `rkvdec` and `P010` elsewhere, and swscale
converts it. `NV15` takes an FFmpeg whose `hwcontext_v4l2request` tags those frames
as `NV15` and whose swscale reads them; on one that cannot, a 10-bit transcode
fails rather than falling back. H.264 Hi10P is admitted as it is for `rkmpp`,
because `rkvdec` decodes it. Where no request-API decoder accepts a stream,
FFmpeg's device probe finds none and FFmpeg decodes it in software. Measured on
the RK3588 at kernel 7.2.9, HEVC Main 10, VP9 profile 2, H.264 High 10 and 10-bit
AV1 decode bit-exact, and 4K HEVC Main 10 → 1080p costs 101 CPU-seconds against
380 for software decode, which takes it from 0.53x to 1.6x realtime.

Two things it deliberately does not do.

**It never asks for a hardware surface.** FFmpeg has no V4L2 filter to consume
one, so the chain is the software chain and every frame is downloaded.

**It is decode only.** The request API describes no encoder, so the type appears
in neither codec map and selecting it to encode with falls back to the software
encoder. On its own that is a losing trade: `libx264` costs more than the
hardware decode saves (68.8 CPU-seconds against 32.7 at 1080p). It is worth
having *because* `0002` lets it be paired with an encoder from another stack.

FFmpeg upstream has no V4L2 request-API decoders — they live in Kwiboo's
`v4l2-request` tree — so a build that advertises the hwaccel is by construction
built for a mainline kernel, and where it is absent the type simply never
resolves.

## Packaging

Jellyfin's `.deb`s are built by `jellyfin-packaging`, whose `debian/` expects the
server tree at `jellyfin-server/` and publishes it with `dotnet publish`. Four of
these patches make that packaging build outside Jellyfin's own container; the fifth
pairs a server built from source with the web client of its release.

| patch | what it does |
| --- | --- |
| `packaging/0001` | Build the server and the web client in `override_dh_auto_build-arch` and `-indep`, so `dpkg-buildpackage -B` builds the server without Node.js. |
| `packaging/0002` | Take the .NET runtime identifier from `DEB_HOST_ARCH`. `$(shell arch)` prints `aarch64` on an arm64 host, which matched none of the branches. |
| `packaging/0003` | Skip `dh_shlibdeps`. The control file names every dependency by hand and has no `${shlibs:Depends}` for it to fill, and in a cross build it stops on every target library outside the multiarch directories. |
| `packaging/0004` | Read the ICU soname from the installed `libicu-dev` rather than `apt-cache policy`, which needs package lists a build environment may not have. |
| `packaging/0005` | Depend on the `jellyfin-web` of the server's own release, so a solver installs the matching web client and an upgrade holds it back until the server moves with it. |

## Applying and building

```sh
git clone https://github.com/jellyfin/jellyfin.git
cd jellyfin
git am --3way /path/to/patches/jellyfin/*.patch
dotnet publish Jellyfin.Server --configuration Release --output ./out
```

As a package: check out `jellyfin-packaging` at the release tag, apply
`packaging/*.patch` to it, lay the patched server tree at `jellyfin-server/`, write
a `debian/changelog` entry for the version, and run `dpkg-buildpackage -B`. boot2deb's
`jellyfin-v4l2request` feature does exactly this, offline, from pinned sources.

Jellyfin's own README documents `dotnet build` plus running the output from
`Jellyfin.Server/bin/Debug/net10.0`; either is fine, and `dotnet publish` is
shown here only because a self-contained output directory is easier to drop over
a packaged install.

Nothing outside the server is touched, so `jellyfin-web` is unmodified and the
stock package can be used with it. To replace the server in a Debian install,
keep the packaged `jellyfin-web` and the packaged unit file and swap only the
server assemblies under `/usr/lib/jellyfin/bin/`.

**`jellyfin-web` has no selector for the two new fields.** They are set by
editing `encoding.xml`, below, or through the API. Adding the two optional
selectors to the web UI is a separate change in a separate repository.

## Server configuration

On an RK3588 with a mainline kernel:

```xml
<HardwareAccelerationType>rkmpp</HardwareAccelerationType>
<HardwareDecodingType>v4l2request</HardwareDecodingType>
<HardwareEncodingType>rkmpp</HardwareEncodingType>
<EnableHardwareEncoding>true</EnableHardwareEncoding>
<HardwareDecodingCodecs>
  <string>h264</string>
  <string>hevc</string>
  <string>vp9</string>
</HardwareDecodingCodecs>
```

`HardwareAccelerationType` still carries the pairing's "family" for anything
that reads it directly, and the two specific fields override it.

List in `HardwareDecodingCodecs` only what the board in question decodes. On
RK3588 that is H.264, HEVC and VP9. AV1 is decoded by a separate `hantro-vpu`
node whose output is bit-exact on most streams but has known deterministic gaps
— add it only if those have been checked against the content in question.

10-bit content follows the codec list, and Jellyfin's 10-bit HEVC and VP9
switches (`EnableDecodingColorDepth10Hevc`, `EnableDecodingColorDepth10Vp9`, both
on by default) apply to this type as to any other.

**An unpatched server ignores the two new elements** — `XmlSerializer` drops
unknown elements — so the same file is safe to ship to a stock install, where it
behaves as `HardwareAccelerationType` alone always did. On a stock server, leave
`HardwareDecodingCodecs` empty: naming codecs there makes it emit
`-hwaccel rkmpp`, which is the silent software fallback measured above.

## Verification

Against `v12.2`, on .NET 10.0.401:

- The server patches apply under strict `git am`, and a self-contained
  `linux-arm64` publish of `Jellyfin.Server` is clean.
- The packaging patches apply under strict `git am` to `v12.2-202610051851` and
  reproduce the authoring tree.
- `Jellyfin.Model.Tests` (748), `Jellyfin.Server.Implementations.Tests` (1074, 12
  skipped), `Jellyfin.MediaEncoding.Tests` (100, 1 skipped) and `Jellyfin.Api.Tests`
  (156) pass. `Jellyfin.Controller.Tests` passes all 235, the 21 added here among
  them. Two upstream cases in it are order-dependent,
  `PlaylistTests.IsVisible_PlaylistWithNothingLeftInIt_IsHidden` and one in
  `BaseItemTests`: they can fail when the project runs whole, in stock `v12.2` too,
  and pass run on their own.

The added tests pin the two properties that make the change safe: every
acceleration type resolves both halves to itself when the new fields are unset,
and a same-stack configuration initialises its device exactly once. They also
pin the RK3588 pairing end to end — `-hwaccel v4l2request` with `hevc_rkmpp` —
and which streams the type admits: 10-bit 4:2:0 HEVC, VP9, AV1 and H.264 Hi10P take
the hardware path, the operator's 10-bit HEVC switch still declines it, and 4:2:2
stays in software.

**Validated on hardware, 2026-10-08.** The series on `v12.2`, built as a package by
boot2deb (.NET 10.0.401), ran on a Turing RK1 (kernel 7.2.9) with the configuration
above. Its FFmpeg carried `media-accel/ffmpeg` through `0023` and libdav1d. Transcodes
forced through the Jellyfin API emitted the intended command lines and ran to
completion, 900 frames each:

- 4K HEVC Main 10 → 1080p: `-hwaccel v4l2request` with `h264_rkmpp`, 1.4x realtime.
  Software decode runs the same transcode at 0.53x.
- 1080p HEVC Main 10, VP9 profile 2 and H.264 High 10 → 720p: `-hwaccel v4l2request`,
  4.4-4.9x.
- 1080p 8-bit H.264 → 720p: `-hwaccel v4l2request`, decoded on `rkvdec`, 7.9x.
- 1080p HEVC Main 10 with `EnableDecodingColorDepth10Hevc` turned off: no `-hwaccel`,
  software decode, 3.0x.
- 10-bit AV1 left out of the codec list: software decode through libdav1d, 2.4x. With
  `av1` added: `-hwaccel v4l2request`, 3.8x.
