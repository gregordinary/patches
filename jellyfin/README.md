# Jellyfin patches

Patches against the Jellyfin **server** (`github.com/jellyfin/jellyfin`).
Standalone: applying them needs nothing from this repository but the patch
files, and building needs nothing but Jellyfin's own toolchain.

Base: `master` at `d6bcad3b5` (`v12.0-rc5-70`).

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

Three things it deliberately does not do.

**It never asks for a hardware surface.** FFmpeg has no V4L2 filter to consume
one, so the chain is the software chain and every frame is downloaded.

**It declines anything but 8-bit.** `hwcontext_v4l2request` offers no transfer
format for 10-bit frames — the hardware writes a packed layout with no
system-memory equivalent — so a 10-bit frame could never be read back out of the
software chain. Those decode in software, which is what happens today.

**It is decode only.** The request API describes no encoder, so the type appears
in neither codec map and selecting it to encode with falls back to the software
encoder. On its own that is a losing trade: `libx264` costs more than the
hardware decode saves (68.8 CPU-seconds against 32.7 at 1080p). It is worth
having *because* `0002` lets it be paired with an encoder from another stack.

FFmpeg upstream has no V4L2 request-API decoders — they live in Kwiboo's
`v4l2-request` tree — so a build that advertises the hwaccel is by construction
built for a mainline kernel, and where it is absent the type simply never
resolves.

## Applying and building

```sh
git clone https://github.com/jellyfin/jellyfin.git
cd jellyfin
git am --3way /path/to/patches/jellyfin/*.patch
dotnet publish Jellyfin.Server --configuration Release --output ./out
```

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

**An unpatched server ignores the two new elements** — `XmlSerializer` drops
unknown elements — so the same file is safe to ship to a stock install, where it
behaves as `HardwareAccelerationType` alone always did. On a stock server, leave
`HardwareDecodingCodecs` empty: naming codecs there makes it emit
`-hwaccel rkmpp`, which is the silent software fallback measured above.

## Verification

Against `master` at `d6bcad3b5`, on .NET 10.0.400:

- The series applies under `git am --3way` and under strict `git apply` applied
  in order, and reproduces the authoring tree byte-for-byte.
- `dotnet build Jellyfin.sln` is clean.
- 1741 tests pass across `Jellyfin.Controller.Tests` (128, including 16 added
  here), `Jellyfin.Model.Tests` (673), `Jellyfin.Server.Implementations.Tests`
  (704), `Jellyfin.MediaEncoding.Tests` (97) and `Jellyfin.Api.Tests` (139).

The added tests pin the two properties that make the change safe: every
acceleration type resolves both halves to itself when the new fields are unset,
and a same-stack configuration initialises its device exactly once. They also
pin the RK3588 pairing end to end — `-hwaccel v4l2request` with `hevc_rkmpp` —
and the 10-bit refusal.

**Not yet run on hardware.** The measurements quoted above were taken with the
previous, narrower version of this work, which reached the same FFmpeg command
line by hard-coding the pairing inside the `rkmpp` type.
