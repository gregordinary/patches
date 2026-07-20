<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# Changelog

Releases of this patch collection, and the kernel window each one covers.

**A release marks a validation event, not a commit.** The trigger for a tag is "this
series is validated against kernel X on these boards" — rare, meaningful, and worth
citing. Tagging every incremental addition would produce version numbers that look
semantic without being so, which is worse than a bare commit id because it invites
confidence they have not earned.

So this file has entries only for validation events. Between them, consumers pin a
commit and read compatibility out of the profile manifests, which is what those are for:

- **`profiles/<name>/profile.toml`** carries the kernel window — an overall envelope
  plus a per-patch range wherever one patch's boundary differs from the rest. That is the
  compatibility mechanism; tags are not.
- **The commit id** is the reproducibility pin. A build records it exactly, so an old
  build is rebuildable whether or not a tag ever existed at that point.

Nothing is blocked by the absence of a tag. `ref = "main"` in a consumer's lock is the
honest value for a series that has not had a validation event yet — it says the pin came
from the tip of development rather than implying a release nobody cut.

## Unreleased

No release has been cut yet. The first tag will be the first "validated against kernel X
on these boards" moment, and will land here paired with its kernel window.
