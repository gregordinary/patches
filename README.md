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
| [`rk3288/`](rk3288/) | RK3288 kernel fixes, written to upstream standards and meant to retire into stable. |
| [`rk3576/`](rk3576/) | RK3576 kernel fixes plus the out-of-tree u-boot series (USB-flash recovery, USB host, HDMI display). |
| [`rk3588/`](rk3588/) | RK3588 u-boot series (block-device export, recovery tooling). Its kernel patches ride `rk3588-accel`, from the subject scopes above. |

Each scope is **self-contained** and need not share a common internal layout —
`rocket/` keeps the shape it had as its own repo.

## Series

[`series/<name>.toml`](series/) manifests bind an ordered, per-tree patch series
drawn from the scopes above — the unit a builder consumes — to the version range
it targets: `applies_to_kernel` for the kernel-family trees
(`kernel`/`ffmpeg`/`userspace`), `applies_to_uboot` for the `uboot` tree, since
u-boot is its own axis. A series that patches only one of the two declares only
that range, and a series pinned to a single upstream generation declares neither
— the builder's `git am` pass is the enforcement either way.

[`series/rk3588-accel.toml`](series/rk3588-accel.toml) is the RK3588 mainline-7.1 media
+ NPU series: kernel `040`–`089`, ffmpeg `0001`, userspace `001`. A series'
`kernel` list spans scopes in one `git am` order (`media-accel/kernel/*` then
`rocket/*`).

## Consuming patches

boot2deb selects a series by name (from its kernel definition), pins this repo to
an exact commit in the recipe lock (`[patches] commit`), and runs a verify-applies
gate that dry-runs the series with `git am --3way`, hard-erroring on any patch that
does not apply. Offline builds fetch the pinned revision manually.

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

Per-file `SPDX-License-Identifier` tags and individual patch headers are authoritative where
present.
