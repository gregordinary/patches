<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# kernel graft (media-accel)

The kernel half of the media-accel stack: hardware **decode**, **encode**, and 2D
**scale/CSC/blend** on mainline Linux, without the vendor BSP kernel. The parent
[`../README.md`](../README.md) indexes what each patch enables; this covers how the
series fits together.

## Series (apply order)

1. `040-vdpu381-multicore-v1-curated.patch`
2. `050-av1-iommu-v14-curated.patch`
3. `060-vepu580-rcawston-v3.patch`
4. `070-rga-multicore-vendor-oot.patch`
5. `071-rga-multicore-7.1-fixups.patch`

The order is the profile's list, not the filename prefixes; the prefixes only make the
directory read the same way. The gaps between them are deliberate room to slot a patch
in without renumbering a committed series.

## Provenance, and what that implies

The four scopes here come from different places, and they age differently:

- **`040` / `050` — mainline-track, curated.** Stateless V4L2 decode and the AV1 helper
  library, taken from upstream posting series and curated to apply as one. These are the
  patches most likely to be absorbed by mainline, which retires them: a patch that stops
  applying because the code is already there is an upper bound on its range, not a break.
- **`060` — out-of-tree by its author's intent.** The VEPU580 encoder is an OOT driver on
  the MPP framework, explicitly not offered for upstream, so it does not retire on its
  own — it stays until something replaces it.
- **`070` / `071` — vendor code, forward-ported.** The multicore RGA driver is vendor OOT,
  and `071` is the forward-port that keeps it building on a newer kernel. This pair is
  where a kernel bump most often lands: `070` is the driver, `071` is the delta that
  tracks the kernel, so a rework normally touches `071` alone.

Anything NPU-shaped lives in the sibling [`../../rocket/`](../../rocket/) scope, which
has its own dependency notes.

## Which kernels these apply to

Not recorded here — it moves on a different clock than this list does, and a copy would
drift. The `profile.toml` that selects these patches carries the kernel window, per patch
where they differ. What has actually been *validated*, on which board, is generated from
the shipped locks rather than written down.

License: GPL-2.0-only (kernel code); see `LICENSES/` at the repo root.
