# rk3288/ — RK3288 kernel fixes

Standalone fixes for the RK3288's upstream drivers, applied on top of a mainline
kernel by the `rk3288-fixes` profile. Everything here is written to upstream
standards (mbox format, `Fixes:` tag, `Cc: stable`) and is meant to leave this
repo: a patch retires when it lands in the stable series the profile targets.

| patch | what it fixes |
|---|---|
| `kernel/100-crypto-rk3288-inherit-fallback-ahash-statesize` | rk-sha1/rk-sha256/rk-md5 self-test failures (`export() overran state buffer`): the driver declares `sizeof(struct shaN_state)` while exporting its fallback's larger partial-block state; inherit the fallback's statesize at init_tfm, as the driver already does for reqsize. |

License: GPL-2.0-only (kernel code); see `LICENSES/` at the repo root.
