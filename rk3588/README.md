<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
# rk3588/ — RK3588 support

Out-of-tree u-boot patches for RK3588 boards. The RK3588 media and NPU kernel
patches are not here: they ride the kernel axis in the `rk3588-accel` series,
drawn from the `media-accel/` and `rocket/` scopes.

## u-boot

Mainline u-boot already brings up an RK3588 board and everything on it that a
flashing session needs — `turing-rk1-rk3588_defconfig` enables the PCIe
controller and the NVMe-over-PCIe driver, and `CONFIG_CMD_BOOTI` selects
`CONFIG_CMD_UNZIP` on arm64, which is what carries `gzwrite`. So these series
are small: they turn on commands, not drivers.

| series | adds | what it is for |
|---|---|---|
| `turing-rk1-recovery` | `ums`, `setexpr` | the u-boot an image ships with |
| `turing-rk1-util` | recovery + `bootmenu`, `memtest`, `md5sum`, `sha1sum`, `wget`, `read`, `write` | the recovery and bring-up tool |

The list in each `series` file is the authoritative apply order; the numeric
filename prefixes only make the directory read in that order.

Both series are board-specific — they patch `configs/turing-rk1-rk3588_defconfig`,
not a SoC-generic defconfig — so they live under `uboot/boards/` and carry the
board's name. A SoC-generic RK3588 series, if one is ever needed, belongs at
`uboot/` alongside that directory, as the `rk3576` scope is arranged.

## uboot/boards/

Board-specific u-boot patches, one directory per board.

### uboot/boards/turing-rk1/

| patch | what it does |
|---|---|
| `0001-export-block-devices-over-usb-with-ums` | `ums` exports any block device u-boot can reach as USB mass storage, so `ums 0 nvme 0` puts the M.2 disk in front of the carrier's host port; also restores the default-y `setexpr` the board defconfig had turned off |
| `0002-enable-the-util-command-set` | the util command set: `bootmenu`, `memtest`, `md5sum`, `sha1sum`, `wget`, `read`, `write` |

### Why `ums` is the whole point

A compute module has no front-panel USB. `usb_host0_xhci` is its only
OTG-capable controller — USB 2.0 high-speed, per the board's upstream DT — and
what sits on the other end is the carrier board's decision. On a cluster
carrier that is a management controller, and such a controller typically knows
how to write the module's eMMC and nothing else, because the loader it streams
into the module only speaks eMMC.

The disk on the M.2 slot behind `pcie3x4` is therefore one u-boot enumerates
perfectly well and no host can address. `ums` is the one command that closes
the gap, and it costs a single Kconfig symbol: `CONFIG_CMD_USB_MASS_STORAGE`
depends only on `BLK` and `USB_GADGET_DOWNLOAD`, both of which the board
defconfig already sets for `rockusb`.

License: GPL-2.0-only (u-boot code); see `LICENSES/` at the repo root.
