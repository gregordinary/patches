<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# crypto/ — Rockchip second-generation crypto offloader

The RK356x/RK3588 hardware cryptographic accelerator: AES (ECB, CBC, XTS) and
SHA-1/SHA-256/SHA-384/SHA-512, MD5 and SM3, driven through an LLI-based DMA
engine. Applied by the `rk3588-accel` series, which is the RK3588 kernel's
default build — this is not an opt-in add-on.

This is a **subject** scope, not an SoC one: one driver serves both parts, and
the series carries a device-tree node for each.

| patch | what it is |
|---|---|
| `kernel/100-dt-bindings-crypto-rockchip-rk356x-rk3588-engine` | The binding. `rockchip,rk3568-crypto` is the base compatible; RK3588 nodes carry `rockchip,rk3588-crypto` in front of it and bind through the fallback. |
| `kernel/101-crypto-rockchip-rk356x-rk3588-offloader` | The driver (`drivers/crypto/rockchip/rk2_*`), built on the crypto engine framework with a software fallback for every algorithm. |
| `kernel/102-arm64-dts-rockchip-rk356x-crypto-node` | The RK356x node. No board here builds against it; carried so the series stays whole. |
| `kernel/103-arm64-dts-rockchip-rk3588-crypto-node` | The RK3588 node, on the SCMI clocks and reset. |

## The consumer sets the symbol

`CRYPTO_DEV_ROCKCHIP2` is a tristate with no `default`, and this collection sets
no kconfig anywhere — a series of patches is not the right owner for a build
decision, and the in-tree arm64 `defconfig` is a file that moves every release,
so a one-line hunk in it is the least durable way to carry one. **Set the symbol
from your own kconfig fragment.** Without it the driver is not built and 102/103
describe a device nothing binds to, with no build error and no boot message to
say so.

## Origin, and what to expect of it

100-103 are Dawid Olesinski's and Corentin Labbe's, carried **verbatim** from the
linux-rockchip series *"crypto: rockchip: Add RK356x/RK3588 cryptographic
offloader"* at its **v3** posting, with their `Co-developed-by`,
`Signed-off-by` and `Tested-by` lines intact. **v3 is not merged.** It is under
review and should be expected to change; re-sync rather than carrying these
files indefinitely.

Points raised against v3 that a v4 would plausibly move, so that a re-sync knows
what it is looking at:

- `reset-names` admits three names (`core`, `aclk`, `hclk`) while both in-tree
  users list exactly one. The same over-permissiveness was cut from 30 to 1 in
  the RK3576 NPU series' power-domain binding at review.
- The per-SG IV update in `rk2_cipher_run()` computes the **destination** offset
  from a length taken from the **source** scatterlist. It is correct only
  because `rk2_cipher_need_fallback()` rejects a request whose source and
  destination chunks differ, and nothing at the use site says so.
- `dma_free_coherent()` and `kfree(rkc->algs)` in `.remove` run before devres
  frees the IRQ. The handler is guarded by `pm_runtime_get_if_active()` and the
  device is suspended by then, so this is an ordering smell rather than a live
  bug.
- The binding file is named after `rockchip,rk3588-crypto` while the compatible
  it declares as a `const` is `rockchip,rk3568-crypto`.
- The ahash offloads `digest` only; `update`/`final`/`finup`/`export`/`import`
  all fall back, at `cra_priority = 300`.

## Before trusting it on a board

The nodes carry no `status`, so the block is **enabled by default** on every
RK356x and RK3588 board built from these dtsi files. The clocks are the
non-secure instance (`SCMI_ACLK_SECURE_NS` / `SCMI_HCLK_SECURE_NS`) and the
reset is `SCMI_SRST_CRYPTO_CORE`, all of which are in the mainline dt-bindings
headers; `rk3588-base.dtsi` already reaches `scmi_reset` the same way its `rng`
node does. Whether a given board's firmware also drives that block is not
something this series establishes, and it is the first thing to check on a part
that misbehaves after this lands.

The check that closes it is the kernel's own: with the module loaded,
`CONFIG_CRYPTO_MANAGER_DISABLE_TESTS=n` runs the self-tests for every algorithm
the driver registers at load, and `dmesg` names any that fail. Upstream reports
all of them passing on Quartz64-B, NanoPi R5S and NanoPC-T6 LTS at roughly
100 MiB/s; **no board in this tree has been through that run.**

License: GPL-2.0-only (kernel code); see `LICENSES/` at the repo root.
