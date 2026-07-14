<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# patches

Canonical, version-controlled home for the out-of-tree patches and helpers used
across the device-builder ecosystem (kernel, u-boot, ffmpeg, userspace). Build
systems reference patch sets from here by scope/profile rather than vendoring
their own copies, so a fix lands in one place.

## Scopes

| Dir | Scope |
|-----|-------|
| [`rocket/`](rocket/) | Rockchip NPU — patches, UAPI header, and notes for the mainline `rocket` DRM accel driver (RK3588). |
| [`media-accel/`](media-accel/) | Rockchip HW video transcode (RK35xx/RK3588) — kernel decode/encode/RGA, ffmpeg-rockchip, and MPP/RGA userspace patches. |

Each scope is **self-contained** and need not share a common internal layout —
`rocket/` keeps the shape it had as its own repo. Future scopes (kernel
video-codec accel, ffmpeg-rockchip, MPP userspace) will be added as the generic
builder is built out.

## Profiles

[`profiles/<name>/profile.toml`](profiles/) manifests bind a kernel-version range
(`applies_to_kernel`) to an ordered, per-tree patch series drawn from the scopes
above — the unit a builder consumes.
[`profiles/rk3588-accel`](profiles/rk3588-accel/) is the RK3588 mainline-7.1 media
+ NPU series: kernel `040`–`085`, ffmpeg `0001`, userspace `001`. A profile's
`kernel` list spans scopes in one `git am` order (`media-accel/kernel/*` then
`rocket/*`).

## Consuming patches

boot2deb selects a profile by name (from its kernel definition), pins this repo to
an exact commit in the recipe lock (`[patches] commit`), and runs a verify-applies
gate that dry-runs the series with `git am --3way`, hard-erroring on any patch that
does not apply. Offline builds fetch the pinned revision manually.

## Licensing

This repository is mixed-license: repository-original artifacts carry a repository
license, and each patch is a derivative work that inherits the license of the tree it
modifies.

- **Repository-original artifacts** — this README, the `media-accel/` scope READMEs, and
  the profile manifests under `profiles/` — are **GPL-3.0-or-later**
  ([`LICENSES/GPL-3.0-or-later.txt`](LICENSES/GPL-3.0-or-later.txt)), matching the userspace
  projects these profiles build against.
- **`rocket/` scope** — the NPU driver patches are
  **GPL-2.0-only** (kernel-derivative work; see [`rocket/LICENSE`](rocket/LICENSE)). Its uAPI
  header `rocket/uapi/rocket_accel.h` is **MIT** (© Tomeu Vizoso), per its SPDX tag.
- **`media-accel/` scope** — each patch inherits the license of its upstream tree (the
  Linux kernel, ffmpeg-rockchip, or the Rockchip MPP/RGA userspace); the patch's provenance
  header (`From:` / `Source:`) names that upstream. Kernel patches are GPL-2.0-only.

Per-file `SPDX-License-Identifier` tags and individual patch headers are authoritative where
present.
