<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# patches

Canonical, version-controlled home for the out-of-tree patches and helpers used
across the device-builder ecosystem (kernel, u-boot, ffmpeg, userspace). Build
systems reference patch sets from here by scope/series rather than vendoring
their own copies, so a fix lands in one place.

## Scopes

Two kinds of scope live here: **subject** scopes, which collect patches for one
capability across SoCs, and **SoC** scopes, which collect what one SoC needs on
top of mainline.

| Dir | Scope |
|-----|-------|
| [`rocket/`](rocket/) | Rockchip NPU — patches, UAPI header, and notes for the mainline `rocket` DRM accel driver (RK3588). |
| [`media-accel/`](media-accel/) | Rockchip HW video transcode (RK35xx/RK3588) — kernel decode/encode/RGA, ffmpeg-rockchip, and MPP/RGA userspace patches. |
| [`crypto/`](crypto/) | Rockchip second-generation crypto offloader (RK356x/RK3588) — AES and hash offload over an LLI DMA engine. |
| [`rk3288/`](rk3288/) | RK3288 kernel fixes, written to upstream standards and meant to retire into stable. |
| [`rk3576/`](rk3576/) | RK3576 kernel fixes plus the out-of-tree u-boot series (USB-flash recovery, USB host, HDMI display). |
| [`rk3588/`](rk3588/) | RK3588 u-boot series (block-device export, recovery tooling). Its kernel patches ride `rk3588-accel`, from the subject scopes above. |
| [`uboot-host/`](uboot-host/) | u-boot host-build fixes — the tooling that runs on the build machine rather than the board. SoC-independent, so every u-boot series carries them ahead of its own patches. |
| [`jellyfin/`](jellyfin/) | Jellyfin **server** patches for Rockchip boards on a mainline kernel, and the Debian packaging patches that build the patched server as a package. Bound by `series/jellyfin.toml`; they also apply by hand to a Jellyfin checkout. |
| [`media-capture/`](media-capture/) | Capture into the hardware pipeline: the FFmpeg V4L2 input device hands out DRM PRIME frames that reference the capture buffers, rather than copies. Bound by `series/media-capture.toml`, which a feature composes on top of an FFmpeg provider series. |

Each scope is **self-contained** and need not share a common internal layout —
`rocket/` keeps the shape it had as its own repo.

## Series

[`series/<name>.toml`](series/) manifests bind an ordered, per-tree patch series
drawn from the scopes above — the unit a builder consumes — to the version range
it targets: `applies_to_kernel` for the kernel-family trees
(`kernel`/`ffmpeg`/`userspace`), `applies_to_uboot` for the `uboot` tree, since
u-boot is its own axis, and `applies_to_app` for an application's two trees
(`app`, `app_packaging`), matched against the application's own release. A series
declares only the ranges of the trees it patches, and a series pinned to a single
upstream generation declares none — the builder's `git am` pass is the enforcement
either way.

[`series/jellyfin.toml`](series/jellyfin.toml) is the Jellyfin server series:
`app` `0001`–`0004` on the server tree, `app_packaging` `packaging/0001`–`0005` on
Jellyfin's packaging tree.

[`series/media-capture.toml`](series/media-capture.toml) is the media capture series:
`ffmpeg` `0001`–`0003`, applied after whichever FFmpeg provider series the build carries.

[`series/rk3588-accel.toml`](series/rk3588-accel.toml) is the RK3588 mainline media
+ NPU + crypto series: kernel `030`–`103`, ffmpeg `v4l2-request/0001`–`0013` then
`0001`–`0022`, userspace `001`. A
series' `kernel` list spans scopes in one `git am` order (`media-accel/kernel/*`, then
`rocket/*`, then `crypto/kernel/*`).

A series spans every kernel generation it can, rather than being forked per version.
Where one patch has to differ, or drops out because upstream took it, that patch — not
the series — carries a `kernels` range, and the file name says which generation it is
for. A series that no longer has a divergence to express declares its floor once and
every entry applies unconditionally, which is where `rk3588-accel`, `rk3576-fixes` and
`rk3576-npu` stand at 7.2.4: the rocket fixes they used to carry are the kernel's own.

## Consuming patches

boot2deb selects a series by name (from its kernel definition, or from an app a
feature declares), pins this repo to an exact commit in the recipe lock (`[patches]
commit`, or the app's own `[apps.patches] commit`), and runs a verify-applies gate
that dry-runs the series with `git am --3way`, hard-erroring on any patch that does
not apply. Offline builds fetch the pinned revision manually.

## Licensing

This repository is mixed-license: repository-original artifacts carry a repository
license, and each patch is a derivative work that inherits the license of the tree it
modifies.

- **Repository-original artifacts** — this README, the `media-accel/` scope READMEs, and
  the series manifests under `series/` — are **GPL-3.0-or-later**
  ([`LICENSES/GPL-3.0-or-later.txt`](LICENSES/GPL-3.0-or-later.txt)), matching the userspace
  projects these series build against.
- **`rocket/` scope** — the NPU driver patches are
  **GPL-2.0-only** (kernel-derivative work; see [`rocket/LICENSE`](rocket/LICENSE)). Its uAPI
  header `rocket/uapi/rocket_accel.h` is **MIT** (© Tomeu Vizoso), per its SPDX tag.
- **`media-accel/` scope** — each patch inherits the license of its upstream tree (the
  Linux kernel, ffmpeg-rockchip, or the Rockchip MPP/RGA userspace); the patch's provenance
  header (`From:` / `Source:`) names that upstream. Kernel patches are GPL-2.0-only.
- **`uboot-host/` scope** — u-boot-derivative work, **GPL-2.0-or-later**, the license
  of the tree it patches.
- **`crypto/` scope** — kernel-derivative work, **GPL-2.0-only**. Patches 100-103 are
  carried verbatim from the linux-rockchip posting and keep their authors' `From:`,
  `Co-developed-by`, `Signed-off-by` and `Tested-by` lines.

Per-file `SPDX-License-Identifier` tags and individual patch headers are authoritative where
present.
